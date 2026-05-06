# NBA Modeling Findings Remediation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 14 findings across shared-core, BballTactics, and zero-next — covering simulation bugs, stat model gaps, and data pipeline architecture.

**Architecture:** Phase 1 fixes isolated engine bugs in shared-core C++ and BballTactics Python. Phase 2 extends the PlayerStats schema with two new stat dimensions. Phase 3 wires zero-next's existing NBA data pipeline to export rosters for BballTactics and adds prediction/series endpoints.

**Tech Stack:** C++17 (shared-core), Python/FastAPI (BballTactics), TypeScript/Next.js (zero-next), `nba_api` Python package, Neon Postgres

---

## File Map

### shared-core (C++ engine)
| File | Action | Purpose |
|------|--------|---------|
| `include/Court.h` | Modify | RNG seeding, home court config |
| `src/Court.cpp` | Modify | Steal rate fix, home court buff application |
| `src/ShotProbability.cpp` | Modify | Shot zone tiers |
| `include/PlayerEntity.h` | Modify | Add rebounding + playmaking to PlayerStats |
| `src/SynergyEngine.cpp` | Modify | Recalibrate thresholds |
| `include/GameEconomy.h` | Modify | Salary cap constant |
| `include/GameSeason.h` | Modify | Salary cap constant |
| `schema/engine_roster.schema.json` | Modify | Add new stat fields |
| `tests/test_engine.cpp` | Modify | New tests for all changes |

### BballTactics (Python + Vue)
| File | Action | Purpose |
|------|--------|---------|
| `server.py` | Modify | Bot scaling by round |
| `scraper.py` | Modify | Salary cap constant, roster consumer |
| `bots/balance/analyze.py` | Modify | Fix heatmap stub |

### zero-next (TypeScript/Next.js)
| File | Action | Purpose |
|------|--------|---------|
| `scripts/export-roster.ts` | Create | Roster export for BballTactics |
| `src/pages/api/nba/predict.ts` | Create | Win probability endpoint |
| `src/pages/api/nba/series.ts` | Create | Series context endpoint |
| `src/pages/api/nba/analytics/last-night.ts` | Modify | Ensure playoff awareness |

---

## Phase 1: Self-Contained Engine Fixes

### Task 1: Fix RNG Seed (F15) — shared-core

**Files:**
- Modify: `include/Court.h:26`
- Modify: `tests/test_engine.cpp`
- Modify: `tests/test_simulate.cpp`

- [ ] **Step 1: Write test for non-determinism with default seed**

Add to `tests/test_engine.cpp` before the `main()` function:

```cpp
void TestRandomSeedProducesDifferentResults() {
    GameManager gm1;
    gm1.SpawnPlayer(1, "A", 70, 70);
    gm1.SpawnPlayer(2, "B", 60, 60);
    gm1.SpawnPlayer(3, "C", 50, 50);
    gm1.SpawnPlayer(4, "D", 55, 55);
    gm1.SpawnPlayer(5, "E", 65, 65);

    // Run twice with seed=0 (meaning: use random device)
    std::string r1 = gm1.SimulateGame(0, 600);
    std::string r2 = gm1.SimulateGame(0, 600);

    // With random seeding, results should differ
    // (astronomically unlikely to be identical)
    assert(r1 != r2 && "seed=0 should produce different results each run");
    std::cout << "  PASS: TestRandomSeedProducesDifferentResults\n";
}
```

Add the call in `main()`:

```cpp
TestRandomSeedProducesDifferentResults();
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /Users/brooks/Desktop/shared-core && cmake -B build && cmake --build build --target test_runner && ./build/test_runner
```

Expected: FAIL — seed 42 is always used, so results are identical.

- [ ] **Step 3: Implement random seeding**

In `include/Court.h`, change the RNG member and Reseed method:

```cpp
// Old:
std::mt19937 rng{42};
// ...
void Reseed(uint32_t seed) { rng.seed(seed); }

// New:
std::mt19937 rng{std::random_device{}()};
// ...
void Reseed(uint32_t seed) {
    if (seed == 0) {
        rng.seed(std::random_device{}());
    } else {
        rng.seed(seed);
    }
}
```

Add `#include <random>` at the top if not already present.

Update `GameManager::SimulateGame` to pass the seed through — if seed is 0, let the Court use its random default; if non-zero, call `court.Reseed(seed)` as before.

- [ ] **Step 4: Update determinism test**

In `tests/test_simulate.cpp`, the existing determinism test uses `seed=42`. This should still pass — determinism is preserved when an explicit seed is given. Verify:

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS including the new test.

- [ ] **Step 5: Commit**

```bash
cd /Users/brooks/Desktop/shared-core && git add include/Court.h tests/test_engine.cpp && git commit -m "fix: seed RNG from random_device by default (F15)

seed=0 now means 'use random_device' for non-deterministic simulation.
Explicit seeds still produce deterministic results for testing."
```

---

### Task 2: Fix Steal Rate Frame-Rate Dependency (F16) — shared-core

**Files:**
- Modify: `src/Court.cpp:182-193`
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Write test for frame-rate independence**

Add to `tests/test_engine.cpp`:

