#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "PlayerEntity.h"
#include "SynergyEngine.h"
#include "Court.h"

class GameManager {
public:
    GameManager() = default;

    void SpawnPlayer(int id, std::string name, float speed, float shooting);
    // Full-stat spawn used by the autochess client: includes team (for
    // franchise synergies) and defense so home units carry real stats.
    void SpawnUnit(int id, std::string name, std::string team,
                   float speed, float shooting, float defense);
    // Load the ghost opponent board (JSON array of units, or an object with a
    // "units" array). Replaces the hardcoded bot opponents on the next
    // StartRound(). Pass "[]" / bad JSON to fall back to default bots.
    void SetAwayTeamJSON(std::string jsonData);
    void SetPlayerPlay(int playerId, int actionTypeInt, float targetX, float targetY);
    void TickSimulation(float deltaTime);
    std::string GetGameStateJSON();
    void StartRound();
    void LoadRosterJSON(std::string jsonData);
    void SetPlayerCoordinates(int playerId, float offX, float offY, float defX, float defY);
    void RemovePlayer(int playerId);

    // seriesState: 0=NORMAL, 1=ELIMINATION_GAME, 2=CLOSEOUT_GAME, 3=GAME_7
    // seriesTeam:  0=home, 1=away, -1=none. missingStars: count of OUT star cards.
    void SetSeriesContext(int seriesState, int seriesTeam, int missingStars);

    // Run a full game simulation. Returns JSON: {homeScore, awayScore, simTicks, synergies}
    // seed: RNG seed for determinism. ticks: number of simulation steps to run.
    std::string SimulateGame(uint32_t seed, int ticks);

    // Apply a home court advantage: boosts shooting and speed for home team players.
    void SetHomeCourtBonus(float shootingBonus, float speedBonus);

private:
    struct AwayUnitDef {
        int id = 0;
        std::string name = "Ghost";
        float speed = 45.0f;
        float shooting = 45.0f;
        float defense = 45.0f;
        float gridX = 2.0f; // planning-grid coords (0-4), mirrored to right half
        float gridY = 2.0f;
    };

    std::map<int, std::shared_ptr<PlayerEntity>> activeRoster;
    std::vector<AwayUnitDef> pendingAwayTeam;
    SynergyEngine synergyEngine;
    Court court;

    std::vector<std::shared_ptr<PlayerEntity>> GetActiveFloorPlayers();
    void SpawnBotOpponents();
};
