#pragma once
#include <iostream>

enum class RoundType { STANDARD_5V5, PLAYGROUND_3V3, HORSE_MINIGAME, DRAFT_LOTTERY };

class GameState {
public:
    int currentSeason;
    int currentRound;
    int teamWins;
    int teamLosses;
    int currentCapSpace;
    static constexpr int SALARY_CAP = 151000000;
    static constexpr int SALARY_FLOOR = 136000000;

    GameState();

    RoundType DetermineNextRound();
    void ExecuteDraftLottery();
    void ProcessShopRoll(int costToRoll);
};
