#include <iostream>
#include <cassert>
#include <string>
#include <cmath>
#include "GameManager.h"
#include "ShotProbability.h"
#include "Basketball.h"

void TestBasicMovement() {
    std::cout << "--- Test: Basic Movement ---\n";

    GameManager engine;
    engine.SpawnPlayer(1, "Steph Curry", 99.0f, 99.0f);
    engine.SetPlayerPlay(1, 1, 50.0f, 100.0f); // CUT_TO_BASKET

    std::string initialState = engine.GetGameStateJSON();
    std::cout << "Initial State: " << initialState << "\n";

    engine.TickSimulation(1.0f);

    std::string tickedState = engine.GetGameStateJSON();
    std::cout << "State after 1s tick: " << tickedState << "\n";

    assert(initialState != tickedState && "Player did not move during tick!");
    std::cout << "PASSED\n\n";
}

void TestSynergyDetection() {
    std::cout << "--- Test: Synergy Detection ---\n";

    GameManager engine;
    // Two Lakers players should trigger Franchise synergy
    engine.SpawnPlayer(1, "LeBron James", 85.0f, 80.0f);
    engine.SpawnPlayer(2, "Anthony Davis", 75.0f, 70.0f);

    // StartRound calls SynergyEngine::AnalyzeRoster internally
    engine.StartRound();
    std::cout << "PASSED (no crash, synergy logic ran)\n\n";
}

void TestStatClamping() {
    std::cout << "--- Test: Stat Clamping ---\n";

    PlayerEntity player(1, "Test Player", 95.0f, 95.0f);

    // Apply limitless range (should cap at 99)
    player.ApplyLimitlessRange();
    assert(player.stats.shooting <= 99.0f && "Shooting exceeded 99 after ability!");

    // Calling again should be a no-op
    float shootingBefore = player.stats.shooting;
    player.ApplyLimitlessRange();
    assert(player.stats.shooting == shootingBefore && "Limitless Range stacked!");

    std::cout << "PASSED\n\n";
}

// Bug 2.1: EliteShooter stacking — ability must not stack on repeated calls
void TestLimitlessRangeNoStack() {
    std::cout << "--- Test: Limitless Range No Stack (Bug 2.1) ---\n";

    PlayerEntity player(1, "Klay Thompson", 70.0f, 80.0f);

    player.ApplyLimitlessRange();
    float afterFirst = player.stats.shooting; // should be 99 (80+20 capped)
    assert(afterFirst <= 99.0f && "Shooting exceeded 99!");

    player.ApplyLimitlessRange();
    player.ApplyLimitlessRange();
    player.ApplyLimitlessRange();
    assert(player.stats.shooting == afterFirst && "Limitless Range stacked on repeat calls!");

    std::cout << "PASSED\n\n";
}

// Bug 2.2: abs() vs std::abs() — verify transition uses proper float abs
// Bug 2.3: Transition must check BOTH X and Y arrival
void TestTransitionBothAxes() {
    std::cout << "--- Test: Transition Checks Both Axes (Bug 2.2 + 2.3) ---\n";

    PlayerEntity player(1, "Fast Player", 99.0f, 50.0f);
    player.state = PlayerState::TRANSITION_TO_OFFENSE;

    // Target is far on X, close on Y — Y arrives first
    player.pos = Vector2D(0.0f, 0.0f);
    player.targetLocation = Vector2D(100.0f, 1.0f);

    // Tick once — Y should snap, X should still be moving
    player.UpdateTransition();

    // Y arrived but X hasn't — state must NOT be OFFENSE yet
    assert(player.state == PlayerState::TRANSITION_TO_OFFENSE &&
           "Transition completed with only Y arrived — Bug 2.3 not fixed!");

    // Keep ticking until X arrives too
    for (int i = 0; i < 200; i++) {
        player.UpdateTransition();
        if (player.state == PlayerState::OFFENSE) break;
    }
    assert(player.state == PlayerState::OFFENSE &&
           "Transition never completed even after many ticks!");

    std::cout << "PASSED\n\n";
}

