#include "Court.h"
#include "ShotProbability.h"
#include <cmath>
#include <algorithm>
#include <limits>

// Pixel-space court: 800 x 400. Hoops centred at x=30 and x=770, mid-height.
static const Vector2D HOME_HOOP{30.0f,  200.0f};
static const Vector2D AWAY_HOOP{770.0f, 200.0f};

// Distance from hoop at which a player will attempt a shot
static const float SHOT_RANGE   = 100.0f;
// Distance at which a player picks up a loose ball
static const float PICKUP_RANGE = 30.0f;
// Pixel-to-feet ratio so ShotProbability (designed in feet) stays accurate
static const float PX_PER_FT    = 8.25f;

// ── Helpers ──────────────────────────────────────────────────────────────────

void Court::AddPlayer(std::shared_ptr<PlayerEntity> p, bool isHome) {
    if (isHome) {
        if (homeShootingBonus != 0.0f || homeSpeedBonus != 0.0f) {
            p->stats.shooting += homeShootingBonus;
            p->stats.speed    += homeSpeedBonus;
            p->ClampStats();
        }
        homeTeam.push_back(p);
    } else {
        awayTeam.push_back(p);
    }
}

void Court::Clear() {
    homeTeam.clear();
    awayTeam.clear();
    ball = Basketball{};
    homeScore = 0;
    awayScore = 0;
}

void Court::InitPossession() {
    if (homeTeam.empty()) return;
    // Ball starts with the home player who has the best shooting
    auto best = std::max_element(homeTeam.begin(), homeTeam.end(),
        [](const auto& a, const auto& b) {
            return a->stats.shooting < b->stats.shooting;
        });
    ball.isPossessed  = true;
    ball.possessorId  = (*best)->id;
    ball.position     = {(*best)->pos.x, (*best)->pos.y, 0.0f};
}

void Court::MovePlayerToward(PlayerEntity& p, Vector2D target, float dt) {
    Vector2D dir  = target - p.pos;
    float    dist = dir.Magnitude();
    if (dist < 2.0f) return;
    // Max speed 200 px/s scaled by the player's speed stat
    float step = (p.stats.speed / 100.0f) * 200.0f * dt;
    p.pos = p.pos + dir.Normalize() * std::min(step, dist);
}

std::shared_ptr<PlayerEntity> Court::FindNearestDefender(
    const std::shared_ptr<PlayerEntity>& attacker, bool isHomeAttacker)
{
    auto& defenders = isHomeAttacker ? awayTeam : homeTeam;
    std::shared_ptr<PlayerEntity> nearest;
    float minDist = std::numeric_limits<float>::max();
    for (auto& d : defenders) {
        float dist = attacker->pos.DistanceTo(d->pos);
        if (dist < minDist) { minDist = dist; nearest = d; }
    }
    return nearest;
}

// ── Shot attempt ─────────────────────────────────────────────────────────────

void Court::AttemptShot(std::shared_ptr<PlayerEntity>& shooter, bool isHomeTeam) {
    Vector2D targetHoop = isHomeTeam ? AWAY_HOOP : HOME_HOOP;
    auto defender = FindNearestDefender(shooter, isHomeTeam);

    // ShotProbability formula was designed for feet; scale pixel positions down
    float prob;
    if (defender) {
        PlayerEntity scaledS = *shooter;
        PlayerEntity scaledD = *defender;
        scaledS.pos = {shooter->pos.x / PX_PER_FT, shooter->pos.y / PX_PER_FT};
        scaledD.pos = {defender->pos.x / PX_PER_FT, defender->pos.y / PX_PER_FT};
        Vector2D hoopFt{targetHoop.x / PX_PER_FT, targetHoop.y / PX_PER_FT};
        prob = CalculateShotProbability(&scaledS, &scaledD, hoopFt);
    } else {
        prob = (shooter->stats.shooting / 100.0f) * 0.5f;
    }

    // 3-pointer if shot is taken from beyond 200px of the hoop (~24 ft)
    float distToHoop = shooter->pos.DistanceTo(targetHoop);
    int   points     = distToHoop > 200.0f ? 3 : 2;

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    bool made = roll(rng) < prob;

    if (made) {
        if (isHomeTeam) homeScore += points;
        else             awayScore += points;
        // Hand ball to the other team at mid-court
        ball.position  = {400.0f, 200.0f, 0.0f};
        ball.velocity  = {0.0f,   0.0f,   0.0f};
        auto& nextTeam = isHomeTeam ? awayTeam : homeTeam;
        if (!nextTeam.empty()) {
            ball.isPossessed = true;
            ball.possessorId = nextTeam[0]->id;
        } else {
            ball.isPossessed = false;
        }
    } else {
        // Miss: launch ball on arc toward the hoop area for a rebound
        std::uniform_real_distribution<float> spread(-50.0f, 50.0f);
        ball.isPossessed = false;
        ball.possessorId = -1;
        float landX = targetHoop.x + spread(rng);
        float landY = targetHoop.y + spread(rng);
        float flightTime = 0.7f;
        ball.position = {shooter->pos.x, shooter->pos.y, 5.0f};
        ball.velocity = {
            (landX - shooter->pos.x) / flightTime,
            (landY - shooter->pos.y) / flightTime,
            8.0f  // upward arc
        };
    }
}

