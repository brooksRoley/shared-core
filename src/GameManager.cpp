#include "GameManager.h"
#include "json.hpp"
#include <iostream>

void GameManager::SpawnPlayer(int id, std::string name, float speed, float shooting) {
    activeRoster[id] = std::make_shared<PlayerEntity>(id, std::move(name), speed, shooting);
    std::cout << "Engine: Spawned " << activeRoster[id]->name << " into the simulation.\n";
}

void GameManager::SpawnUnit(int id, std::string name, std::string team,
                            float speed, float shooting, float defense) {
    auto player = std::make_shared<PlayerEntity>(id, std::move(name), speed, shooting);
    player->team          = std::move(team);
    player->stats.defense = defense;
    player->ClampStats();
    activeRoster[id] = player;
    std::cout << "Engine: Spawned " << activeRoster[id]->name << " into the simulation.\n";
}

void GameManager::SetAwayTeamJSON(std::string jsonData) {
    using json = nlohmann::json;
    pendingAwayTeam.clear();
    try {
        auto parsed = json::parse(jsonData);
        // Accept either a bare array of units or {"units": [...]}
        json units = parsed.is_array() ? parsed
                   : (parsed.is_object() && parsed.contains("units")) ? parsed.at("units")
                   : json::array();

        int nextId = 901;
        for (auto& entry : units) {
            if (!entry.is_object()) continue;
            AwayUnitDef def;

            // Ghost ids come from another player's board and may collide with
            // home unit ids (or be strings, e.g. "bot_1") — always remap.
            def.id = nextId++;

            if (entry.contains("name") && entry.at("name").is_string()) {
                def.name = entry.at("name").get<std::string>();
            }
            if (entry.contains("stats") && entry.at("stats").is_object()) {
                auto& s = entry.at("stats");
                if (s.contains("speed"))    def.speed    = s.at("speed").get<float>();
                if (s.contains("shooting")) def.shooting = s.at("shooting").get<float>();
                if (s.contains("defense"))  def.defense  = s.at("defense").get<float>();
            }
            // Grid coords: accept x/y or courtX/courtY, clamp to the 0-4 grid
            auto readCoord = [&entry](const char* a, const char* b, float fallback) {
                if (entry.contains(a) && entry.at(a).is_number()) return entry.at(a).get<float>();
                if (entry.contains(b) && entry.at(b).is_number()) return entry.at(b).get<float>();
                return fallback;
            };
            def.gridX = std::min(4.0f, std::max(0.0f, readCoord("x", "courtX", 2.0f)));
            def.gridY = std::min(4.0f, std::max(0.0f, readCoord("y", "courtY", 2.0f)));

            // Bench-hero tags, same field names as engine_roster.json
            if (entry.contains("role") && entry.at("role").is_string())
                def.isBenchHero = entry.at("role").get<std::string>() == "bench_hero";
            if (entry.contains("ppg_mean") && entry.at("ppg_mean").is_number())
                def.ppgMean = entry.at("ppg_mean").get<float>();
            if (entry.contains("ppg_stddev") && entry.at("ppg_stddev").is_number())
                def.ppgStddev = entry.at("ppg_stddev").get<float>();

            pendingAwayTeam.push_back(def);
        }
        std::cout << "Engine: Loaded " << pendingAwayTeam.size() << " away units from ghost board\n";
    } catch (const json::exception& e) {
        pendingAwayTeam.clear();
        std::cerr << "Engine: Failed to parse away team JSON: " << e.what() << "\n";
    }
}

void GameManager::SetBenchHero(int playerId, float ppgMean, float ppgStddev) {
    auto it = activeRoster.find(playerId);
    if (it != activeRoster.end()) {
        it->second->isBenchHero = true;
        it->second->ppgMean     = ppgMean;
        it->second->ppgStddev   = ppgStddev;
    }
}

void GameManager::SetPlayerPlay(int playerId, int actionTypeInt, float targetX, float targetY) {
    auto it = activeRoster.find(playerId);
    if (it != activeRoster.end()) {
        ActionType action = static_cast<ActionType>(actionTypeInt);
        it->second->AssignPlay(action, targetX, targetY);
        std::cout << "Engine: " << it->second->name
                  << " assigned action " << actionTypeInt
                  << " targeting (" << targetX << ", " << targetY << ")\n";
    }
}

void GameManager::TickSimulation(float deltaTime) {
    if (!court.GetHomeTeam().empty()) {
        // Round in progress — let the court sim drive everything
        court.UpdateSimulationStep(deltaTime);
    } else {
        // Pre-round (e.g. unit tests): just move players via their planned actions
        for (auto& [id, player] : activeRoster) {
            player->UpdatePhysicsTick(deltaTime);
        }
    }
}

