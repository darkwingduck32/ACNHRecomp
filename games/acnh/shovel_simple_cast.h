#pragma once

#include "shovel_target.h"

namespace nx::acnh {

enum class ShovelStepCastMask : uint32_t { height = 4, stage_item = 16, material = 0x67 };

struct ShovelStepCastQuery {
    ShovelTargetPoint center;
    float radius;
    ShovelStepCastMask mask;
};

template<class Memory, class Services>
uint32_t shovel_step_cast_stage_key(uint64_t base, bool center, const Memory& memory, Services& services) {
    const auto scene = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab2720));
    uint64_t stage = base + 0x54c04a0;
    if (scene && memory.template read<uint64_t>(scene + 0x330)) {
        const auto owner = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab2640));
        const auto index = memory.template read<uint32_t>(scene + 0x338);
        if (services.stage_hash(owner, index, center) == 0x758f7eaf)
            stage = memory.template read<uint64_t>(base + 0x4ac3a80);
    }
    return memory.template read<uint32_t>(stage + 0x38);
}

template<class Memory, class Probe>
uint32_t shovel_walk_step_cast(uint64_t base, ShovelStepCastQuery query, const Memory& memory, Probe probe) {
    if (const auto result = probe(query.center, true)) return result;
    if (std::fabs(query.radius) < 0x1p-23f) return 0;

    const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00);
    const auto step = memory.template read<uint32_t>(base + 0x5140230);
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const float dy = query.radius * 0.f;
    for (unsigned sector = 0; sector < 8; ++sector) {
        const uint32_t heading = step * sector;
        const auto row = trig + 16 * (heading >> 24);
        const float fraction = static_cast<float>(heading & 0xffffffu) * 0x1p-24f;
        const float dx = std::fma(read_float(row + 4), fraction, read_float(row)) * query.radius;
        const float dz = std::fma(read_float(row + 12), fraction, read_float(row + 8)) * query.radius;
        ShovelTargetPoint point{query.center.x + dx, query.center.y + dy, query.center.z + dz};
        if (const auto result = probe(point, false)) return result;
    }
    return 0;
}

template<class Memory, class Services>
uint32_t shovel_simple_step_cast(uint64_t base, ShovelStepCastQuery query, const Memory& memory, Services& services) {
    return shovel_walk_step_cast(base, query, memory, [&](ShovelTargetPoint& point, bool center) {
        if (!services.probe(point, query.mask, center)) return 0u;
        if (query.mask == ShovelStepCastMask::height)
            services.record_height_contact(point, shovel_step_cast_stage_key(base, center, memory, services), center);
        return static_cast<uint32_t>(query.mask);
    });
}

} // namespace nx::acnh
