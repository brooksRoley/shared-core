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
// Release height of a shot (the client lifts the ball 3px per z)
static const float ARC_Z0       = 5.0f;
// Ball flight. Shots reach the rim (RIM_Z, ~24 px of client lift) in a hang
// time that grows with distance; a miss caroms off the rim into a rebound.
// Passes travel at PASS_SPEED with a low arc.
static const float RIM_Z            = 8.0f;
static const float SHOT_HANG_BASE   = 0.35f;   // s
static const float SHOT_HANG_PER_PX = 1.0f / 800.0f;
static const float CAROM_VZ         = 7.0f;
static const float CAROM_SPREAD     = 50.0f;
static const float PASS_SPEED       = 700.0f;  // px/s
// After a catch the receiver attacks before he can pass again.
static const float CATCH_AND_GO     = 0.6f;    // s
static const float PASS_MIN_TIME    = 0.15f;   // s
static const float PASS_ARC_Z       = 1.5f;
// Movement. Players accelerate toward a desired velocity instead of snapping
// to full speed, slow down within ARRIVE_RADIUS of their target, and are kept
// MIN_SEPARATION apart (tokens are ~30 px across) so nobody stacks.
static const float ACCEL_PER_SPEED  = 8.0f;    // full speed in ~1/8 s
static const float ARRIVE_RADIUS    = 24.0f;
static const float MIN_SEPARATION   = 24.0f;
// The ball carrier has right of way: in a pair with him only the other
// player gives ground, so a drive moves at full speed. He keeps a slightly
// smaller gap than everyone else, about where his defender's guard spot
// already sits (30 px). Tighter, and the defender ends up pinned inside the
// shot-contest radius on every shot: at 10 px, accuracy fell from ~54% to ~45%.
static const float CARRIER_CONTACT  = 26.0f;
// A loose ball on the floor rolls to a stop instead of halting dead.
static const float ROLL_DAMPING     = 3.0f;    // 1/s

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
    shot = ShotFlight{};
    pass = PassFlight{};
    passCooldown = 0.0f;
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
    // Steering: accelerate toward a desired velocity (top speed 200 px/s
    // scaled by the speed stat), easing off inside ARRIVE_RADIUS so players
    // settle on their spot instead of snapping onto it.
    Vector2D dir      = target - p.pos;
    float    dist     = dir.Magnitude();
    float    maxSpeed = (p.stats.speed / 100.0f) * 200.0f;
    Vector2D desired  = dist < 2.0f
        ? Vector2D{0.0f, 0.0f}
        : dir.Normalize() * (maxSpeed * std::min(1.0f, dist / ARRIVE_RADIUS));
    Vector2D dv    = desired - p.velocity;
    float    maxDv = maxSpeed * ACCEL_PER_SPEED * dt;
    if (dv.Magnitude() > maxDv) dv = dv.Normalize() * maxDv;
    p.velocity = p.velocity + dv;
    p.pos = ClampToCourt(p.pos + p.velocity * dt);
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

    // The shot is decided now and resolved when the ball reaches the rim
    // (UpdateShotFlight): the ball travels a real arc, and the points land
    // with it. Hang time grows with distance.
    Vector2D from{shooter->pos.x, shooter->pos.y};
    float hang = SHOT_HANG_BASE + from.DistanceTo(targetHoop) * SHOT_HANG_PER_PX;
    const float g = Basketball::GRAVITY;
    shot = ShotFlight{made, isHomeTeam, points, shooter->id, hang, targetHoop};
    ball.isPossessed = false;
    ball.possessorId = -1;
    ball.flight      = Basketball::Flight::SHOT;
    ball.position    = {from.x, from.y, ARC_Z0};
    ball.velocity    = {
        (targetHoop.x - from.x) / hang,
        (targetHoop.y - from.y) / hang,
        (RIM_Z - ARC_Z0 + 0.5f * g * hang * hang) / hang,   // reaches RIM_Z at t = hang
    };
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
    StepPlay(dt);
    ResolveOverlaps();
    // A held ball rides with its carrier, after separation has moved him.
    if (ball.isPossessed) {
        if (auto carrier = FindPlayer(ball.possessorId)) ball.position = {carrier->pos.x, carrier->pos.y, 0.0f};
    }
}

