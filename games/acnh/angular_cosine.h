#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace nx::acnh {

struct CosineSample {
    float base;
    float slope;
};

inline float angular_cosine(uint32_t a, uint32_t b, std::span<const CosineSample, 129> table) {
    const uint32_t turn = a - b;
    const uint32_t distance = std::min(turn, 0u - turn);
    const CosineSample sample = table[distance >> 24];
    const float delta = sample.slope * static_cast<float>(distance & 0x00ffffff);
    return sample.base + delta * 0x1p-24f;
}

}  // namespace nx::acnh
