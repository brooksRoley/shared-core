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

    std::cout << "=== All Tests Passed ===\n";
    return 0;
}