void Court::StepPlay(float dt) {
    if (ball.flight == Basketball::Flight::SHOT) { UpdateShotFlight(dt); return; }
    if (ball.flight == Basketball::Flight::PASS) { UpdatePassFlight(dt); return; }

    // Loose ball: apply physics and try to assign rebound once ball lands
    if (!ball.isPossessed) {
        ball.UpdatePhysics(dt);
        Vector2D onFloor = ClampToCourt({ball.position.x, ball.position.y});
        ball.position.x = onFloor.x;
        ball.position.y = onFloor.y;
        if (ball.position.z <= 0.5f) {
            // On the floor it rolls to a stop while players go get it
            float roll = std::exp(-ROLL_DAMPING * dt);
            ball.velocity.x *= roll;
            ball.velocity.y *= roll;
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
    passCooldown -= dt;
    if (passCooldown <= 0.0f && nearestDef && carrier->pos.DistanceTo(nearestDef->pos) < 60.0f) {
        for (auto& tm : team) {
            if (tm->id == carrier->id) continue;
            auto tmDef = FindNearestDefender(tm, isHomeCarrier);
            float openness = tmDef ? tm->pos.DistanceTo(tmDef->pos) : 200.0f;
            // playmaking=50 (default) yields 0.225; scales linearly 0.0–0.45 over
            // [0,100]. Lower than when passes were instant: a pass now spends
            // real time in the air.
            float passProbability = (carrier->stats.playmaking / 100.0f) * 0.45f;
            if (openness > PASS_OPEN && roll(rng) < passProbability * dt) {
                // The pass travels (UpdatePassFlight) instead of teleporting.
                Vector2D from{carrier->pos.x, carrier->pos.y};
                pass = PassFlight{tm->id, carrier->id, isHomeCarrier, 0.0f,
                                  std::max(PASS_MIN_TIME, from.DistanceTo(tm->pos) / PASS_SPEED), from};
                ball.isPossessed = false;
                ball.possessorId = -1;
                ball.flight      = Basketball::Flight::PASS;
                ball.position    = {from.x, from.y, 0.0f};
                ball.velocity    = {0.0f, 0.0f, 0.0f};
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

    PositionTeams(team, opponents, carrier, targetHoop, dt);
}

// ── Positioning ──────────────────────────────────────────────────────────────

// The offense spreads to its spots around `focus` (the ball carrier, or the
// shooter / pass receiver while the ball is in the air) and the defense plays
// man-to-man, the on-ball defender on `focus`.
void Court::PositionTeams(std::vector<std::shared_ptr<PlayerEntity>>& team,
                          std::vector<std::shared_ptr<PlayerEntity>>& opponents,
                          const std::shared_ptr<PlayerEntity>& focus, Vector2D targetHoop, float dt) {
    const bool attackRight = targetHoop.x > COURT_W / 2.0f;   // home attacks the right hoop

    // ── Teammates spread to offensive spots ──────────────────────────────────
    for (size_t i = 0; i < team.size(); i++) {
        if (team[i]->id == focus->id) continue;
        auto slot = spotOf.find(team[i]->id);
        MovePlayerToward(*team[i], OffensiveSpot(slot != spotOf.end() ? slot->second : i, attackRight), dt);
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
        auto mark = marks[d] ? marks[d] : focus;
        float sag = (mark->id == focus->id) ? 30.0f : 70.0f;
        Vector2D toHoop = targetHoop - mark->pos;
        float    mag    = toHoop.Magnitude();
        Vector2D guardSpot = mag > 0
            ? mark->pos + toHoop.Normalize() * std::min(sag, mag)
            : mark->pos;
        MovePlayerToward(*opponents[d], guardSpot, dt);
    }
}

// ── Ball in flight ───────────────────────────────────────────────────────────

void Court::UpdateShotFlight(float dt) {
    ball.UpdatePhysics(dt);
    auto& team     = shot.home ? homeTeam : awayTeam;
    auto& defense  = shot.home ? awayTeam : homeTeam;
    if (auto shooter = FindPlayer(shot.shooterId)) PositionTeams(team, defense, shooter, shot.hoop, dt);

    shot.timeLeft -= dt;
    if (shot.timeLeft > 0.0f) return;

    ball.flight = Basketball::Flight::NONE;
    if (shot.made) {
        lastScorerId = shot.shooterId;
        if (shot.home) homeScore += shot.points;
        else           awayScore += shot.points;
        // Inbound: the defender nearest the basket takes it out, so the ball
        // goes to a player already there instead of jumping to mid-court.
        std::shared_ptr<PlayerEntity> inbounder;
        float best = std::numeric_limits<float>::max();
        for (auto& p : defense) {
            float d = p->pos.DistanceTo(shot.hoop);
            if (d < best) { best = d; inbounder = p; }
        }
        ball.velocity = {0.0f, 0.0f, 0.0f};
        // ...and outlets it to the teammate furthest up the floor, a pass in
        // flight like any other, instead of walking it the length of the court.
        Vector2D attackHoop = shot.home ? HOME_HOOP : AWAY_HOOP;   // the inbounding side attacks the other way
        std::shared_ptr<PlayerEntity> outlet;
        float closest = std::numeric_limits<float>::max();
        for (auto& p : defense) {
            if (p == inbounder) continue;
            float d = p->pos.DistanceTo(attackHoop);
            if (d < closest) { closest = d; outlet = p; }
        }
        if (inbounder && outlet) {
            Vector2D from{inbounder->pos.x, inbounder->pos.y};
            pass = PassFlight{outlet->id, inbounder->id, !shot.home, 0.0f,
                              std::max(PASS_MIN_TIME, from.DistanceTo(outlet->pos) / PASS_SPEED), from};
            ball.isPossessed = false;
            ball.possessorId = -1;
            ball.flight      = Basketball::Flight::PASS;
            ball.position    = {from.x, from.y, 0.0f};
        } else if (inbounder) {
            ball.isPossessed = true;
            ball.possessorId = inbounder->id;
            ball.position    = {inbounder->pos.x, inbounder->pos.y, 0.0f};
        } else {
            ball.isPossessed = false;
            ball.position    = {shot.hoop.x, shot.hoop.y, 0.0f};
        }
    } else {
        // Miss: off the rim on a short carom toward a contestable spot, sized
        // to its real hang time so it lands where it was aimed.
        std::uniform_real_distribution<float> spread(-CAROM_SPREAD, CAROM_SPREAD);
        Vector2D land = ClampToCourt({shot.hoop.x + spread(rng), shot.hoop.y + spread(rng)});
        const float g = Basketball::GRAVITY;
        float t = (CAROM_VZ + std::sqrt(CAROM_VZ * CAROM_VZ + 2.0f * g * RIM_Z)) / g;
        ball.position = {shot.hoop.x, shot.hoop.y, RIM_Z};
        ball.velocity = {(land.x - shot.hoop.x) / t, (land.y - shot.hoop.y) / t, CAROM_VZ};
    }
}

void Court::UpdatePassFlight(float dt) {
    auto receiver = FindPlayer(pass.receiverId);
    if (!receiver) {   // he left the floor: it's a loose ball
        ball.flight = Basketball::Flight::NONE;
        return;
    }
    auto& team    = pass.home ? homeTeam : awayTeam;
    auto& defense = pass.home ? awayTeam : homeTeam;
    // The defense reacts to the catch, not the throw: while the ball is in the
    // air it keeps guarding the passer, so a pass to an open man arrives open.
    // (Closing out during the flight made every catch pressured, and pressure
    // triggers the next pass: 11 passes and 3 shots a game.)
    auto passer = FindPlayer(pass.passerId);
    PositionTeams(team, defense, passer ? passer : receiver, pass.home ? AWAY_HOOP : HOME_HOOP, dt);

    pass.t += dt;
    float f = std::min(1.0f, pass.t / pass.duration);
    Vector2D at = pass.from + (receiver->pos - pass.from) * f;   // chases a moving receiver
    ball.position = {at.x, at.y, 4.0f * PASS_ARC_Z * f * (1.0f - f)};
    if (f >= 1.0f) {
        ball.flight      = Basketball::Flight::NONE;
        ball.isPossessed = true;
        ball.possessorId = receiver->id;
        ball.position    = {receiver->pos.x, receiver->pos.y, 0.0f};
        passCooldown     = CATCH_AND_GO;
    }
}

// ── Separation ───────────────────────────────────────────────────────────────

// Nobody stands inside anybody: overlapping players are pushed apart along
// the line between them, half each. Two passes settle small clusters. The
// ball carrier keeps only CARRIER_CONTACT and is never pushed himself: the
// player in his way gives ground, so a drive moves at full speed.
void Court::ResolveOverlaps() {
    const int carrierId = ball.isPossessed ? ball.possessorId : -1;
    std::vector<PlayerEntity*> all;
    for (auto& p : homeTeam) all.push_back(p.get());
    for (auto& p : awayTeam) all.push_back(p.get());
    for (int iter = 0; iter < 2; iter++) {
        for (size_t i = 0; i < all.size(); i++) {
            for (size_t j = i + 1; j < all.size(); j++) {
                Vector2D d    = all[j]->pos - all[i]->pos;
                float    dist = d.Magnitude();
                float    gap  = (all[i]->id == carrierId || all[j]->id == carrierId) ? CARRIER_CONTACT : MIN_SEPARATION;
                if (dist >= gap) continue;
                Vector2D n       = dist > 0.001f ? d * (1.0f / dist) : Vector2D{1.0f, 0.0f};
                float    overlap = gap - dist;
                if (all[i]->id == carrierId)      all[j]->pos = ClampToCourt(all[j]->pos + n * overlap);
                else if (all[j]->id == carrierId) all[i]->pos = ClampToCourt(all[i]->pos - n * overlap);
                else {
                    all[i]->pos = ClampToCourt(all[i]->pos - n * (overlap * 0.5f));
                    all[j]->pos = ClampToCourt(all[j]->pos + n * (overlap * 0.5f));
                }
            }
        }
    }
}

std::shared_ptr<PlayerEntity> Court::FindPlayer(int id, bool* isHome) {
    for (auto& p : homeTeam) if (p->id == id) { if (isHome) *isHome = true;  return p; }
    for (auto& p : awayTeam) if (p->id == id) { if (isHome) *isHome = false; return p; }
    return nullptr;
}
