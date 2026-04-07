#pragma once
#include <cmath>

struct Vector2D {
    float x, y;

    Vector2D(float _x = 0, float _y = 0) : x(_x), y(_y) {}

    Vector2D operator+(const Vector2D& v) const { return Vector2D(x + v.x, y + v.y); }
    Vector2D operator-(const Vector2D& v) const { return Vector2D(x - v.x, y - v.y); }
    Vector2D operator*(float scalar) const { return Vector2D(x * scalar, y * scalar); }

    float Magnitude() const { return std::sqrt(x*x + y*y); }
    Vector2D Normalize() const {
        float mag = Magnitude();
        return mag > 0 ? Vector2D(x/mag, y/mag) : Vector2D(0,0);
    }
    float DistanceTo(const Vector2D& v) const { return (*this - v).Magnitude(); }
};

struct Vector3D {
    float x, y, z;

    Vector3D(float _x = 0, float _y = 0, float _z = 0) : x(_x), y(_y), z(_z) {}

    Vector3D operator+(const Vector3D& v) const { return Vector3D(x + v.x, y + v.y, z + v.z); }
    Vector3D operator-(const Vector3D& v) const { return Vector3D(x - v.x, y - v.y, z - v.z); }
    Vector3D operator*(float scalar) const { return Vector3D(x * scalar, y * scalar, z * scalar); }

    float Magnitude() const { return std::sqrt(x*x + y*y + z*z); }
};