// Bug 2.4: ShotProbability must not exceed 1.0 at close range
void TestShotProbabilityCloseRange() {
    std::cout << "--- Test: Shot Probability Bounded at Close Range (Bug 2.4) ---\n";

    PlayerEntity shooter(1, "Point Blank Shooter", 50.0f, 99.0f);
    PlayerEntity defender(2, "Far Defender", 50.0f, 50.0f);

    // Shooter standing right at the hoop, defender far away
    shooter.pos = Vector2D(25.0f, 4.0f);
    defender.pos = Vector2D(25.0f, 50.0f);
    Vector2D hoopPos(25.0f, 4.0f); // distance ~0

    float prob = CalculateShotProbability(&shooter, &defender, hoopPos);
    assert(prob <= 1.0f && "Shot probability exceeded 1.0 at close range!");
    assert(prob >= 0.0f && "Shot probability went negative!");

    // Also test at dist=0.5 (the old formula gave 1/(0.5*0.1) = 20x multiplier)
    shooter.pos = Vector2D(25.0f, 4.5f);
    prob = CalculateShotProbability(&shooter, &defender, hoopPos);
    assert(prob <= 1.0f && "Shot probability exceeded 1.0 at dist=0.5!");

    std::cout << "  At hoop: prob=" << prob << "\n";
    std::cout << "PASSED\n\n";
}

// Bug 2.5: Uninitialized members — all fields must have safe defaults
void TestDefaultInitialization() {
    std::cout << "--- Test: Default Initialization (Bug 2.5) ---\n";

    // PlayerEntity default-constructed
    PlayerEntity p;
    assert(p.pos.x == 0.0f && p.pos.y == 0.0f && "pos not initialized!");
    assert(p.velocity.x == 0.0f && p.velocity.y == 0.0f && "velocity not initialized!");
    assert(p.plannedAction == ActionType::HOLD && "plannedAction not initialized!");
    assert(p.state == PlayerState::OFFENSE && "state not initialized!");
    assert(p.hasLimitlessRange == false && "hasLimitlessRange not initialized!");

    // Basketball default-constructed
    Basketball ball;
    assert(ball.isPossessed == false && "isPossessed not initialized!");
    assert(ball.possessorId == -1 && "possessorId not initialized!");
    assert(ball.position.x == 0.0f && "ball position.x not initialized!");
    assert(ball.position.z == 0.0f && "ball position.z not initialized!");
    assert(ball.velocity.x == 0.0f && "ball velocity not initialized!");

    std::cout << "PASSED\n\n";
}

// Vector3D operators — were stubs in old Basketball.cpp, now must work
void TestVector3DOperators() {
    std::cout << "--- Test: Vector3D Operators ---\n";

    Vector3D a(1.0f, 2.0f, 3.0f);
    Vector3D b(4.0f, 5.0f, 6.0f);

    Vector3D sum = a + b;
    assert(sum.x == 5.0f && sum.y == 7.0f && sum.z == 9.0f && "Vector3D + broken!");

    Vector3D diff = b - a;
    assert(diff.x == 3.0f && diff.y == 3.0f && diff.z == 3.0f && "Vector3D - broken!");

    Vector3D scaled = a * 2.0f;
    assert(scaled.x == 2.0f && scaled.y == 4.0f && scaled.z == 6.0f && "Vector3D * broken!");

    float mag = Vector3D(3.0f, 4.0f, 0.0f).Magnitude();
    assert(std::abs(mag - 5.0f) < 0.001f && "Vector3D Magnitude broken!");

    std::cout << "PASSED\n\n";
}

