#pragma once

#include <array>
#include <cmath>

namespace nx::acnh {

using Position = std::array<float, 3>;

struct ApproachResult {
    Position position;
    float remaining;
    bool wrote;
};

inline float position_length(const Position& delta) {
    const float xy = std::fma(delta[0], delta[0], delta[1] * delta[1]);
    return std::sqrt(std::fma(delta[2], delta[2], xy));
}

// This path takes disjoint positions and finite values in the validated range.
inline ApproachResult approach_position(Position position, const Position& target,
                                        float gain, float maximum, float minimum) {
    if (position == target) return {position, 0.f, false};

    const Position delta{position[0] - target[0], position[1] - target[1], position[2] - target[2]};
    const float distance = position_length(delta);
    const float step = distance * gain;
    if (distance < minimum || std::fabs(step) <= 0x1p-23f) return {target, 0.f, true};

    Position movement{delta[0] * gain, delta[1] * gain, delta[2] * gain};
    if (step > maximum || step < minimum) {
        const float scale = (step > maximum ? maximum : minimum) / step;
        for (float& component : movement) component *= scale;
    }

    for (size_t i = 0; i < position.size(); ++i) position[i] -= movement[i];
    return {position, position_length({position[0] - target[0], position[1] - target[1],
                                       position[2] - target[2]}), true};
}

}  // namespace nx::acnh
