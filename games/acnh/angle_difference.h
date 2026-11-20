#pragma once

#include <cmath>
#include <cstdint>

namespace nx::acnh {

struct AngleDifference {
    int32_t direction;
    float distance;
};

inline float normalize_degrees(float degrees) {
    constexpr float full_turn = 360.f;
    if (degrees >= 0.f && degrees <= full_turn) return degrees;

    float turns = degrees / full_turn;
    if (degrees < 0.f) turns -= 1.f;
    // AArch64 saturates the conversion to int32_t, then rounds back to float.
    // At this limit both INT32_MAX and INT32_MAX + 1 round to the same float.
    constexpr float limit = 0x1p31f;
    const float whole_turns = std::fabs(turns) >= limit
        ? std::copysign(limit, turns) : static_cast<float>(static_cast<int32_t>(turns));
    return std::fma(-whole_turns, full_turn, degrees);
}

// Finite inputs only. Direction selects how to turn from first toward second.
inline AngleDifference angle_difference(float first, float second) {
    first = normalize_degrees(first);
    second = normalize_degrees(second);
    if (first == second) return {};

    const bool increasing = second > first;
    const float distance = increasing ? second - first : first - second;
    const int32_t direction = increasing ? 1 : -1;
    if (distance > 180.f) return {-direction, 360.f - distance};
    return {direction, distance};
}

}  // namespace nx::acnh
