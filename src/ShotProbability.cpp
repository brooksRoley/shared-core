#include "ShotProbability.h"
#include <cmath>
#include <algorithm>

float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos) {
    float distToHoop = shooter->pos.DistanceTo(hoopPos);
    float defenderProximity = shooter->pos.DistanceTo(nearestDefender->pos);

    // Exponential decay prevents exceeding 1.0 at close range
    float baseProb = (shooter->stats.shooting / 100.0f) * std::exp(-distToHoop * 0.05f);

    // Contest penalty
    float contestPenalty = 0.0f;
    if (defenderProximity < 5.0f) {
        contestPenalty = (5.0f - defenderProximity) * 0.1f;
    }

    return std::clamp(baseProb - contestPenalty, 0.0f, 1.0f);
}
