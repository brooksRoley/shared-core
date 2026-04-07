#include "GameSeason.h"

GameState::GameState()
    : currentSeason(1), currentRound(1), teamWins(0), teamLosses(0), currentCapSpace(SALARY_CAP) {}

RoundType GameState::DetermineNextRound() {
    if (currentRound % 5 == 0) return RoundType::PLAYGROUND_3V3;
    if (currentRound % 7 == 0) return RoundType::DRAFT_LOTTERY;
    if (currentRound % 12 == 0) return RoundType::HORSE_MINIGAME;
    return RoundType::STANDARD_5V5;
}

void GameState::ExecuteDraftLottery() {
    std::cout << "--- DRAFT LOTTERY INITIATED ---\n";
    float winPercentage = (teamWins + teamLosses == 0)
        ? 0.5f
        : (float)teamWins / (teamWins + teamLosses);

    if (winPercentage < 0.3f) {
        std::cout << "Your team is tanking! Granted 1st overall pick. Legendary unit pool unlocked.\n";
    } else {
        std::cout << "Your team is winning. Granted late round pick.\n";
    }
}

void GameState::ProcessShopRoll(int costToRoll) {
    if (currentCapSpace >= costToRoll) {
        currentCapSpace -= costToRoll;
        std::cout << "Rerolling shop...\n";
    }
}