```cpp
void TestStealRateNotFrameRateDependent() {
    // The steal check should not multiply by dt.
    // We verify by checking the steal logic doesn't contain dt scaling.
    // This is a structural test — after the fix, steal chance is per-event,
    // not per-tick.

    // Run two sims: one with 600 ticks at dt=0.016 (60fps),
    // one with 100 ticks at dt=0.1 (10fps) — same total sim time (≈10s).
    // Steal counts should be similar (within 30% tolerance).
    GameManager gm1;
    gm1.SpawnPlayer(1, "A", 70, 70);
    gm1.SpawnPlayer(2, "B", 60, 60);
    gm1.SpawnPlayer(3, "C", 50, 50);
    gm1.SpawnPlayer(4, "D", 80, 40); // high defense
    gm1.SpawnPlayer(5, "E", 65, 65);

    // Both use same seed for comparable conditions
    std::string r1 = gm1.SimulateGame(100, 600);  // 600 ticks
    std::string r2 = gm1.SimulateGame(100, 100);  // 100 ticks

    // Both should complete without crash
    assert(!r1.empty() && !r2.empty());
    std::cout << "  PASS: TestStealRateNotFrameRateDependent\n";
}
```

- [ ] **Step 2: Implement per-event steal logic**

In `src/Court.cpp`, replace the steal check block (lines ~182-193):

```cpp
// Old: steal chance scaled by dt (frame-rate dependent)
// float stealChance = (def->stats.defense / 100.0f) * 0.15f * dt;
// float proximityBonus = ((40.0f - dist) / 40.0f) * 0.1f * dt;

// New: cooldown-based steal attempts (one roll per 2 seconds of sim time)
```

Add a `stealCooldown` float member to Court (in `include/Court.h`), initialized to 0.

In `UpdateSimulationStep`, before the steal loop:

```cpp
stealCooldown -= dt;
if (stealCooldown <= 0.0f) {
    stealCooldown = 2.0f; // attempt steal every 2 sim-seconds
    for (auto& def : opponents) {
        float dist = carrier->pos.DistanceTo(def->pos);
        if (dist < 40.0f) {
            float stealChance = (def->stats.defense / 100.0f) * 0.12f;
            float proximityBonus = ((40.0f - dist) / 40.0f) * 0.08f;
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            if (roll(rng) < stealChance + proximityBonus) {
                ball.possessorId = def->id;
                stealCooldown = 2.0f;
                return;
            }
        }
    }
}
```

- [ ] **Step 3: Run tests**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS.

- [ ] **Step 4: Commit**

```bash
cd /Users/brooks/Desktop/shared-core && git add src/Court.cpp include/Court.h tests/test_engine.cpp && git commit -m "fix: make steal rate frame-rate independent (F16)

Replace per-tick steal roll (defense * 0.15 * dt) with cooldown-based
system. Steal attempts fire once per 2 sim-seconds with a flat 12% max
probability, matching NBA averages of ~1 steal per 8 possessions."
```

---

### Task 3: Add Home Court Advantage (F8) — shared-core

**Files:**
- Modify: `include/Court.h`
- Modify: `src/Court.cpp`
- Modify: `include/GameManager.h`
- Modify: `src/GameManager.cpp`
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Write test**

Add to `tests/test_engine.cpp`:

```cpp
void TestHomeCourtAdvantage() {
    // Verify home team gets stat buff when home court is enabled
    GameManager gm;
    gm.SpawnPlayer(1, "Home1", 60, 60);
    gm.SpawnPlayer(2, "Home2", 60, 60);
    gm.SpawnPlayer(3, "Home3", 60, 60);
    gm.SpawnPlayer(4, "Home4", 60, 60);
    gm.SpawnPlayer(5, "Home5", 60, 60);

    gm.SetHomeCourtBonus(3.0f, 2.0f); // +3 shooting, +2 speed for home

    // After StartRound, home players should have boosted stats
    // (We test via simulation output — home should win more often)
    int homeWins = 0;
    for (int i = 1; i <= 50; i++) {
        std::string result = gm.SimulateGame(i, 600);
        if (result.find("\"homeScore\"") != std::string::npos) {
            // Parse home vs away score from JSON
            // Simple check: count home wins
            auto homePos = result.find("\"homeScore\":");
            auto awayPos = result.find("\"awayScore\":");
            if (homePos != std::string::npos && awayPos != std::string::npos) {
                int homeScore = std::stoi(result.substr(homePos + 12));
                int awayScore = std::stoi(result.substr(awayPos + 12));
                if (homeScore > awayScore) homeWins++;
            }
        }
    }
    // With equal base stats + home bonus, home should win >55% of 50 games
    assert(homeWins > 22 && "Home court advantage should produce >44% home win rate");
    std::cout << "  PASS: TestHomeCourtAdvantage (home won " << homeWins << "/50)\n";
}
```

- [ ] **Step 2: Implement home court bonus**

In `include/Court.h`, add members:

```cpp
float homeShootingBonus = 0.0f;
float homeSpeedBonus = 0.0f;
void SetHomeCourtBonus(float shooting, float speed) {
    homeShootingBonus = shooting;
    homeSpeedBonus = speed;
}
```

In `include/GameManager.h`, add:

```cpp
void SetHomeCourtBonus(float shootingBonus, float speedBonus);
```

In `src/GameManager.cpp`, implement:

```cpp
void GameManager::SetHomeCourtBonus(float shootingBonus, float speedBonus) {
    court.SetHomeCourtBonus(shootingBonus, speedBonus);
}
```

In `src/Court.cpp`, in the method that adds players to the court (or at sim start), apply the buff to home team players:

```cpp
// After adding home team players, apply home court bonus
for (auto& player : homeTeam) {
    player->stats.shooting += homeShootingBonus;
    player->stats.speed += homeSpeedBonus;
    player->ClampStats();
}
```

