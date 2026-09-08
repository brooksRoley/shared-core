#include "ShotProbability.h"
#include <cmath>
#include <algorithm>

static float ComputeShotProb(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos, float effectiveDefense) {
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

    // Contest penalty — scaled by the defender's effective defense stat
    float contestPenalty = 0.0f;
    if (defenderProximity < 5.0f) {
        float defenseScale = effectiveDefense / 50.0f;  // normalize around league-average 50
        contestPenalty = (5.0f - defenderProximity) * 0.1f * defenseScale;
    }

    return std::clamp(baseProb - contestPenalty, 0.0f, 1.0f);
}

float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos) {
    return ComputeShotProb(shooter, nearestDefender, hoopPos, nearestDefender->stats.defense);
}

float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos, float boardAvgDefense) {
    // Cap the defender's effective defense at MAX_DEFENSE_MULTIPLIER * board average
    float cap = boardAvgDefense * MAX_DEFENSE_MULTIPLIER;
    float effectiveDefense = std::min(nearestDefender->stats.defense, cap);
    return ComputeShotProb(shooter, nearestDefender, hoopPos, effectiveDefense);
}