// ── Rebound ───────────────────────────────────────────────────────────────────

void Court::AssignRebound(float dt) {
    Vector2D ballPos{ball.position.x, ball.position.y};
    std::shared_ptr<PlayerEntity> nearest;
    float minAdj = std::numeric_limits<float>::max();

    auto check = [&](std::vector<std::shared_ptr<PlayerEntity>>& team) {
        for (auto& p : team) {
            float dist      = p->pos.DistanceTo(ballPos);
            // Taller players get a virtual distance bonus
            float heightAdj = dist - (p->stats.height_inches - 72) * 2.0f;
            if (heightAdj < minAdj) { minAdj = heightAdj; nearest = p; }
        }
    };
    check(homeTeam);
    check(awayTeam);

    if (!nearest) return;

    if (nearest->pos.DistanceTo(ballPos) < PICKUP_RANGE) {
        ball.isPossessed = true;
        ball.possessorId = nearest->id;
    } else {
        MovePlayerToward(*nearest, ballPos, dt);
    }
}

// ── Main sim step ─────────────────────────────────────────────────────────────

void Court::UpdateSimulationStep(float dt) {
    // Loose ball: apply physics and try to assign rebound once ball lands
    if (!ball.isPossessed) {
        ball.UpdatePhysics(dt);
        if (ball.position.z <= 0.5f) {
            AssignRebound(dt);
        }
        return;
    }

    // Identify the ball carrier and their team
    bool isHomeCarrier = false;
    std::shared_ptr<PlayerEntity> carrier;
    for (auto& p : homeTeam) {
        if (p->id == ball.possessorId) { carrier = p; isHomeCarrier = true; break; }
    }
    if (!carrier) {
        for (auto& p : awayTeam) {
            if (p->id == ball.possessorId) { carrier = p; isHomeCarrier = false; break; }
        }
    }
    if (!carrier) return;

    auto& team      = isHomeCarrier ? homeTeam : awayTeam;
    auto& opponents = isHomeCarrier ? awayTeam : homeTeam;
    Vector2D targetHoop = isHomeCarrier ? AWAY_HOOP : HOME_HOOP;

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);

    // ── Steal check (cooldown-based, frame-rate independent) ─────────────────
    stealCooldown -= dt;
    if (stealCooldown <= 0.0f) {
        stealCooldown = 2.0f;
        for (auto& def : opponents) {
            float dist = carrier->pos.DistanceTo(def->pos);
            if (dist < 40.0f) {
                float stealChance = (def->stats.defense / 100.0f) * 0.12f;
                float proximityBonus = ((40.0f - dist) / 40.0f) * 0.08f;
                if (roll(rng) < stealChance + proximityBonus) {
                    ball.possessorId = def->id;
                    stealCooldown = 2.0f;
                    return;
                }
            }
        }
    }

    // ── Pass check (under defensive pressure) ────────────────────────────────
    auto nearestDef = FindNearestDefender(carrier, isHomeCarrier);
    if (nearestDef && carrier->pos.DistanceTo(nearestDef->pos) < 60.0f) {
        for (auto& tm : team) {
            if (tm->id == carrier->id) continue;
            auto tmDef = FindNearestDefender(tm, isHomeCarrier);
            float openness = tmDef ? tm->pos.DistanceTo(tmDef->pos) : 200.0f;
            if (openness > 80.0f && roll(rng) < 0.3f * dt) {
                ball.possessorId = tm->id;
                ball.position = {tm->pos.x, tm->pos.y, 0.0f};
                return;
            }
        }
    }

    // ── Ball carrier drives to basket ────────────────────────────────────────
    MovePlayerToward(*carrier, targetHoop, dt);
    ball.position = {carrier->pos.x, carrier->pos.y, 0.0f};

    if (carrier->pos.DistanceTo(targetHoop) < SHOT_RANGE) {
        AttemptShot(carrier, isHomeCarrier);
        return;
    }

    // ── Teammates spread to offensive spots ──────────────────────────────────
    float offBaseX = isHomeCarrier ? 480.0f : 120.0f;
    for (size_t i = 0; i < team.size(); i++) {
        if (team[i]->id == carrier->id) continue;
        Vector2D spot{offBaseX + float(i % 3) * 80.0f, 80.0f + float(i) * 100.0f};
        MovePlayerToward(*team[i], spot, dt);
    }

    // ── Opponents defend: position between attacker and attacked basket ──────
    for (auto& def : opponents) {
        std::shared_ptr<PlayerEntity> mark;
        float minD = std::numeric_limits<float>::max();
        for (auto& att : team) {
            float d = def->pos.DistanceTo(att->pos);
            if (d < minD) { minD = d; mark = att; }
        }
        if (mark) {
            Vector2D toHoop = targetHoop - mark->pos;
            float    mag    = toHoop.Magnitude();
            Vector2D guardSpot = mag > 0
                ? mark->pos + toHoop.Normalize() * std::min(30.0f, mag)
                : mark->pos;
            MovePlayerToward(*def, guardSpot, dt);
        }
    }
}