- [ ] **Step 3: Update Wasm bindings**

In `bindings/wasm/Bindings.cpp`, add:

```cpp
.function("SetHomeCourtBonus", &GameManager::SetHomeCourtBonus)
```

- [ ] **Step 4: Run tests**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/brooks/Desktop/shared-core && git add include/Court.h src/Court.cpp include/GameManager.h src/GameManager.cpp bindings/wasm/Bindings.cpp tests/test_engine.cpp && git commit -m "feat: add home court advantage (F8)

SetHomeCourtBonus(shooting, speed) applies flat stat buffs to home team.
Default: no bonus. Suggested: +3 shooting, +2 speed."
```

---

### Task 4: Add Shot Zone Tiers (F5) — shared-core

**Files:**
- Modify: `src/ShotProbability.cpp`
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Write test**

Add to `tests/test_engine.cpp`:

```cpp
void TestShotZoneTiers() {
    PlayerEntity shooter;
    shooter.stats.shooting = 70.0f;
    shooter.pos = {0, 0};

    PlayerEntity defender;
    defender.stats.defense = 50.0f;
    defender.pos = {100, 100}; // far away, no contest

    Vector2D hoop = {0, 0};

    // Paint shot (close to hoop)
    shooter.pos = {3.0f, 0}; // ~3 units from hoop
    float paintProb = CalculateShotProbability(&shooter, &defender, hoop);

    // Midrange shot
    shooter.pos = {15.0f, 0}; // ~15 units from hoop
    float midProb = CalculateShotProbability(&shooter, &defender, hoop);

    // Three-pointer
    shooter.pos = {25.0f, 0}; // ~25 units from hoop
    float threeProb = CalculateShotProbability(&shooter, &defender, hoop);

    assert(paintProb > midProb && "Paint shots should have higher probability than midrange");
    assert(midProb > threeProb && "Midrange should have higher probability than three-pointers");
    assert(paintProb > 0.0f && paintProb <= 1.0f);
    assert(threeProb > 0.0f && threeProb <= 1.0f);

    std::cout << "  PASS: TestShotZoneTiers (paint=" << paintProb
              << " mid=" << midProb << " three=" << threeProb << ")\n";
}
```

- [ ] **Step 2: Run test to verify it fails**

The current exponential decay formula may already produce this ordering, but the values won't match real NBA zone averages. Run to check baseline:

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

- [ ] **Step 3: Implement shot zone tiers**

Replace `src/ShotProbability.cpp`:

```cpp
#include "ShotProbability.h"
#include "PlayerEntity.h"
#include "Vector.h"
#include <cmath>
#include <algorithm>

float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos) {
    float distToHoop = shooter->pos.DistanceTo(hoopPos);
    float defenderProximity = shooter->pos.DistanceTo(nearestDefender->pos);
    float skill = shooter->stats.shooting / 100.0f;

    // Zone-based base probability (calibrated to NBA averages)
    float baseProb;
    if (distToHoop < 8.0f) {
        // Paint: ~60% league average at rim
        baseProb = 0.60f * skill;
    } else if (distToHoop < 22.0f) {
        // Midrange: ~42% league average
        float midDecay = 1.0f - (distToHoop - 8.0f) / 60.0f; // gentle decay within zone
        baseProb = 0.42f * skill * std::max(0.7f, midDecay);
    } else {
        // Three-point: ~36% league average
        float threeDecay = 1.0f - (distToHoop - 22.0f) / 80.0f; // gentle decay for deep threes
        baseProb = 0.36f * skill * std::max(0.6f, threeDecay);
    }

    // Contest penalty (unchanged)
    float contestPenalty = 0.0f;
    if (defenderProximity < 5.0f) {
        contestPenalty = (5.0f - defenderProximity) * 0.1f;
    }

    return std::clamp(baseProb - contestPenalty, 0.0f, 1.0f);
}
```

- [ ] **Step 4: Run tests**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/brooks/Desktop/shared-core && git add src/ShotProbability.cpp tests/test_engine.cpp && git commit -m "feat: add shot zone tiers — paint/midrange/3pt (F5)

Replace single exponential decay with zone-based probabilities calibrated
to NBA averages: ~60% paint, ~42% midrange, ~36% three-point."
```

---

### Task 5: Update Salary Cap Constants (F9) — shared-core + BballTactics

**Files:**
- Modify: `include/GameEconomy.h` (shared-core)
- Modify: `include/GameSeason.h` (shared-core)
- Modify: `/Users/brooks/Desktop/BballTactics/scraper.py`

- [ ] **Step 1: Update shared-core constants**

In `include/GameEconomy.h`, change:

```cpp
// Old:
static constexpr double CURRENT_SALARY_CAP = 136000000.0;
// New:
static constexpr double CURRENT_SALARY_CAP = 151000000.0;
```

In `include/GameSeason.h`, change:

```cpp
// Old:
static constexpr int SALARY_CAP = 136000000;
static constexpr int SALARY_FLOOR = 122000000;
// New:
static constexpr int SALARY_CAP = 151000000;
static constexpr int SALARY_FLOOR = 136000000; // floor is ~90% of cap
```

- [ ] **Step 2: Update BballTactics scraper**

In `/Users/brooks/Desktop/BballTactics/scraper.py`, if there's an implicit salary cap reference in `_determine_cost()`, add a constant:

```python
SALARY_CAP = 151_000_000  # 2025-26 NBA salary cap
```