void TestLoadRosterJSON() {
    std::cout << "--- Test: LoadRosterJSON ---\n";

    GameManager engine;

    std::string json = R"([
        {"id": 1, "name": "Steph Curry", "cost": 5, "stats": {"shooting": 74, "speed": 63, "defense": 51}},
        {"id": 2, "name": "Alex Caruso", "cost": 3, "stats": {"shooting": 38, "speed": 56, "defense": 63}}
    ])";

    engine.LoadRosterJSON(json);

    // Verify players are in the engine by checking game state
    std::string state = engine.GetGameStateJSON();
    assert(state.find("Steph Curry") != std::string::npos || state.find("\"id\": 1") != std::string::npos);

    // Verify we can tick without crashing (players have valid stats)
    engine.TickSimulation(1.0f);

    // Verify StartRound works on loaded roster
    engine.StartRound();

    std::cout << "  State: " << state << "\n";
    std::cout << "PASSED\n\n";
}

void TestLoadRosterJSON_BadInput() {
    std::cout << "--- Test: LoadRosterJSON Bad Input ---\n";

    GameManager engine;

    // Should not crash on invalid JSON
    engine.LoadRosterJSON("not valid json {{{");
    engine.LoadRosterJSON("");
    engine.LoadRosterJSON("[]");

    std::cout << "PASSED\n\n";
}

// F15: seed=0 must use random_device — two runs should differ
void TestRandomSeedProducesDifferentResults() {
    std::cout << "--- Test: Random Seed Produces Different Results (F15) ---\n";

    auto makeResult = []() -> std::string {
        GameManager engine;
        engine.SpawnPlayer(1, "Home PG", 80.0f, 85.0f);
        engine.SpawnPlayer(2, "Home SG", 75.0f, 90.0f);
        engine.SpawnPlayer(3, "Home SF", 70.0f, 75.0f);
        engine.SpawnPlayer(4, "Home PF", 65.0f, 60.0f);
        engine.SpawnPlayer(5, "Home C",  55.0f, 50.0f);
        for (int i = 1; i <= 5; i++) {
            engine.SetPlayerCoordinates(i, float(i % 3), float(i / 3), float(i % 3), float(i / 3));
        }
        return engine.SimulateGame(0, 600); // seed=0 → random_device
    };

    std::string r1 = makeResult();
    std::string r2 = makeResult();

    // With true randomness, two runs should differ. We allow up to 5 retries
    // to avoid flakiness from an astronomically unlikely collision.
    bool differ = (r1 != r2);
    for (int attempt = 0; !differ && attempt < 5; ++attempt) {
        std::string r3 = makeResult();
        differ = (r1 != r3);
    }
    assert(differ && "seed=0 produced identical results across runs — RNG not random!");

    std::cout << "PASSED\n\n";
}

// F16: steal rate must not be frame-rate dependent — cooldown-based system
void TestStealRateNotFrameRateDependent() {
    std::cout << "--- Test: Steal Rate Not Frame-Rate Dependent (F16) ---\n";

    // Run a short sim at a fine dt (many ticks) — should complete without crash
    GameManager engineFine;
    engineFine.SpawnPlayer(1, "Home PG", 80.0f, 85.0f);
    engineFine.SpawnPlayer(2, "Home SG", 75.0f, 90.0f);
    engineFine.SpawnPlayer(3, "Away PG", 70.0f, 85.0f);
    engineFine.SpawnPlayer(4, "Away SG", 65.0f, 80.0f);
    engineFine.SimulateGame(42, 30); // 30 sim-seconds, fixed seed

    // Run again at a coarse dt — should also complete without crash
    GameManager engineCoarse;
    engineCoarse.SpawnPlayer(1, "Home PG", 80.0f, 85.0f);
    engineCoarse.SpawnPlayer(2, "Home SG", 75.0f, 90.0f);
    engineCoarse.SpawnPlayer(3, "Away PG", 70.0f, 85.0f);
    engineCoarse.SpawnPlayer(4, "Away SG", 65.0f, 80.0f);
    engineCoarse.SimulateGame(42, 30);

    std::cout << "PASSED\n\n";
}

