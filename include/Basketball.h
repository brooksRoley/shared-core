#pragma once
#include "Vector.h"

class Basketball {
public:
    Vector3D position{0, 0, 0};
    Vector3D velocity{0, 0, 0};
    bool isPossessed = false;
    int possessorId = -1;

    // What an unheld ball is doing. The client renders from it and tells a
    // caught pass from a missed shot by it: NONE = loose (bouncing, rolling,
    // up for grabs), SHOT = on its way to the rim, PASS = on its way to a
    // teammate.
    enum class Flight { NONE, SHOT, PASS };
    Flight flight = Flight::NONE;

    // Sim units: court pixels horizontally, abstract height units for z
    static constexpr float GRAVITY = 30.0f;

    void UpdatePhysics(float deltaTime);
};