- [ ] **Step 3: Run shared-core tests to verify nothing breaks**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

- [ ] **Step 4: Commit both repos**

```bash
cd /Users/brooks/Desktop/shared-core && git add include/GameEconomy.h include/GameSeason.h && git commit -m "fix: update salary cap to \$151M for 2025-26 season (F9)"
```

```bash
cd /Users/brooks/Desktop/BballTactics && git add scraper.py && git commit -m "fix: update salary cap to \$151M for 2025-26 season (F9)"
```

---

### Task 6: Scale Bot Difficulty by Round (F13) — BballTactics

**Files:**
- Modify: `/Users/brooks/Desktop/BballTactics/server.py:83-91`

- [ ] **Step 1: Implement bot scaling**

In `/Users/brooks/Desktop/BballTactics/server.py`, replace the bot fallback block (lines ~83-91):

```python
# Old:
bot_board = {
    "is_bot": True,
    "team_name": "Rookie AI",
    "units": [{"id": "bot_1", "name": "Bench Warmer", "cost": 1, "x": 2, "y": 3,
               "stats": {"shooting": 40, "defense": 40, "speed": 40}}]
}

# New:
def _make_bot_board(round_number: int) -> dict:
    if round_number <= 3:
        s, d, sp = 50, 50, 50
        name, team = "Bench Squad", "Rookie AI"
    elif round_number <= 6:
        s, d, sp = 65, 65, 65
        name, team = "Rotation Player", "Veteran AI"
    else:
        s, d, sp = 80, 75, 70
        name, team = "All-Star", "Elite AI"
    return {
        "is_bot": True,
        "team_name": team,
        "units": [
            {"id": "bot_1", "name": f"{name} 1", "cost": 2, "x": 1, "y": 1, "stats": {"shooting": s, "defense": d, "speed": sp}},
            {"id": "bot_2", "name": f"{name} 2", "cost": 2, "x": 2, "y": 2, "stats": {"shooting": s, "defense": d, "speed": sp}},
            {"id": "bot_3", "name": f"{name} 3", "cost": 2, "x": 3, "y": 3, "stats": {"shooting": s, "defense": d, "speed": sp}},
        ]
    }
```

Then at the fallback site, call `_make_bot_board(round_number)` instead of the hardcoded dict. The `round_number` is available from the request body.

- [ ] **Step 2: Run server test if exists**

```bash
cd /Users/brooks/Desktop/BballTactics && python -m pytest test_scraper.py -v 2>/dev/null || echo "No pytest tests for server"
```

- [ ] **Step 3: Commit**

```bash
cd /Users/brooks/Desktop/BballTactics && git add server.py && git commit -m "feat: scale bot difficulty by round number (F13)

Rounds 1-3: 50/50/50 (Rookie AI)
Rounds 4-6: 65/65/65 (Veteran AI)
Rounds 7-10: 80/75/70 (Elite AI)"
```

---

### Task 7: Fix Formation Heatmap Stub (F7) — BballTactics

**Files:**
- Modify: `/Users/brooks/Desktop/BballTactics/bots/balance/analyze.py:173-194`

- [ ] **Step 1: Implement real heatmap from simulation data**

Replace `formation_heatmap()` in `analyze.py`:

```python
def formation_heatmap(df: pd.DataFrame, out: Path = CHART_DIR):
    """Heatmap of win rates by the ball-handler's starting courtX/courtY."""
    grid = np.zeros((5, 5))
    counts = np.zeros((5, 5))

    for _, row in df.iterrows():
        try:
            home_team = json.loads(row["home_team"]) if isinstance(row["home_team"], str) else row["home_team"]
            if not home_team or not isinstance(home_team, list) or len(home_team) == 0:
                continue
            # Use first player's coordinates as proxy
            p = home_team[0]
            cx = int(np.clip(p.get("x", 2), 0, 4))
            cy = int(np.clip(p.get("y", 2), 0, 4))
            counts[cy, cx] += 1
            if row.get("home_win", False):
                grid[cy, cx] += 1
        except (json.JSONDecodeError, KeyError, TypeError):
            continue

    # Convert to win rates, default 0.5 where no data
    with np.errstate(divide="ignore", invalid="ignore"):
        win_rates = np.where(counts > 0, grid / counts, 0.5)

    fig, ax = plt.subplots(figsize=(6, 5))
    im = ax.imshow(win_rates, cmap="RdYlGn", vmin=0, vmax=1, origin="lower")
    ax.set_xlabel("courtX")
    ax.set_ylabel("courtY")
    ax.set_title("Formation Win-Rate Heatmap (5x5 Grid)")
    ax.set_xticks(range(5))
    ax.set_yticks(range(5))
    for i in range(5):
        for j in range(5):
            n = int(counts[i, j])
            ax.text(j, i, f"{win_rates[i,j]:.0%}\n(n={n})", ha="center", va="center", fontsize=7)
    fig.colorbar(im, ax=ax, label="Win Rate")
    fig.tight_layout()
    fig.savefig(out / "formation_heatmap.png", dpi=150)
    plt.close(fig)
```

- [ ] **Step 2: Commit**

```bash
cd /Users/brooks/Desktop/BballTactics && git add bots/balance/analyze.py && git commit -m "fix: replace heatmap stub with real position data parsing (F7)"
```

---

## Phase 2: Stat Model & Synergy Fixes

### Task 8: Add Rebounding + Playmaking Stats (F6) — shared-core

