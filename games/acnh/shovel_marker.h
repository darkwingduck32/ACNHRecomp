#pragma once

#include "shovel_terrain_helpers.h"

namespace nx::acnh {

struct ShovelMarkerQuery { uint64_t point; float radius; };

template<class Memory>
bool shovel_marker_in_mask(TerrainQuadrants corners, uint64_t mask, const Memory& memory) {
    for (const auto output : corners.output) {
        const auto kind = memory.template read<uint32_t>(output);
        if (kind < 73 && (memory.template read<uint32_t>(mask + 4 * (kind / 32)) & (1u << (kind % 32))))
            return true;
    }
    return false;
}

template<class Memory, class Services>
bool shovel_marker_match(uint64_t owner, uint64_t descriptor, uint64_t mask, Memory& memory, Services& services) {
    const auto corners = services.prepare_corners();
    return services.sample_corners({owner}, descriptor, corners) && shovel_marker_in_mask(corners, mask, memory);
}

template<class Memory, class Services>
bool shovel_marker_extent(uint64_t base, ShovelMarkerQuery query, const Memory& memory, Services& services) {
    const auto coordinate_type = memory.template read<uint64_t>(base + 0x4ab2010) + 0x10;
    const auto terrain_slot = memory.template read<uint64_t>(base + 0x4ab27c8);
    const auto as_tile = [](TerrainWorldPoint point) {
        const auto coord = [](float value) { return std::isnan(value) ? 0 : terrain_half_tile(value); };
        return HalfTile{coord(point.x), coord(point.z)};
    };
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const auto read_point = [&] {
        return TerrainWorldPoint{read_float(query.point), read_float(query.point + 8)};
    };
    const auto test_direct = [&](TerrainWorldPoint point, unsigned probe) {
        const auto owner = memory.template read<uint64_t>(terrain_slot);
        return owner && memory.template read<uint64_t>(owner + 0xa8)
            && services.test_direct({owner + 0x28}, as_tile(point), coordinate_type, probe);
    };

    if (test_direct(read_point(), 0)) return true;
    if (std::fabs(query.radius) <= 0x1p-23f) return false;
    auto point = read_point();
    point.x += query.radius * 0.f;
    point.z += query.radius;
    if (test_direct(point, 1)) return true;

    const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00);
    for (unsigned sector = 1; sector < 8; ++sector) {
        const uint32_t angle = memory.template read<uint32_t>(base + 0x5140230) * sector;
        const auto row = trig + 16 * (angle >> 24);
        const float fraction = static_cast<float>(angle & 0xffffffu) * 0x1p-24f;
        const float dx = std::fma(read_float(row + 4), fraction, read_float(row));
        const float dz = std::fma(read_float(row + 12), fraction, read_float(row + 8));
        point = read_point();
        point.x += dx * query.radius;
        point.z += dz * query.radius;
        if (sector < 4) {
            if (test_direct(point, sector + 1)) return true;
        } else if (services.test_global(as_tile(point), coordinate_type, sector - 4)) {
            return true;
        }
    }
    return false;
}

template<class Memory, class Services>
bool shovel_marker_global(uint64_t base, uint64_t descriptor, const Memory& memory, Services& services) {
    const auto owner = memory.template read<uint64_t>(base + 0x4bfae30);
    return owner && memory.template read<uint64_t>(owner + 0xa8)
        && services.test({owner + 0x28}, descriptor);
}

template<class Memory, class Services>
bool shovel_marker_corners(uint64_t owner, uint64_t descriptor, TerrainQuadrants corners, Memory& memory, Services& services) {
    const HalfTile tile{memory.template read<int32_t>(descriptor + 8), memory.template read<int32_t>(descriptor + 12)};
    bool sampled = false;
    if (tile.x >= 0 && tile.z >= 0) {
        const auto grid = memory.template read<uint64_t>(owner + 0x80);
        if (uint32_t(tile.x) < memory.template read<uint32_t>(grid + 0x38)
            && uint32_t(tile.z) < memory.template read<uint32_t>(grid + 0x3c)) {
            const auto kinds = services.sample_kinds({grid}, descriptor);
            for (size_t i = 0; i < kinds.size(); ++i)
                memory.template write<uint32_t>(corners.output[i], kinds[i]);
            sampled = true;
        }
    }

    const std::array<TerrainWorldPoint, 4> offsets{{{0, -1.25f}, {-1.25f, 1.25f},
                                                   {1.25f, 1.25f}, {1.25f, -1.25f}}};
    const HalfTile current{memory.template read<int32_t>(descriptor + 8), memory.template read<int32_t>(descriptor + 12)};
    TerrainWorldPoint point{std::fma(static_cast<float>(current.x), 5.f, 2.5f),
                            std::fma(static_cast<float>(current.z), 5.f, 2.5f)};
    for (size_t corner = 0; corner < offsets.size(); ++corner) {
        point.x += offsets[corner].x;
        point.z += offsets[corner].z;
        const auto kind = services.primary_kind(point, corner);
        if (kind != 7) memory.template write<uint32_t>(corners.output[corner], kind);
    }
    return sampled;
}

} // namespace nx::acnh
