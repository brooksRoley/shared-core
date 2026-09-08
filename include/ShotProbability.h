#pragma once
#include "PlayerEntity.h"

// Single-card defense dominance cap: no defender's effective defense
// can exceed this multiple of the board's average defense stat.
static constexpr float MAX_DEFENSE_MULTIPLIER = 1.8f;

float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos);
float CalculateShotProbability(PlayerEntity* shooter, PlayerEntity* nearestDefender, Vector2D hoopPos, float boardAvgDefense);