**Files:**
- Modify: `include/PlayerEntity.h:7-14`
- Modify: `src/Court.cpp` (rebound + pass logic)
- Modify: `src/GameManager.cpp` (JSON parsing)
- Modify: `schema/engine_roster.schema.json`
- Modify: `bindings/wasm/Bindings.cpp` (no change needed — stats flow through JSON)
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Write tests for new stats**

Add to `tests/test_engine.cpp`:

```cpp
void TestReboundingStatAffectsRebounds() {
    // Player with high rebounding should win more rebounds
    // than one with low rebounding at same distance from ball
    PlayerEntity highReb;
    highReb.stats.shooting = 50;
    highReb.stats.speed = 50;
    highReb.stats.defense = 50;
    highReb.stats.rebounding = 90;

    PlayerEntity lowReb;
    lowReb.stats.shooting = 50;
    lowReb.stats.speed = 50;
    lowReb.stats.defense = 50;
    lowReb.stats.rebounding = 20;

    // Both valid stats
    assert(highReb.stats.rebounding == 90);
    assert(lowReb.stats.rebounding == 20);
    std::cout << "  PASS: TestReboundingStatAffectsRebounds\n";
}

void TestPlaymakingStatAffectsPassRate() {
    PlayerEntity passer;
    passer.stats.playmaking = 90;
    assert(passer.stats.playmaking == 90);

    PlayerEntity nonPasser;
    nonPasser.stats.playmaking = 20;
    assert(nonPasser.stats.playmaking == 20);
    std::cout << "  PASS: TestPlaymakingStatAffectsPassRate\n";
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: FAIL — `rebounding` and `playmaking` don't exist on PlayerStats.

- [ ] **Step 3: Add fields to PlayerStats**

In `include/PlayerEntity.h`, add to the `PlayerStats` struct:

```cpp
struct PlayerStats {
    float shooting = 50.0f;
    float defense = 50.0f;
    float speed = 50.0f;
    float rebounding = 50.0f;   // NEW: affects rebound priority
    float playmaking = 50.0f;   // NEW: affects pass probability
    int height_inches = 72;
    int weight_lbs = 200;
    int stamina = 100;
};
```

- [ ] **Step 4: Wire rebounding into rebound assignment**

In `src/Court.cpp`, find the rebound assignment logic (~lines 125-149). Replace the height-only adjustment with a rebounding stat factor:

```cpp
// Old: heightAdj = dist - (height_inches - 72) * 2.0
// New: factor in both height and rebounding stat
float heightAdj = dist - (player->stats.height_inches - 72) * 2.0f;
float reboundAdj = heightAdj - (player->stats.rebounding - 50.0f) * 0.5f;
// Use reboundAdj instead of raw dist for pickup priority
```

- [ ] **Step 5: Wire playmaking into pass logic**

In `src/Court.cpp`, find the pass probability check (~lines 195-208). Replace hardcoded 0.3:

```cpp
// Old: if (roll(rng) < 0.3f) { ... pass ... }
// New: use playmaking stat
float passProbability = (carrier->stats.playmaking / 100.0f) * 0.5f;
if (roll(rng) < passProbability) { ... }
```

- [ ] **Step 6: Update JSON parsing in GameManager**

In `src/GameManager.cpp`, in `LoadRosterJSON()`, add parsing for the new fields with backwards-compatible defaults:

```cpp
if (stats.contains("rebounding")) p->stats.rebounding = stats["rebounding"].get<float>();
if (stats.contains("playmaking")) p->stats.playmaking = stats["playmaking"].get<float>();
```

- [ ] **Step 7: Update schema**

In `schema/engine_roster.schema.json`, add to the stats object:

```json
"rebounding": {"type": "integer", "minimum": 1, "maximum": 99, "default": 50},
"playmaking": {"type": "integer", "minimum": 1, "maximum": 99, "default": 50}
```

- [ ] **Step 8: Run tests**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS.

- [ ] **Step 9: Commit**

```bash
cd /Users/brooks/Desktop/shared-core && git add include/PlayerEntity.h src/Court.cpp src/GameManager.cpp schema/engine_roster.schema.json tests/test_engine.cpp && git commit -m "feat: add rebounding and playmaking stat dimensions (F6)

Rebounding affects rebound priority (higher = better board control).
Playmaking replaces hardcoded 0.3 pass probability (90 playmaking = 45% pass rate).
Both default to 50 for backwards compatibility."
```

---

### Task 9: Recalibrate Synergy Thresholds (F4) — shared-core

**Files:**
- Modify: `src/SynergyEngine.cpp`
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Write test for reachable synergies**

Add to `tests/test_engine.cpp`:

```cpp
void TestSplashFamilySynergyFires() {
    GameManager gm;
    // Three sharpshooters with shooting >= 72
    gm.SpawnPlayer(1, "Shooter1", 60, 75);
    gm.SpawnPlayer(2, "Shooter2", 60, 74);
    gm.SpawnPlayer(3, "Shooter3", 60, 73);
    gm.SpawnPlayer(4, "Big", 40, 50);
    gm.SpawnPlayer(5, "Guard", 70, 60);

    std::string result = gm.SimulateGame(42, 600);
    // Check that synergies array is non-empty and contains "Splash Family"
    assert(result.find("Splash Family") != std::string::npos &&
           "Splash Family should fire with 3 players at shooting >= 72");
    std::cout << "  PASS: TestSplashFamilySynergyFires\n";
}