std::string GameManager::GetGameStateJSON() {
    std::string json = "{ \"players\": [";
    bool first = true;

    // During a round read court state; before StartRound read the raw roster
    const auto& homeTeam = court.GetHomeTeam();
    if (!homeTeam.empty()) {
        for (auto& p : homeTeam) {
            if (!first) json += ", ";
            json += "{\"id\": " + std::to_string(p->id) +
                    ", \"name\": \"" + p->name + "\"" +
                    ", \"x\": " + std::to_string(p->pos.x) +
                    ", \"y\": " + std::to_string(p->pos.y) + "}";
            first = false;
        }
    } else {
        for (auto& [id, player] : activeRoster) {
            if (!first) json += ", ";
            json += "{\"id\": " + std::to_string(id) +
                    ", \"name\": \"" + player->name + "\"" +
                    ", \"x\": " + std::to_string(player->pos.x) +
                    ", \"y\": " + std::to_string(player->pos.y) + "}";
            first = false;
        }
    }
    json += "], ";

    // Bots (away team)
    json += "\"bots\": [";
    first = true;
    for (auto& p : court.GetAwayTeam()) {
        if (!first) json += ", ";
        json += "{\"id\": " + std::to_string(p->id) +
                ", \"name\": \"" + p->name + "\"" +
                ", \"x\": " + std::to_string(p->pos.x) +
                ", \"y\": " + std::to_string(p->pos.y) + "}";
        first = false;
    }
    json += "], ";

    // Ball (includes z for arc rendering, possession info)
    json += "\"ball\": {\"x\": " + std::to_string(court.ball.position.x) +
            ", \"y\": " + std::to_string(court.ball.position.y) +
            ", \"z\": " + std::to_string(court.ball.position.z) +
            ", \"isPossessed\": " + (court.ball.isPossessed ? "true" : "false") +
            ", \"possessorId\": " + std::to_string(court.ball.possessorId) + "}, ";

    // Scores
    json += "\"homeScore\": " + std::to_string(court.homeScore) +
            ", \"awayScore\": " + std::to_string(court.awayScore) +
            ", \"lastScorerId\": " + std::to_string(court.lastScorerId) + " }";

    return json;
}

std::vector<std::shared_ptr<PlayerEntity>> GameManager::GetActiveFloorPlayers() {
    std::vector<std::shared_ptr<PlayerEntity>> players;
    players.reserve(activeRoster.size());
    for (auto& [id, player] : activeRoster) {
        players.push_back(player);
    }
    return players;
}

void GameManager::SpawnBotOpponents() {
    // Five bot opponents modeled on a depth-first roster (OKC archetype).
    // All five have overall rating (avg of shooting/speed/defense) >= 55,
    // which triggers the Balanced Roster synergy for the bot team.
    struct BotDef { int id; const char* name; float startX; float startY; float speed; float shooting; float defense; };
    static const BotDef bots[] = {
        {901, "Bot PG", 620.0f,  60.0f, 68.0f, 58.0f, 52.0f},
        {902, "Bot SG", 660.0f, 140.0f, 60.0f, 62.0f, 55.0f},
        {903, "Bot SF", 640.0f, 200.0f, 58.0f, 55.0f, 60.0f},
        {904, "Bot PF", 600.0f, 280.0f, 55.0f, 50.0f, 65.0f},
        {905, "Bot C",  620.0f, 340.0f, 45.0f, 48.0f, 70.0f},
    };
    for (auto& def : bots) {
        auto bot = std::make_shared<PlayerEntity>(def.id, def.name, def.speed, def.shooting);
        bot->stats.defense = def.defense;
        bot->pos = {def.startX, def.startY};
        court.AddPlayer(bot, /*isHome=*/false);
    }
}

// The planning board is the home team's attacking half-court, drawn with the
// hoop at the bottom: grid x (0-4) runs sideline to sideline, grid y (0-4)
// runs from half court (0) to the baseline (4). Home attacks the right hoop
// (x=770). towardHoop nudges a position closer to the baseline.
Vector2D GameManager::PlanningToCourt(float gridX, float gridY, float towardHoop) {
    float depth = 40.0f + (4.0f - gridY) * 75.0f - towardHoop; // px out from the hoop
    return {770.0f - depth, 40.0f + gridX * 80.0f};
}

void GameManager::StartRound() {
    court.Clear();

    synergyEngine.AnalyzeRoster(GetActiveFloorPlayers());
    auto buffs = synergyEngine.GetActiveBuffs();

    for (auto& [id, player] : activeRoster) {
        for (const auto& buff : buffs) {
            player->stats.speed    += buff.speedBuff;
            player->stats.shooting += buff.shootingBuff;
            player->stats.defense  += buff.defenseBuff;
            player->ClampStats();
        }

        player->pos = PlanningToCourt(player->offensivePlacement.x, player->offensivePlacement.y, 0.0f);

        court.AddPlayer(player, /*isHome=*/true);
    }

    if (!pendingAwayTeam.empty()) {
        // Ghost board loaded via SetAwayTeamJSON: the away team opens on
        // defense, each ghost a step toward the hoop from its planning cell.
        for (const auto& def : pendingAwayTeam) {
            auto ghost = std::make_shared<PlayerEntity>(def.id, def.name, def.speed, def.shooting);
            ghost->stats.defense = def.defense;
            ghost->isBenchHero   = def.isBenchHero;
            ghost->ppgMean       = def.ppgMean;
            ghost->ppgStddev     = def.ppgStddev;
            ghost->ClampStats();
            ghost->pos = PlanningToCourt(def.gridX, def.gridY, 25.0f);
            court.AddPlayer(ghost, /*isHome=*/false);
        }
    } else {
        SpawnBotOpponents();
    }
    court.InitPossession();
}

