#pragma once

#include <cmath>
#include <cstdint>
#include <span>

namespace nx::acnh {

struct HeadingSample {
    uint32_t angle;
    float slope;
};

// Finite inputs only. The result is an unsigned turn with zero pointing along +Z.
inline uint32_t heading(float x, float z, std::span<const HeadingSample, 129> table) {
    constexpr uint32_t quarter_turn = 0x40000000u;
    const float x_magnitude = std::fabs(x);
    const float z_magnitude = std::fabs(z);
    const bool near_z_axis = z_magnitude >= x_magnitude;
    const float major = near_z_axis ? z_magnitude : x_magnitude;
    if (major == 0.f) return 0;

    const float minor = near_z_axis ? x_magnitude : z_magnitude;
    const float scaled = (minor / major) * 128.f;
    const uint32_t index = static_cast<uint32_t>(scaled);
    const HeadingSample sample = table[index];
    const float fraction = scaled - static_cast<float>(index);
    const uint32_t angle = sample.angle + static_cast<uint32_t>(fraction * sample.slope);

    if (near_z_axis) {
        const uint32_t axis = z < 0.f ? 2 * quarter_turn : 0;
        return (x < 0.f) == (z < 0.f) ? axis + angle : axis - angle;
    }
    const uint32_t axis = x < 0.f ? 3 * quarter_turn : quarter_turn;
    return (x < 0.f) == (z < 0.f) ? axis - angle : axis + angle;
}

}  // namespace nx::acnh
