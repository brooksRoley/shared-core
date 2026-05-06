#pragma once
#include <string>
#include <memory>
#include <algorithm>
#include "Vector.h"

struct PlayerStats {
    float shooting = 50.0f;
    float defense = 50.0f;
    float speed = 50.0f;
    int height_inches = 72;
    int weight_lbs = 200;
    int stamina = 100;
    float rebounding = 50.0f;   // affects rebound priority
    float playmaking = 50.0f;   // affects pass probability
};

enum class Position { PG, SG, SF, PF, C };
enum class ActionType { HOLD, CUT_TO_BASKET, SET_SCREEN, SPOT_UP };
enum class PlayerState { OFFENSE, DEFENSE, TRANSITION_TO_OFFENSE, TRANSITION_TO_DEFENSE };

class PlayerEntity {
public:
    int id = 0;
    std::string name;
    std::string team;
    Position position = Position::PG;
    PlayerStats stats;
    int cost = 1;
    int currentHealth = 100;

    // Spatial state
    Vector2D pos{0, 0};
    Vector2D velocity{0, 0};
    Vector2D targetLocation{0, 0};
    PlayerState state = PlayerState::OFFENSE;
    ActionType plannedAction = ActionType::HOLD;

    // Formation placement coordinates
    Vector2D offensivePlacement{0, 0};
    Vector2D defensivePlacement{0, 0};

    // Ability tracking
    bool hasLimitlessRange = false;

    PlayerEntity() = default;
    PlayerEntity(int id, std::string name, float speed, float shooting);
    PlayerEntity(std::string name, Position pos, PlayerStats s);

    void AssignPlay(ActionType action, float targetX, float targetY);
    void UpdatePhysicsTick(float deltaTime);
    void UpdateTransition();
    void ClampStats();
    void SetPlayCoordinates(float offX, float offY, float defX, float defY);
    void ApplyLimitlessRange();
};
