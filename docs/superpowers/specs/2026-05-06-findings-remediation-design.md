# NBA Modeling Findings Remediation

**Date:** 2026-05-06
**Scope:** 14 findings across shared-core, BballTactics, and zero-next
**Approach:** Quick wins first (isolated fixes), then schema changes, then data pipeline architecture

## Context

The Basketball Modeling daily routine identified 19 findings across the ecosystem. 4 are iOS-only (NBASwiftTactics not on machine) and 1 (F14) is resolved architecturally by making zero-next the single data pipeline. This spec covers the remaining 14 actionable findings.

NbaApi (Flask app) is archived. The `nba_api` Python package remains available as a data-fetching tool. zero-next is the single data pipeline going forward. BballTactics consumes a static roster JSON exported by zero-next — no runtime dependency.

## Phase 1: Self-Contained Engine Fixes

Isolated code changes with no cross-repo data dependencies. All can be done independently.

### F15 — Fixed RNG Seed (shared-core)

**Problem:** `std::mt19937 rng(42)` in the C++ engine makes every simulation deterministic. Same rosters always produce identical scores.

**Fix:** Seed from `std::random_device` by default. Keep a `reseed(uint64_t)` method for test reproducibility.

**Files:** shared-core `src/` — wherever the RNG is initialized.

**Validation:** Run the same two rosters 10 times, confirm 10 different scores.

### F16 — Steal Rate Is Frame-Rate Dependent (shared-core)

**Problem:** Steal chance is `(defense/100) * 0.15 * dt` — a per-tick roll that produces different statistical distributions at different tick rates.

**Fix:** Convert to a per-possession steal probability model. Steal attempt fires once per possession change event, not every physics tick. Target: `stealProb = (defense/100) * 0.12` (12% max per possession for a perfect defender, matching NBA averages of ~1 steal per 8 possessions for elite defenders). If the engine lacks an explicit possession concept, the fix is to gate steal rolls behind a cooldown timer (e.g., one roll per 2 seconds of sim time) rather than rolling every tick.

**Files:** shared-core simulation step logic.

**Validation:** Run at two different tick rates, confirm steal rates converge within 2%.

### F8 — No Home Court Advantage (shared-core)

**Problem:** No modifier for home/away. The engine produces ~50/50 for mirrored matchups.

**Fix:** Add a `homeCourtBonus` float param to the game config (suggested: +3 shooting, +2 speed for home team). Applied as a flat buff to home team player stats at simulation start.

**Files:** shared-core game config struct + simulation init.

**Validation:** Run mirrored matchup suite (A@home vs B, then B@home vs A) 100 times each, confirm 5-10% win rate differential.

### F13 — Bot Fallback Is Static 40/40/40 (BballTactics)

**Problem:** When no ghost opponent exists, the bot has hardcoded 40/40/40 stats — trivially beatable.

**Fix:** Scale bot stats by round number:
- Rounds 1-3: 50/50/50
- Rounds 4-6: 65/65/65
- Rounds 7-10: 80/75/70

**Files:** `BballTactics/server.py` — `submit_and_fetch()` fallback logic.

**Validation:** Confirm bot stats increase with round number in server response.

### F5 — Shot Probability Underspecified (shared-core)

**Problem:** Single formula `(shooting/100) * exp(-dist * 0.05)` for all shot types. No paint/midrange/3pt distinction.

**Fix:** Introduce shot zone tiers with distinct base probability curves:
- Paint (<8 units from hoop): `baseProbability = 0.60 * (shooting/100)`
- Midrange (8-22 units): `baseProbability = 0.42 * (shooting/100)`
- Three-point (>22 units): `baseProbability = 0.36 * (shooting/100)`

Distance decay within each zone uses a gentler exponential. The constants are calibrated to approximate real NBA league-average shooting by zone (~65% at rim, ~42% midrange, ~36% from three).

**Files:** shared-core shot probability calculation.

**Validation:** Confirm paint shots hit at higher rates than threes for the same shooter.

### F7 — Formation Heatmap Is a Hardcoded Stub (BballTactics)

