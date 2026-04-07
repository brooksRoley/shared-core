#include "PlayerEntity.h"
#include <cmath>
#include <iostream>

PlayerEntity::PlayerEntity(int id, std::string name, float speed, float shooting)
    : id(id), name(std::move(name)) {
    stats.speed = speed;
    stats.shooting = shooting;
}

PlayerEntity::PlayerEntity(std::string name, Position pos, PlayerStats s)
    : name(std::move(name)), position(pos), stats(s) {}

void PlayerEntity::AssignPlay(ActionType action, float targetX, float targetY) {
    plannedAction = action;
    targetLocation = Vector2D(targetX, targetY);
}

void PlayerEntity::UpdatePhysicsTick(float deltaTime) {
    if (plannedAction == ActionType::CUT_TO_BASKET || plannedAction == ActionType::SPOT_UP) {
        Vector2D direction = (targetLocation - pos).Normalize();
        float courtSpeed = (stats.speed / 100.0f) * 15.0f;
        velocity = direction * courtSpeed;

        if (pos.DistanceTo(targetLocation) > 0.5f) {
            pos = pos + (velocity * deltaTime);
        } else {
            plannedAction = ActionType::HOLD;
        }
    }
}

void PlayerEntity::UpdateTransition() {
    if (state != PlayerState::TRANSITION_TO_OFFENSE &&
        state != PlayerState::TRANSITION_TO_DEFENSE) return;

    float spd = (stats.speed / 100.0f) * 2.5f;

    // Move X axis
    if (std::abs(pos.x - targetLocation.x) > spd) {
        pos.x += (targetLocation.x > pos.x) ? spd : -spd;
    } else {
        pos.x = targetLocation.x;
    }

    // Move Y axis
    if (std::abs(pos.y - targetLocation.y) > spd) {
        pos.y += (targetLocation.y > pos.y) ? spd : -spd;
    } else {
        pos.y = targetLocation.y;
    }

    // Transition complete only when BOTH axes have arrived
    if (pos.x == targetLocation.x && pos.y == targetLocation.y) {
        state = (state == PlayerState::TRANSITION_TO_OFFENSE)
                ? PlayerState::OFFENSE : PlayerState::DEFENSE;
    }
}

void PlayerEntity::ClampStats() {
    stats.shooting = std::clamp(stats.shooting, 1.0f, 99.0f);
    stats.defense = std::clamp(stats.defense, 1.0f, 99.0f);
    stats.speed = std::clamp(stats.speed, 1.0f, 99.0f);
}

void PlayerEntity::SetPlayCoordinates(float offX, float offY, float defX, float defY) {
    offensivePlacement = {offX, offY};
    defensivePlacement = {defX, defY};
}

void PlayerEntity::ApplyLimitlessRange() {
    if (!hasLimitlessRange) {
        stats.shooting = std::min(stats.shooting + 20.0f, 99.0f);
        hasLimitlessRange = true;
        std::cout << name << " triggers 'Limitless Range'! Shooting buff applied.\n";
    }
}