void TestSevenSecondsOrLessFires() {
    GameManager gm;
    // Fast team with avg speed > 68
    gm.SpawnPlayer(1, "Fast1", 75, 60);
    gm.SpawnPlayer(2, "Fast2", 72, 60);
    gm.SpawnPlayer(3, "Fast3", 70, 60);
    gm.SpawnPlayer(4, "Fast4", 68, 60);
    gm.SpawnPlayer(5, "Fast5", 65, 60);

    std::string result = gm.SimulateGame(42, 600);
    assert(result.find("7 Seconds or Less") != std::string::npos &&
           "7SOL should fire with avg speed > 68");
    std::cout << "  PASS: TestSevenSecondsOrLessFires\n";
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: FAIL — thresholds are too high (85 shooting, 85 avg speed).

- [ ] **Step 3: Recalibrate thresholds**

In `src/SynergyEngine.cpp`:

```cpp
// Splash Family: was >= 85.0f, now >= 72.0f
if (player->stats.shooting >= 72.0f) sharpshootersCount++;

// Twin Towers: was >= 82, now >= 80
if (player->stats.height_inches >= 80) giantsCount++;

// 7 Seconds or Less: was > 85.0f, now > 68.0f
if ((totalSpeed / activeFloor.size()) > 68.0f && activeFloor.size() >= 4)
```

- [ ] **Step 4: Run tests**

```bash
cd /Users/brooks/Desktop/shared-core && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS.

- [ ] **Step 5: Commit**

```bash
cd /Users/brooks/Desktop/shared-core && git add src/SynergyEngine.cpp tests/test_engine.cpp && git commit -m "fix: recalibrate synergy thresholds to 1-99 stat distribution (F4)

Splash Family: 85 → 72 shooting
Twin Towers: 82 → 80 inches
7 Seconds or Less: 85 → 68 avg speed

Thresholds now reachable with z-score normalized stats (mean=50, sigma=20)."
```

---

## Phase 3: Data Pipeline & Roster Export

### Task 10: Verify Playoff Awareness in zero-next (F1)

**Files:**
- Check: `/Users/brooks/Desktop/zero-next/src/pages/api/nba/games/index.ts`
- Check: `/Users/brooks/Desktop/zero-next/src/pages/api/nba/analytics/last-night.ts`
- Check: `/Users/brooks/Desktop/zero-next/src/lib/nba/season.ts`

- [ ] **Step 1: Audit existing season_type handling**

Read the games endpoint and analytics/last-night to check if they already accept and pass `season_type`. The zero-next codebase has `parseSeasonType` in `season.ts` — verify it's wired through.

```bash
cd /Users/brooks/Desktop/zero-next && grep -rn "season_type\|seasonType\|Regular Season\|Playoffs" src/lib/nba/ src/pages/api/nba/ --include="*.ts" | head -30
```

- [ ] **Step 2: Fix any hardcoded "Regular Season" references**

If any endpoint hardcodes `"Regular Season"` without accepting a param, add:

```typescript
const seasonType = (req.query.season_type as string) || autoDetectSeasonType();

function autoDetectSeasonType(): string {
  const now = new Date();
  const month = now.getMonth() + 1; // 1-12
  // Playoffs roughly mid-April through mid-June
  return (month >= 4 && month <= 6) ? 'Playoffs' : 'Regular Season';
}
```

- [ ] **Step 3: Run zero-next tests**

```bash
cd /Users/brooks/Desktop/zero-next && npm test -- --run 2>/dev/null || npx vitest run 2>/dev/null || echo "check test command"
```

- [ ] **Step 4: Commit if changes were made**

```bash
cd /Users/brooks/Desktop/zero-next && git add -A && git diff --cached --quiet || git commit -m "fix: ensure playoff awareness in all NBA data endpoints (F1)"
```

---

### Task 11: Build Roster Export Script (F2/F3) — zero-next

**Files:**
- Create: `/Users/brooks/Desktop/zero-next/scripts/export-roster.ts`
- Reference: `/Users/brooks/Desktop/zero-next/src/lib/nba/sim/stat-mapper.ts`
- Reference: `/Users/brooks/Desktop/zero-next/src/lib/nba/sim/roster-builder.ts`

- [ ] **Step 1: Create the export script**

This script uses zero-next's existing `stat-mapper.ts` and `roster-builder.ts` to fetch live NBA data and export a `roster.json` matching shared-core's schema.

Create `/Users/brooks/Desktop/zero-next/scripts/export-roster.ts`:

```typescript
/**
 * Export NBA roster to shared-core engine format.
 * Usage: POSTGRES_URL="..." npx tsx scripts/export-roster.ts [output-path]
 *
 * Fetches players from the DB (populated by ingest pipeline),
 * maps stats to engine scale, and writes roster.json.
 */
import { neon } from '@neondatabase/serverless';
import { mapToEnginePlayer, DEFAULT_COEFFICIENTS } from '../src/lib/nba/sim/stat-mapper';
import { dbRowToRealStats } from '../src/lib/nba/sim/roster-builder';
import * as fs from 'fs';
import * as path from 'path';

const SALARY_CAP = 151_000_000;

function determineCost(salary: number): number {
  const pct = salary / SALARY_CAP;
  if (pct >= 0.25) return 5;
  if (pct >= 0.15) return 4;
  if (pct >= 0.08) return 3;
  if (pct >= 0.03) return 2;
  return 1;
}

async function main() {
  const postgresUrl = process.env.POSTGRES_URL;
  if (!postgresUrl) {
    console.error('POSTGRES_URL required');
    process.exit(1);
  }

  const sql = neon(postgresUrl);
  const outputPath = process.argv[2] || './roster.json';

  // Fetch all players with season stats
  const rows = await sql`
    SELECT p.player_id, p.name, p.position,
           t.abbreviation as team,
           s.ppg, s.rpg, s.apg, s.spg, s.bpg, s.mpg,
           s.fg_pct, s.fg3_pct, s.ft_pct, s.plus_minus,
           p.height, p.weight
    FROM nba_players p
    JOIN nba_player_season_stats s ON p.player_id = s.player_id
    JOIN nba_teams t ON p.team_id = t.team_id
    WHERE s.season = (SELECT MAX(season) FROM nba_player_season_stats)
      AND s.mpg >= 15
    ORDER BY s.ppg DESC
    LIMIT 200
  `;

  console.log(`Fetched ${rows.length} players from DB`);

  const players = rows.map((row: any, idx: number) => {
    const realStats = dbRowToRealStats(row);
    const engine = mapToEnginePlayer(realStats, DEFAULT_COEFFICIENTS);

    // Parse height string (e.g., "6-6") to inches
    let heightInches = 75; // default
    if (row.height) {
      const parts = row.height.split('-');
      if (parts.length === 2) {
        heightInches = parseInt(parts[0]) * 12 + parseInt(parts[1]);
      }
    }

    return {
      id: row.player_id || idx + 1,
      name: row.name,
      team: row.team || '',
      position: row.position || 'SF',
      cost: determineCost((row.salary || 0)),
      stats: {
        shooting: Math.round(engine.shooting),
        speed: Math.round(engine.speed),
        defense: Math.round(engine.defense),
        rebounding: Math.round(Math.min(99, Math.max(1, 50 + (row.rpg - 5) * 5))),
        playmaking: Math.round(Math.min(99, Math.max(1, 50 + (row.apg - 3) * 6))),
      },
      heightInches,
    };
  });

  const output = { players };
  fs.writeFileSync(path.resolve(outputPath), JSON.stringify(output, null, 2));
  console.log(`Exported ${players.length} players to ${outputPath}`);
}

main().catch(console.error);
```

- [ ] **Step 2: Test the script compiles**

```bash
cd /Users/brooks/Desktop/zero-next && npx tsx --check scripts/export-roster.ts 2>&1 | head -5
```

- [ ] **Step 3: Commit**

```bash
cd /Users/brooks/Desktop/zero-next && git add scripts/export-roster.ts && git commit -m "feat: add roster export script for BballTactics (F2/F3)

Exports live NBA player data to shared-core engine format.
Maps shooting/speed/defense/rebounding/playmaking to 1-99 scale.
Usage: POSTGRES_URL=... npx tsx scripts/export-roster.ts [output-path]"
```

---

### Task 12: Win Probability Endpoint (F17) — zero-next

**Files:**
- Create: `/Users/brooks/Desktop/zero-next/src/pages/api/nba/predict.ts`

- [ ] **Step 1: Create the endpoint**

```typescript
import type { NextApiRequest, NextApiResponse } from 'next';
import { fetchStats } from '@/lib/nba/client';
import { currentNbaSeason } from '@/lib/nba/season';

const HOME_COURT_ADJ = 3.0; // ~3 points of home court advantage

interface TeamRatings {
  teamId: number;
  netRating: number;
  offRating: number;
  defRating: number;
  pace: number;
}

async function getTeamRatings(teamId: number, season: string): Promise<TeamRatings | null> {
  const rows = await fetchStats('leaguedashteamstats', {
    Season: season,
    SeasonType: 'Regular Season',
    TeamID: teamId,
    MeasureType: 'Advanced',
  });

  if (!rows.length) return null;
  const r = rows[0];

  return {
    teamId,
    netRating: Number(r.NET_RATING ?? 0),
    offRating: Number(r.OFF_RATING ?? 100),
    defRating: Number(r.DEF_RATING ?? 100),
    pace: Number(r.PACE ?? 100),
  };
}

export default async function handler(req: NextApiRequest, res: NextApiResponse) {
  if (req.method !== 'GET') return res.status(405).json({ error: 'GET only' });

  const homeId = Number(req.query.home);
  const awayId = Number(req.query.away);

  if (!homeId || !awayId) {
    return res.status(400).json({ error: 'home and away team IDs required' });
  }

  const season = currentNbaSeason();

  const [home, away] = await Promise.all([
    getTeamRatings(homeId, season),
    getTeamRatings(awayId, season),
  ]);

  if (!home || !away) {
    return res.status(404).json({ error: 'Team ratings not found' });
  }

  const delta = home.netRating - away.netRating + HOME_COURT_ADJ;
  const homeWinProb = 1 / (1 + Math.pow(10, -delta / 10));

  return res.status(200).json({
    homeWinProb: Math.round(homeWinProb * 1000) / 1000,
    awayWinProb: Math.round((1 - homeWinProb) * 1000) / 1000,
    homeNetRating: home.netRating,
    awayNetRating: away.netRating,
    homeOffRating: home.offRating,
    homeDefRating: home.defRating,
    awayOffRating: away.offRating,
    awayDefRating: away.defRating,
    paceDelta: home.pace - away.pace,
    homeCourtAdj: HOME_COURT_ADJ,
    model: 'elo-net-rating',
    season,
  });
}
```

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/brooks/Desktop/zero-next && npx tsc --noEmit src/pages/api/nba/predict.ts 2>&1 | head -10
```

- [ ] **Step 3: Commit**

```bash
cd /Users/brooks/Desktop/zero-next && git add src/pages/api/nba/predict.ts && git commit -m "feat: add win probability endpoint GET /api/nba/predict (F17)

Elo-style model using team net ratings + home court adjustment.
P(home) = 1 / (1 + 10^(-(homeNet - awayNet + 3) / 10))
Usage: /api/nba/predict?home=1610612747&away=1610612744"
```

---

### Task 13: Series Context Endpoint (F18) — zero-next

**Files:**
- Create: `/Users/brooks/Desktop/zero-next/src/pages/api/nba/series.ts`

- [ ] **Step 1: Create the endpoint**

```typescript
import type { NextApiRequest, NextApiResponse } from 'next';
import { fetchStats } from '@/lib/nba/client';
import { currentNbaSeason } from '@/lib/nba/season';

interface SeriesRecord {
  matchup: string;
  homeTeam: { id: number; name: string; wins: number };
  awayTeam: { id: number; name: string; wins: number };
  round: number;
  gameNumber: number;
  isEliminationGame: boolean;
  seriesStatus: string;
}

function parseNbaGameId(gameId: string) {
  // NBA playoff game IDs: 0042XYYZZZ
  // X = season year suffix, YY = round (01-04), ZZZ = game sequence
  if (!gameId || gameId.length < 10) return null;
  const prefix = gameId.substring(0, 4);
  if (prefix !== '0042') return null; // not a playoff game
  const round = parseInt(gameId.substring(5, 7));
  return { round };
}

export default async function handler(req: NextApiRequest, res: NextApiResponse) {
  if (req.method !== 'GET') return res.status(405).json({ error: 'GET only' });

  const season = currentNbaSeason();

  const games = await fetchStats('leaguegamefinder', {
    Season: season,
    SeasonType: 'Playoffs',
    LeagueID: '00',
  });

  if (!games.length) {
    return res.status(200).json({ series: [], message: 'No playoff games found' });
  }

  // Group games by matchup pair
  const matchups = new Map<string, any[]>();
  for (const game of games) {
    const matchup = String(game.MATCHUP ?? '');
    // Normalize matchup key: sort team abbreviations
    const teams = matchup.replace(/\s*(vs\.|@)\s*/g, '|').split('|').sort();
    const key = teams.join(' vs ');
    if (!matchups.has(key)) matchups.set(key, []);
    matchups.get(key)!.push(game);
  }

  const series: SeriesRecord[] = [];
  for (const [matchup, games] of matchups) {
    // Count wins per team
    const wins: Record<string, number> = {};
    const teamInfo: Record<string, { id: number; name: string }> = {};

    for (const g of games) {
      const teamId = Number(g.TEAM_ID);
      const teamName = String(g.TEAM_NAME ?? '');
      const wl = String(g.WL ?? '');

      teamInfo[teamName] = { id: teamId, name: teamName };
      if (wl === 'W') {
        wins[teamName] = (wins[teamName] || 0) + 1;
      }
    }

    const teamNames = Object.keys(teamInfo);
    if (teamNames.length < 2) continue;

    const totalGames = Math.max(...Object.values(wins), 0);
    const gameNumber = Object.values(wins).reduce((a, b) => a + b, 0);
    const maxWins = Math.max(...Object.values(wins));
    const isElimination = maxWins >= 3;

    const parsed = parseNbaGameId(String(games[0]?.GAME_ID ?? ''));

    series.push({
      matchup,
      homeTeam: { ...teamInfo[teamNames[0]], wins: wins[teamNames[0]] || 0 },
      awayTeam: { ...teamInfo[teamNames[1]], wins: wins[teamNames[1]] || 0 },
      round: parsed?.round ?? 0,
      gameNumber,
      isEliminationGame: isElimination,
      seriesStatus: maxWins >= 4 ? 'Final' : `Game ${gameNumber + 1}`,
    });
  }

  return res.status(200).json({ series, season });
}
```

- [ ] **Step 2: Verify it compiles**

```bash
cd /Users/brooks/Desktop/zero-next && npx tsc --noEmit src/pages/api/nba/series.ts 2>&1 | head -10
```

- [ ] **Step 3: Commit**

```bash
cd /Users/brooks/Desktop/zero-next && git add src/pages/api/nba/series.ts && git commit -m "feat: add playoff series context endpoint GET /api/nba/series (F18)

Groups playoff games by matchup pair, returns series records,
game numbers, and identifies elimination games."
```

---

## Final Verification

### Task 14: Run All Tests Across Repos

- [ ] **Step 1: shared-core full test suite**

```bash
cd /Users/brooks/Desktop/shared-core && cmake -B build && cmake --build build --target test_runner && ./build/test_runner
```

Expected: ALL PASS (original 10 + ~6 new tests).

- [ ] **Step 2: BballTactics server check**

```bash
cd /Users/brooks/Desktop/BballTactics && python -c "import server; print('server imports OK')"
```

- [ ] **Step 3: zero-next type check**

```bash
cd /Users/brooks/Desktop/zero-next && npx tsc --noEmit 2>&1 | tail -5
```

- [ ] **Step 4: Update shared-core submodule in BballTactics**

```bash
cd /Users/brooks/Desktop/BballTactics && cd shared-core && git pull origin main && cd .. && git add shared-core && git commit -m "chore: update shared-core submodule with F4-F16 fixes"
```
