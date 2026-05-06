#pragma once
#include <cmath>
#include <algorithm>

enum class UnitCost { ONE = 1, TWO = 2, THREE = 3, FOUR = 4, FIVE = 5 };

struct RawStats {
    double points, rebounds, assists, speed_metric, salary;
};

class GameEconomy {
public:
    // Thresholds match scraper.py:_determine_cost() -- keep in sync
    UnitCost CalculateDraftCost(double playerSalary);

private:
    static constexpr double CURRENT_SALARY_CAP = 151000000.0;
};

class StatNormalizer {
public:
    int ConvertZScoreToGameStat(double zScore);
};
