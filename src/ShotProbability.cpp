#include "ShotProbability.h"
#include <cmath>
#include <algorithm>

float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos) {
    float distToHoop = shooter->pos.DistanceTo(hoopPos);
    float defenderProximity = shooter->pos.DistanceTo(nearestDefender->pos);
    float skill = shooter->stats.shooting / 100.0f;

    float baseProb;
    if (distToHoop < 8.0f) {
        // Paint: ~60% league average at rim
        baseProb = 0.60f * skill;
    } else if (distToHoop < 22.0f) {
        // Midrange: ~42% league average
        float midDecay = 1.0f - (distToHoop - 8.0f) / 60.0f;
        baseProb = 0.42f * skill * std::max(0.7f, midDecay);
    } else {
        // Three-point: ~36% league average
        float threeDecay = 1.0f - (distToHoop - 22.0f) / 80.0f;
        baseProb = 0.36f * skill * std::max(0.6f, threeDecay);
    }

    float contestPenalty = 0.0f;
    if (defenderProximity < 5.0f) {
        contestPenalty = (5.0f - defenderProximity) * 0.1f;
    }

    return std::clamp(baseProb - contestPenalty, 0.0f, 1.0f);
}
