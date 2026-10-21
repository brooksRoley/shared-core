#pragma once
#include "Vector.h"

class Basketball {
public:
    Vector3D position{0, 0, 0};
    Vector3D velocity{0, 0, 0};
    bool isPossessed = false;
    int possessorId = -1;

    // Sim units: court pixels horizontally, abstract height units for z
    static constexpr float GRAVITY = 30.0f;

    void UpdatePhysics(float deltaTime);
};
