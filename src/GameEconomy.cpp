#include "GameEconomy.h"

// Thresholds match scraper.py:_determine_cost() -- keep in sync
UnitCost GameEconomy::CalculateDraftCost(double playerSalary) {
    double capPercentage = playerSalary / CURRENT_SALARY_CAP;

    if (capPercentage >= 0.25) return UnitCost::FIVE;
    if (capPercentage >= 0.15) return UnitCost::FOUR;
    if (capPercentage >= 0.08) return UnitCost::THREE;
    if (capPercentage >= 0.03) return UnitCost::TWO;
    return UnitCost::ONE;
}

int StatNormalizer::ConvertZScoreToGameStat(double zScore) {
    double scaled = 50.0 + (zScore * 20.0);
    return std::clamp(static_cast<int>(std::round(scaled)), 1, 99);
}