**Problem:** `formation_heatmap()` returns `np.full((5,5), 0.5)` — flat, no signal.

**Fix:** Parse actual starting positions from game output JSON and compute win-rate-by-position. Requires the C++ engine to echo starting positions in its output (if it doesn't already).

**Files:** `BballTactics/bots/balance/analyze.py` — `formation_heatmap()`. Possibly shared-core game output serialization.

**Validation:** Heatmap grid has standard deviation >= 0.05 across cells after 100 simulated games.

### F9 — Salary Cap at $136M (BballTactics)

**Problem:** Hardcoded 2022-23 salary cap value. Player cost tiers are miscalibrated.

**Fix:** Update to $151M (2025-26 NBA salary cap). This is a single constant change.

**Files:** `BballTactics/scraper.py` — `_determine_cost()` or equivalent constant.

**Validation:** Confirm the constant is $151M.

## Phase 2: Stat Model & Synergy Fixes

Schema change in shared-core that ripples to BballTactics bindings and roster JSON format.

### F6 — Only 3 Stat Dimensions (shared-core)

**Problem:** Engine only models shooting, speed, defense. Missing rebounding and playmaking — two core game-flow drivers.

**Fix:** Add two fields to `PlayerStats` struct:
- `rebounding` (int, 1-99): Wired into rebound assignment logic. Higher rebounding = higher priority when a missed shot occurs.
- `playmaking` (int, 1-99): Replaces the hardcoded 0.3 pass probability. `passProbability = playmaking / 100.0 * 0.5` (so a 60 playmaking player passes 30% of the time, a 90 playmaking player passes 45%).

**Ripple effects:**
- shared-core: `PlayerStats` struct, simulation step logic (rebound assignment + pass logic)
- shared-core: Wasm/JSON bindings to accept the new fields
- shared-core: JSON schema update
- BballTactics: roster JSON format gains two new fields (backwards-compatible if defaulted to 50)

**Files:** shared-core `PlayerStats` definition, simulation logic, bindings. BballTactics roster schema.

**Validation:** A player with 90 rebounding wins significantly more rebounds than a player with 30 rebounding. A player with 90 playmaking passes more often than one with 30.

### F4 — Synergies Unreachable (shared-core)

**Problem:** Splash Family requires shooting >= 85, Twin Towers requires height >= 82", 7 Seconds requires avg speed > 85. With z-score normalization centering at 50, these thresholds are effectively unreachable.

**Fix:** Recalibrate to the actual 1-99 distribution:
- **Splash Family:** shooting >= 72 (was 85). Three sharpshooters should trigger this.
- **Twin Towers:** heightInches >= 80 (was 82). 6'8"+ should qualify, not just 6'10"+.
- **7 Seconds or Less:** average speed > 68 (was 85). A fast backcourt should trigger this.
- **Franchise:** Already works once `team` field is populated (Phase 3).

**Depends on:** F6 (stat model must accept `heightInches` as a real value, not default 72).

**Files:** shared-core synergy engine — threshold constants.

**Validation:** Load a known comp (e.g., Curry + Thompson + Poole-type stats) and assert Splash Family fires.

## Phase 3: Data Pipeline & Roster Export

zero-next becomes the single data source. BballTactics scraper is replaced with a consumer.

### F1 — Playoff-Aware Data Fetching (zero-next)

**Problem:** All game-fetching hardcodes `season_type="Regular Season"`. Blind to playoffs.

**Fix:** Build a data-fetching module in zero-next that:
- Accepts a `seasonType` param (default: auto-detect based on date — after mid-April, use "Playoffs")
- Uses `nba_api` Python package via a script, or a TypeScript equivalent, to fetch game data
- Exposes playoff-aware endpoints at `/api/nba/games` and `/api/nba/last-night`

**Files:** New module in `zero-next/src/lib/nba/` or `zero-next/scripts/`.

**Validation:** Endpoint returns playoff games when called during playoff dates.

### F2/F3 — Live Roster Export (zero-next + BballTactics)

**Problem:** BballTactics scraper uses 5 hardcoded mock players. No live data.

**Fix — zero-next side:** Build a roster export script that:
- Fetches all active players from the current season (or playoff teams) via `nba_api`
- Computes z-score normalized stats for all 5 dimensions (shooting, speed, defense, rebounding, playmaking)
- Populates `team`, `heightInches`, `position`
- Computes `cost` tier using the updated $151M salary cap
- Exports to a `roster.json` matching the shared-core schema

**Fix — BballTactics side:** Replace `scraper.py` mock path with a simple consumer:
- Read the exported `roster.json` (either checked in or fetched at build time)
- Copy to `public/engine_roster.json`

**Roster JSON schema:**
```json
{
  "players": [{
    "name": "Shai Gilgeous-Alexander",
    "team": "OKC",
    "shooting": 82,
    "speed": 74,
    "defense": 68,
    "rebounding": 45,
    "playmaking": 78,
    "heightInches": 78,
    "position": "SG",
    "cost": 5
  }]
}
```

**Target:** All 16 playoff teams, top 8 rotation players each = ~128 players minimum.

**Files:** New script in zero-next. `BballTactics/scraper.py` rewrite.

**Validation:** `roster.json` contains >= 100 players, all with non-empty `team` fields and all 5 stats populated.

### F17 — Win Probability Endpoint (zero-next)

**Problem:** No endpoint computes pre-game win probability.

**Fix:** Add `GET /api/nba/predict?home=<teamId>&away=<teamId>` that:
- Fetches both teams' `net_rating`, `off_rating`, `def_rating`, `pace` from `nba_api`
- Computes: `P(home win) = 1 / (1 + 10^(-(homeNet - awayNet + homeCourt) / 10))`
- Returns: `{ homeWinProb, awayWinProb, homeNetRating, awayNetRating, paceDelta, homeCourtAdj }`

**Files:** New API route in `zero-next/src/pages/api/nba/predict.ts` (or equivalent).

**Validation:** Returns a sensible probability (50-65%) for a matched playoff game.

### F18 — Series Context (zero-next)

**Problem:** No concept of playoff series — every game is treated as independent.

**Fix:** Add `GET /api/nba/series` that:
- Fetches playoff games for the current season via `nba_api`
- Groups by matchup pair (using NBA game ID format: `0042XYYZZZ` where Y=round, ZZ=series)
- Returns series records, game-in-series number, and identifies elimination games

**Files:** New API route in `zero-next/src/pages/api/nba/series.ts` (or equivalent).

**Validation:** Returns correct series records for current playoff matchups.

## Deferred (Not In Scope)

### iOS Findings (NBASwiftTactics not on machine)
- **F10:** iOS blends regular/playoff stats
- **F11:** No playoff round type in GameSeason
- **F12:** Lakers-locked service
- **F19:** Salary cap hardcoded at $136M in GameEconomy.swift

These should be addressed when NBASwiftTactics is available. The Swift sim should adopt the same fixes from shared-core (RNG, shot zones, home court, synergies, new stats).

### Enhancement Ideas (Feature Roadmap, Not Bugs)
The daily routine also flagged these as "findings" but they are new features:
- Free throw pipeline
- Shot clock / possession clock
- Stamina degradation
- Position-based movement AI
- Best-of-7 series game format
- Clutch stats endpoint

These belong on a feature roadmap, not a bug tracker.

## Execution Order

```
Phase 1 (independent, parallelizable):
  F15 (RNG) ─────────────┐
  F16 (steal rate) ───────┤
  F8  (home court) ───────┤
  F13 (bot scaling) ──────┤── All land independently
  F5  (shot zones) ───────┤
  F7  (heatmap) ──────────┤
  F9  (salary cap) ───────┘

Phase 2 (sequential, schema change):
  F6  (add stats) ──→ F4 (recalibrate synergies)

Phase 3 (sequential, data pipeline):
  F1  (playoff fetch) ──→ F2/F3 (roster export) ──→ F17 (predict) ──→ F18 (series)
```

Phases 1 and 2 can run in parallel. Phase 3 depends on Phase 2 (roster schema must be finalized before the export script builds against it).
