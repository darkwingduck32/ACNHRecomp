#pragma once

#include "shovel_target.h"

namespace nx::acnh {

struct ShovelTraversalPeakQuery {
    uint64_t origin;
};

template<class Memory, class Services>
ShovelTargetPoint shovel_traversal_peak(ShovelTraversalPeakQuery query,
                                       const Memory& memory, Services& services) {
    const auto read = [&](uint64_t offset) {
        return std::bit_cast<float>(memory.template read<uint32_t>(query.origin + offset));
    };
    auto height = services.origin_height(query.origin);
    ShovelTargetPoint best{read(0), read(4), read(8)};

    constexpr float radius = 0.2f;
    constexpr float diagonal = std::bit_cast<float>(0x3e10cb2au);
    constexpr std::array<std::array<float, 2>, 9> offsets{{
        {0.f, 0.f}, {radius, 0.f}, {0.f, radius}, {-radius, 0.f}, {0.f, -radius},
        {diagonal, diagonal}, {-diagonal, diagonal}, {-diagonal, -diagonal}, {diagonal, -diagonal},
    }};
    for (size_t index = 0; index < offsets.size(); ++index) {
        const auto& offset = offsets[index];
        ShovelTargetPoint sample{read(0) + offset[0], read(4) + 0.f, read(8) + offset[1]};
        const auto candidate = services.sample_height(sample, index);
        if (candidate > height) {
            height = candidate;
            best = {sample.x, height, sample.z};
        }
    }
    return best;
}

} // namespace nx::acnh
