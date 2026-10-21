#include "Basketball.h"

void Basketball::UpdatePhysics(float deltaTime) {
    if (!isPossessed) {
        position = position + (velocity * deltaTime);
        if (position.z > 0) {
            velocity.z -= GRAVITY * deltaTime;
        } else {
            position.z = 0;
            velocity.z = -velocity.z * 0.6f;
        }
    }
}
