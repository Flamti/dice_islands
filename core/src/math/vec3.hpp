#pragma once

#include <cmath>

// Минимальный вектор ядра. Никаких типов Godot ниже адаптера (ARCHITECTURE §1).

namespace dicecore::math {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}

inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(Vec3 a) {
    return std::sqrt(dot(a, a));
}

inline Vec3 normalized(Vec3 a) {
    const float len = length(a);
    if (len <= 0.0f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return a * (1.0f / len);
}

} // namespace dicecore::math
