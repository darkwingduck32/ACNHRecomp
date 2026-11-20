#pragma once

#include "shovel_target.h"

namespace nx::acnh {

struct ShovelObstacleQuery { uint64_t point; float radius; };

template<class Memory, class Services>
bool shovel_point_obstructed(uint64_t base, ShovelObstacleQuery query, const Memory& memory, Services& services) {
    constexpr uint32_t blocked_material = 0x1c;
    const auto owner_slot = memory.template read<uint64_t>(base + 0x4ab27c8);
    const auto owner = [&] { return memory.template read<uint64_t>(owner_slot) + 0x28; };
    if (services.material(owner(), query.point, 0) == blocked_material) return true;
    if (std::fabs(query.radius) <= 0x1p-23f) return false;

    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const float zero_offset = query.radius * 0.f;
    const auto probe = [&](float x_offset, float z_offset, unsigned index) {
        const ShovelTargetPoint point{read_float(query.point) + x_offset,
                                     read_float(query.point + 4) + zero_offset,
                                     read_float(query.point + 8) + z_offset};
        return services.material(owner(), point, index) == blocked_material;
    };
    if (probe(zero_offset, query.radius, 1)) return true;

    const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00);
    for (unsigned sector = 1; sector < 8; ++sector) {
        const uint32_t heading = memory.template read<uint32_t>(base + 0x5140230) * sector;
        const auto row = trig + 16 * (heading >> 24);
        const float fraction = static_cast<float>(heading & 0xffffffu) * 0x1p-24f;
        const float sine = std::fma(read_float(row + 4), fraction, read_float(row));
        const float cosine = std::fma(read_float(row + 12), fraction, read_float(row + 8));
        if (probe(sine * query.radius, cosine * query.radius, sector + 1)) return true;
    }
    return false;
}

} // namespace nx::acnh