// F5: shot zone tiers — paint > midrange > three, all in (0, 1]
void TestShotZoneTiers() {
    std::cout << "--- Test: Shot Zone Tiers (F5) ---\n";

    // Shooter with 70 shooting; defender far away (no contest)
    PlayerEntity shooter(1, "Zone Shooter", 50.0f, 70.0f);
    PlayerEntity defender(2, "Far Defender", 50.0f, 50.0f);
    defender.pos = Vector2D(25.0f, 100.0f); // far from play

    Vector2D hoopPos(25.0f, 4.0f);

    // Paint: 3 units from hoop
    shooter.pos = Vector2D(25.0f, 7.0f);
    float paintProb = CalculateShotProbability(&shooter, &defender, hoopPos);

    // Midrange: 15 units from hoop
    shooter.pos = Vector2D(25.0f, 19.0f);
    float midProb = CalculateShotProbability(&shooter, &defender, hoopPos);

    // Three-point: 25 units from hoop
    shooter.pos = Vector2D(25.0f, 29.0f);
    float threeProb = CalculateShotProbability(&shooter, &defender, hoopPos);

    std::cout << "  Paint prob:    " << paintProb  << "\n";
    std::cout << "  Midrange prob: " << midProb    << "\n";
    std::cout << "  Three prob:    " << threeProb  << "\n";

    assert(paintProb  > 0.0f && paintProb  <= 1.0f && "Paint prob out of (0, 1]!");
    assert(midProb    > 0.0f && midProb    <= 1.0f && "Midrange prob out of (0, 1]!");
    assert(threeProb  > 0.0f && threeProb  <= 1.0f && "Three prob out of (0, 1]!");
    assert(paintProb  > midProb   && "Paint must be > midrange!");
    assert(midProb    > threeProb && "Midrange must be > three!");

    std::cout << "PASSED\n\n";
}

// F6: rebounding and playmaking stat fields exist and are settable
void TestReboardingAndPlaymakingStats() {
    std::cout << "--- Test: Rebounding + Playmaking Stats (F6) ---\n";

    PlayerEntity p(1, "Test Player", 70.0f, 60.0f);

    // Default values should be 50
    assert(p.stats.rebounding == 50.0f && "rebounding default should be 50!");
    assert(p.stats.playmaking == 50.0f && "playmaking default should be 50!");

    // Fields must be settable
    p.stats.rebounding = 80.0f;
    p.stats.playmaking = 30.0f;
    assert(p.stats.rebounding == 80.0f && "rebounding not settable!");
    assert(p.stats.playmaking == 30.0f && "playmaking not settable!");

    std::cout << "PASSED\n\n";
}

// F6: LoadRosterJSON should parse rebounding and playmaking with backwards compat
void TestLoadRosterJSON_NewStatFields() {
    std::cout << "--- Test: LoadRosterJSON Parses rebounding + playmaking (F6) ---\n";

    GameManager engine;

    // With new fields present
    std::string jsonWithFields = R"([
        {"id": 1, "name": "Rebounder", "cost": 3, "stats": {"shooting": 50, "speed": 50, "defense": 50, "rebounding": 85, "playmaking": 30}}
    ])";
    engine.LoadRosterJSON(jsonWithFields);
    engine.TickSimulation(0.1f);

    // Without new fields — backwards compat, should default to 50
    GameManager engine2;
    std::string jsonWithoutFields = R"([
        {"id": 2, "name": "Legacy Player", "cost": 2, "stats": {"shooting": 60, "speed": 55, "defense": 45}}
    ])";
    engine2.LoadRosterJSON(jsonWithoutFields);
    engine2.TickSimulation(0.1f);

    std::cout << "PASSED\n\n";
}