void GameManager::LoadRosterJSON(std::string jsonData) {
    using json = nlohmann::json;
    try {
        auto roster = json::parse(jsonData);
        for (auto& entry : roster) {
            int         id       = entry.at("id").get<int>();
            std::string name     = entry.at("name").get<std::string>();
            int         cost     = entry.at("cost").get<int>();
            auto&       s        = entry.at("stats");
            float       shooting = s.at("shooting").get<float>();
            float       speed    = s.at("speed").get<float>();
            float       defense  = s.at("defense").get<float>();

            auto player = std::make_shared<PlayerEntity>(id, name, speed, shooting);
            player->cost          = cost;
            player->stats.defense = defense;
            if (s.contains("rebounding")) player->stats.rebounding = s["rebounding"].get<float>();
            if (s.contains("playmaking")) player->stats.playmaking = s["playmaking"].get<float>();
            player->ClampStats();

            if (entry.contains("team"))
                player->team = entry["team"].get<std::string>();
            if (entry.contains("injury_status"))
                player->injuryStatus = entry["injury_status"].get<std::string>();
            if (entry.contains("is_active"))
                player->isActive = entry["is_active"].get<bool>();

            // Bench-hero escalation tags (Findings #18 + #44)
            if (entry.contains("role"))
                player->isBenchHero =
                    (entry["role"].get<std::string>() == "bench_hero");
            if (entry.contains("ppg_mean"))
                player->ppgMean = entry["ppg_mean"].get<float>();
            if (entry.contains("ppg_stddev"))
                player->ppgStddev = entry["ppg_stddev"].get<float>();

            // Skip injured/inactive players — they can't be drafted
            if (!player->isActive) {
                std::cout << "Engine: Skipped inactive player " << name << " (" << player->injuryStatus << ")\n";
                continue;
            }

            activeRoster[id] = player;
        }
        std::cout << "Engine: Loaded " << activeRoster.size() << " players from roster JSON\n";
    } catch (const json::exception& e) {
        std::cerr << "Engine: Failed to parse roster JSON: " << e.what() << "\n";
    }
}

void GameManager::SetSeriesContext(int seriesState, int seriesTeam, int missingStars) {
    court.seriesState  = static_cast<Court::SeriesState>(seriesState);
    court.seriesTeam   = seriesTeam;
    court.missingStars = missingStars;
}

void GameManager::SetPlayerCoordinates(int playerId, float offX, float offY, float defX, float defY) {
    auto it = activeRoster.find(playerId);
    if (it != activeRoster.end()) {
        it->second->SetPlayCoordinates(offX, offY, defX, defY);
    }
}

void GameManager::RemovePlayer(int playerId) {
    activeRoster.erase(playerId);
}

void GameManager::SetHomeCourtBonus(float shootingBonus, float speedBonus) {
    court.SetHomeCourtBonus(shootingBonus, speedBonus);
}

std::string GameManager::SimulateGame(uint32_t seed, int ticks) {
    court.Clear();
    court.Reseed(seed);

    synergyEngine.AnalyzeRoster(GetActiveFloorPlayers());
    auto buffs = synergyEngine.GetActiveBuffs();

    for (auto& [id, player] : activeRoster) {
        for (const auto& buff : buffs) {
            player->stats.speed    += buff.speedBuff;
            player->stats.shooting += buff.shootingBuff;
            player->stats.defense  += buff.defenseBuff;
            player->ClampStats();
        }
        float simX = player->offensivePlacement.x * 70.0f + 40.0f;
        float simY = player->offensivePlacement.y * 70.0f + 40.0f;
        player->pos = {simX, simY};
        court.AddPlayer(player, /*isHome=*/true);
    }

    SpawnBotOpponents();
    court.InitPossession();

    float dt = 1.0f / 30.0f;
    for (int i = 0; i < ticks; i++) {
        court.UpdateSimulationStep(dt);
    }

    // Build result JSON
    std::string json = "{";
    json += "\"homeScore\": " + std::to_string(court.homeScore);
    json += ", \"awayScore\": " + std::to_string(court.awayScore);
    json += ", \"simTicks\": " + std::to_string(ticks);

    // Include synergy info
    json += ", \"synergies\": [";
    bool first = true;
    for (const auto& buff : buffs) {
        if (!first) json += ", ";
        json += "{\"name\": \"" + buff.name + "\"";
        json += ", \"tier\": " + std::to_string(buff.tier);
        json += ", \"shootingBuff\": " + std::to_string(buff.shootingBuff);
        json += ", \"defenseBuff\": " + std::to_string(buff.defenseBuff);
        json += ", \"speedBuff\": " + std::to_string(buff.speedBuff) + "}";
        first = false;
    }
    json += "]}";

    return json;
}
