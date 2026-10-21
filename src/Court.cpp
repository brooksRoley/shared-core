#include "Court.h"
#include "ShotProbability.h"
#include <cmath>
#include <algorithm>
#include <limits>

// Pixel-space court: 800 x 400. Hoops centred at x=30 and x=770, mid-height.
static const Vector2D HOME_HOOP{30.0f,  200.0f};
static const Vector2D AWAY_HOOP{770.0f, 200.0f};

// Distance from hoop at which a driving player will attempt a shot (~7.8 ft,
// inside the paint zone of ShotProbability)
static const float SHOT_RANGE   = 64.0f;
// A teammate this far from his defender is open for a kick-out pass
static const float PASS_OPEN    = 60.0f;
// Distance at which a player picks up a loose ball
static const float PICKUP_RANGE = 30.0f;
// Pixel-to-feet ratio so ShotProbability (designed in feet) stays accurate
static const float PX_PER_FT    = 8.25f;
// Court bounds — nothing (player or ball) may leave the floor
static const float COURT_W      = 800.0f;
static const float COURT_H      = 400.0f;
static const float EDGE         = 10.0f;
// Beyond this distance a shot is a three (~24 ft); open catch-and-shoot
// threes are taken out to THREE_MAX when no defender is within THREE_OPEN.
static const float THREE_LINE   = 200.0f;
static const float THREE_MAX    = 250.0f;
static const float THREE_OPEN   = 50.0f;
// Miss arc: ~1.1s hang time, ~7.4 apex (the client lifts the ball 3px per z)
static const float ARC_Z0       = 5.0f;
static const float ARC_VZ       = 12.0f;

static Vector2D ClampToCourt(Vector2D v) {
    return {std::clamp(v.x, EDGE, COURT_W - EDGE), std::clamp(v.y, EDGE, COURT_H - EDGE)};
}

// Five floor spots, as {distance out from the attacked hoop's baseline side,
// y}: two corners, two wings, top of the key — all just outside THREE_LINE.
// Mirrored for the team attacking the left hoop.
static Vector2D OffensiveSpot(size_t slot, bool attackRight) {
    static const Vector2D spots[5] = {
        {95.0f, 20.0f}, {185.0f, 95.0f}, {215.0f, 200.0f}, {185.0f, 305.0f}, {95.0f, 380.0f},
    };
    const Vector2D& s = spots[slot % 5];
    return {attackRight ? AWAY_HOOP.x - s.x : HOME_HOOP.x + s.x, s.y};
}

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
    lastScorerId = -1;
    spotOf.clear();
}

void Court::AssignSpots() {
    // Greedy closest pairs between each team's opening positions and the five
    // floor spots, so the planning board decides who plays corner, wing, top.
    // The away team starts on the right half but attacks left, so its
    // positions are mirrored before matching.
    spotOf.clear();
    auto assign = [this](const std::vector<std::shared_ptr<PlayerEntity>>& team, bool mirror) {
        std::vector<bool> slotTaken(5, false);
        std::vector<bool> placed(team.size(), false);
        for (size_t n = 0; n < std::min<size_t>(team.size(), 5); n++) {
            float best = std::numeric_limits<float>::max();
            size_t bp = 0, bs = 0;
            for (size_t p = 0; p < team.size(); p++) {
                if (placed[p]) continue;
                Vector2D pos = team[p]->pos;
                if (mirror) pos.x = COURT_W - pos.x;
                for (size_t s = 0; s < 5; s++) {
                    if (slotTaken[s]) continue;
                    float d = pos.DistanceTo(OffensiveSpot(s, true));
                    if (d < best) { best = d; bp = p; bs = s; }
                }
            }
            placed[bp] = true;
            slotTaken[bs] = true;
            spotOf[team[bp]->id] = bs;
        }
    };
    assign(homeTeam, false);
    assign(awayTeam, true);
}