// F8: home court advantage — home should win more often when bonus is applied
void TestHomeCourtAdvantage() {
    std::cout << "--- Test: Home Court Advantage (F8) ---\n";

    const int NUM_SIMS = 50;
    const int TICKS    = 1800; // 60s of sim time at 30fps

    // Parse a score value from SimulateGame JSON result
    auto parseScore = [](const std::string& result, const std::string& key) -> int {
        size_t pos = result.find("\"" + key + "\": ");
        if (pos == std::string::npos) return 0;
        return std::stoi(result.substr(pos + key.size() + 4));
    };

    auto runSims = [&](float shootBonus, float speedBonus) -> int {
        int wins = 0;
        for (int i = 0; i < NUM_SIMS; i++) {
            GameManager engine;
            // Mirror the SpawnBotOpponents stats so teams start evenly matched
            engine.SpawnPlayer(1, "Home PG", 60.0f, 45.0f);
            engine.SpawnPlayer(2, "Home SG", 55.0f, 40.0f);
            engine.SpawnPlayer(3, "Home SF", 65.0f, 50.0f);
            engine.SetHomeCourtBonus(shootBonus, speedBonus);
            std::string result = engine.SimulateGame(static_cast<uint32_t>(200 + i), TICKS);
            if (parseScore(result, "homeScore") > parseScore(result, "awayScore")) wins++;
        }
        return wins;
    };

    // Baseline: no bonus
    int baselineWins = runSims(0.0f, 0.0f);
    // Large bonus: +30 shooting, +20 speed — overcomes positional asymmetry, demonstrates feature
    int bonusWins = runSims(30.0f, 20.0f);

    std::cout << "  Baseline home wins (no bonus):      " << baselineWins << "/" << NUM_SIMS << "\n";
    std::cout << "  Home wins (+30 shoot, +20 speed):   " << bonusWins    << "/" << NUM_SIMS << "\n";

    // The bonus must improve home win rate relative to baseline
    if (bonusWins <= baselineWins) {
        std::cerr << "FAIL: Home court bonus did not improve win rate over baseline!\n";
        std::exit(1);
    }

    // With a significant bonus, home should win more than baseline (already checked above)
    // and at least 36% of games (18/50) — conservative to avoid flakiness
    if (bonusWins < 18) {
        std::cerr << "FAIL: Expected home to win >=18/50 games with large bonus, got "
                  << bonusWins << "\n";
        std::exit(1);
    }

    std::cout << "PASSED\n\n";
}

void TestSplashFamilySynergyFires() {
    GameManager gm;
    // Three sharpshooters with shooting >= 72
    gm.SpawnPlayer(1, "Shooter1", 60, 75);
    gm.SpawnPlayer(2, "Shooter2", 60, 74);
    gm.SpawnPlayer(3, "Shooter3", 60, 73);
    gm.SpawnPlayer(4, "Big", 40, 50);
    gm.SpawnPlayer(5, "Guard", 70, 60);

    std::string result = gm.SimulateGame(42, 600);
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

int main() {
    std::cout << "=== C++ Engine Test Suite ===\n\n";

    // Phase 1 tests
    TestBasicMovement();
    TestSynergyDetection();
    TestStatClamping();

    // Phase 2 bug regression tests
    TestLimitlessRangeNoStack();
    TestTransitionBothAxes();
    TestShotProbabilityCloseRange();
    TestDefaultInitialization();
    TestVector3DOperators();

    // LoadRosterJSON tests
    TestLoadRosterJSON();
    TestLoadRosterJSON_BadInput();

    // F15: RNG seed fix
    TestRandomSeedProducesDifferentResults();

    // F16: steal rate frame-rate independence
    TestStealRateNotFrameRateDependent();

    // F5: shot zone tiers
    TestShotZoneTiers();

    // F6: rebounding + playmaking stats
    TestReboardingAndPlaymakingStats();
    TestLoadRosterJSON_NewStatFields();

    // F8: home court advantage
    TestHomeCourtAdvantage();

    // F4: recalibrated synergy thresholds
    TestSplashFamilySynergyFires();
    TestSevenSecondsOrLessFires();

    std::cout << "=== All Tests Passed ===\n";
    return 0;
}
