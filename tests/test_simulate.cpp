#include <cassert>
#include <iostream>
#include <string>
#include "GameManager.h"

int main() {
    GameManager gm;

    // Spawn 5 home players
    gm.SpawnPlayer(1, "Home PG", 80.0f, 85.0f);
    gm.SpawnPlayer(2, "Home SG", 75.0f, 90.0f);
    gm.SpawnPlayer(3, "Home SF", 70.0f, 75.0f);
    gm.SpawnPlayer(4, "Home PF", 65.0f, 60.0f);
    gm.SpawnPlayer(5, "Home C",  55.0f, 50.0f);

    // Place players on court
    for (int i = 1; i <= 5; i++) {
        gm.SetPlayerCoordinates(i, float(i % 3), float(i / 3), float(i % 3), float(i / 3));
    }

    // SimulateGame: run full sim, return JSON with final scores
    std::string result = gm.SimulateGame(42, 600);
    std::cout << "SimulateGame result: " << result << std::endl;

    // Parse result — should contain homeScore and awayScore
    assert(result.find("homeScore") != std::string::npos);
    assert(result.find("awayScore") != std::string::npos);
    assert(result.find("simTicks") != std::string::npos);

    // Run again with same seed — should be deterministic
    GameManager gm2;
    gm2.SpawnPlayer(1, "Home PG", 80.0f, 85.0f);
    gm2.SpawnPlayer(2, "Home SG", 75.0f, 90.0f);
    gm2.SpawnPlayer(3, "Home SF", 70.0f, 75.0f);
    gm2.SpawnPlayer(4, "Home PF", 65.0f, 60.0f);
    gm2.SpawnPlayer(5, "Home C",  55.0f, 50.0f);
    for (int i = 1; i <= 5; i++) {
        gm2.SetPlayerCoordinates(i, float(i % 3), float(i / 3), float(i % 3), float(i / 3));
    }
    std::string result2 = gm2.SimulateGame(42, 600);
    assert(result == result2); // Same seed = same result

    std::cout << "All SimulateGame tests passed!\n";
    return 0;
}