void Court::InitPossession() {
    AssignSpots();
    if (homeTeam.empty()) return;
    // Jump ball: a coin flip decides the opening possession (the home team
    // used to always get it, a free ~10-point win-rate edge). The ball goes
    // to the winning side's best shooter.
    std::uniform_real_distribution<float> tip(0.0f, 1.0f);
    auto& side = (awayTeam.empty() || tip(rng) < 0.5f) ? homeTeam : awayTeam;
    auto best = std::max_element(side.begin(), side.end(),
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
    p.pos = ClampToCourt(p.pos + dir.Normalize() * std::min(step, dist));
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

    // Compute board-wide average defense for the defending team (used by damage cap)
    auto& defenders = isHomeTeam ? awayTeam : homeTeam;
    float boardAvgDef = 50.0f;
    if (!defenders.empty()) {
        float totalDef = 0.0f;
        for (auto& d : defenders) totalDef += d->stats.defense;
        boardAvgDef = totalDef / static_cast<float>(defenders.size());
    }

    // ShotProbability formula was designed for feet; scale pixel positions down
    float prob;
    if (defender) {
        PlayerEntity scaledS = *shooter;
        PlayerEntity scaledD = *defender;
        scaledS.pos = {shooter->pos.x / PX_PER_FT, shooter->pos.y / PX_PER_FT};
        scaledD.pos = {defender->pos.x / PX_PER_FT, defender->pos.y / PX_PER_FT};
        Vector2D hoopFt{targetHoop.x / PX_PER_FT, targetHoop.y / PX_PER_FT};
        prob = CalculateShotProbability(&scaledS, &scaledD, hoopFt, boardAvgDef);
    } else {
        prob = (shooter->stats.shooting / 100.0f) * 0.5f;
    }

    // 3-pointer if shot is taken from beyond 200px of the hoop (~24 ft)
    float distToHoop = shooter->pos.DistanceTo(targetHoop);
    int   points     = distToHoop > THREE_LINE ? 3 : 2;

    // ── Series-state team multiplier (Finding #17) ──────────────────────────
    if (seriesTeam == (isHomeTeam ? 0 : 1)) {
        if      (seriesState == SeriesState::ELIMINATION_GAME) prob *= 1.03f;
        else if (seriesState == SeriesState::CLOSEOUT_GAME)    prob *= 0.98f;
    }

    // ── Dual Bench Hero Escalation (Findings #18 + #44) ─────────────────────
    if (shooter->isBenchHero) {
        float benchHeroProb = (missingStars >= 2) ? 0.30f
                            : (missingStars == 1) ? 0.15f
                                                  : 0.05f;
        std::uniform_real_distribution<float> heroGate(0.0f, 1.0f);
        if (heroGate(rng) < benchHeroProb) {
            float m   = shooter->ppgMean   > 0.0f ? shooter->ppgMean   : 10.0f;
            float sd  = shooter->ppgStddev > 0.0f ? shooter->ppgStddev : 5.0f;
            std::normal_distribution<float> ppgDist(m, sd);
            float sample     = std::max(0.0f, ppgDist(rng));
            float heroFactor = std::clamp(1.4f * (sample / m), 1.0f, 2.5f);
            prob *= heroFactor;
        }
    }
    prob = std::clamp(prob, 0.0f, 1.0f);

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    bool made = roll(rng) < prob;

    if (made) {
        lastScorerId = shooter->id;
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
        // Miss: launch ball on arc toward the hoop area for a rebound. The
        // landing spot stays on the floor, and horizontal velocity is sized to
        // the arc's real hang time so the ball lands where it was aimed.
        std::uniform_real_distribution<float> spread(-50.0f, 50.0f);
        ball.isPossessed = false;
        ball.possessorId = -1;
        Vector2D land = ClampToCourt({targetHoop.x + spread(rng), targetHoop.y + spread(rng)});
        const float g = Basketball::GRAVITY;
        float flightTime = (ARC_VZ + std::sqrt(ARC_VZ * ARC_VZ + 2.0f * g * ARC_Z0)) / g;
        ball.position = {shooter->pos.x, shooter->pos.y, ARC_Z0};
        ball.velocity = {
            (land.x - shooter->pos.x) / flightTime,
            (land.y - shooter->pos.y) / flightTime,
            ARC_VZ  // upward arc
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
            // Higher rebounding stat further reduces effective distance
            float reboundAdj = heightAdj - (p->stats.rebounding - 50.0f) * 0.5f;
            if (reboundAdj < minAdj) { minAdj = reboundAdj; nearest = p; }
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
        Vector2D onFloor = ClampToCourt({ball.position.x, ball.position.y});
        ball.position.x = onFloor.x;
        ball.position.y = onFloor.y;
        if (ball.position.z <= 0.5f) {
            // Ball is down: it stops rolling so the rebound is contestable
            ball.velocity.x = 0.0f;
            ball.velocity.y = 0.0f;
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
    if (!carrier) {
        // Possessor isn't on the floor (e.g. removed between rounds): make it
        // a loose ball so somebody picks it up instead of the sim stalling.
        ball.isPossessed = false;
        ball.possessorId = -1;
        return;
    }

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
            // playmaking=50 (default) yields 0.3; scales linearly 0.0–0.6 over [0,100]
            float passProbability = (carrier->stats.playmaking / 100.0f) * 0.6f;
            if (openness > PASS_OPEN && roll(rng) < passProbability * dt) {
                ball.possessorId = tm->id;
                ball.position = {tm->pos.x, tm->pos.y, 0.0f};
                return;
            }
        }
    }

    // ── Ball carrier drives to basket ────────────────────────────────────────
    MovePlayerToward(*carrier, targetHoop, dt);
    ball.position = {carrier->pos.x, carrier->pos.y, 0.0f};

    float carrierDist = carrier->pos.DistanceTo(targetHoop);
    if (carrierDist < SHOT_RANGE) {
        AttemptShot(carrier, isHomeCarrier);
        return;
    }
    // Open catch-and-shoot three: spacing pays off when nobody closes out
    if (carrierDist > THREE_LINE && carrierDist < THREE_MAX &&
        (!nearestDef || carrier->pos.DistanceTo(nearestDef->pos) > THREE_OPEN) &&
        roll(rng) < (carrier->stats.shooting / 100.0f) * 5.0f * dt) {
        AttemptShot(carrier, isHomeCarrier);
        return;
    }

    // ── Teammates spread to offensive spots ──────────────────────────────────
    for (size_t i = 0; i < team.size(); i++) {
        if (team[i]->id == carrier->id) continue;
        auto slot = spotOf.find(team[i]->id);
        MovePlayerToward(*team[i], OffensiveSpot(slot != spotOf.end() ? slot->second : i, isHomeCarrier), dt);
    }

    // ── Opponents defend man-to-man ──────────────────────────────────────────
    // Each defender takes a unique attacker (greedy closest pairs) so the
    // whole team can't collapse on the ball. The on-ball defender sits 30px
    // toward the hoop; off-ball defenders sag further, into help position.
    // Extra defenders (bigger lineup than the offense) double the ball.
    std::vector<std::shared_ptr<PlayerEntity>> marks(opponents.size());
    std::vector<bool> attackerTaken(team.size(), false);
    for (size_t n = 0; n < std::min(opponents.size(), team.size()); n++) {
        float best = std::numeric_limits<float>::max();
        size_t bd = 0, ba = 0;
        for (size_t d = 0; d < opponents.size(); d++) {
            if (marks[d]) continue;
            for (size_t a = 0; a < team.size(); a++) {
                if (attackerTaken[a]) continue;
                float dist = opponents[d]->pos.DistanceTo(team[a]->pos);
                if (dist < best) { best = dist; bd = d; ba = a; }
            }
        }
        marks[bd] = team[ba];
        attackerTaken[ba] = true;
    }
    for (size_t d = 0; d < opponents.size(); d++) {
        auto mark = marks[d] ? marks[d] : carrier;
        float sag = (mark->id == carrier->id) ? 30.0f : 70.0f;
        Vector2D toHoop = targetHoop - mark->pos;
        float    mag    = toHoop.Magnitude();
        Vector2D guardSpot = mag > 0
            ? mark->pos + toHoop.Normalize() * std::min(sag, mag)
            : mark->pos;
        MovePlayerToward(*opponents[d], guardSpot, dt);
    }
}
