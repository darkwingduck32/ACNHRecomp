#pragma once

#include "field_queries.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <utility>

namespace nx::acnh {

struct TerrainRect {
    HalfTile min;
    HalfTile max;
};

struct TerrainGrid { uint64_t address; };
struct TerrainOwner { uint64_t address; };

struct TerrainSample { uint64_t address; };

template<class Memory>
void sample_terrain(TerrainGrid grid, HalfTile tile, TerrainSample output, Memory& memory) {
    const auto width = memory.template read<uint32_t>(grid.address + 0x38);
    const uint32_t index = width * uint32_t(tile.z) + uint32_t(tile.x);
    const auto displacement = 0x34ll * std::bit_cast<int32_t>(index);
    const uint64_t overlay_start = memory.template read<uint64_t>(grid.address + 0x60);
    const auto overlay_count = memory.template read<uint32_t>(grid.address + 0x58);
    const uint64_t ground_start = memory.template read<uint64_t>(grid.address + 0x50);
    const auto ground_count = memory.template read<uint32_t>(grid.address + 0x48);
    const uint64_t ground = index < ground_count ? ground_start + displacement : 0;
    const uint64_t overlay = index < overlay_count && overlay_start ? overlay_start + displacement : 0;

    for (unsigned corner = 0; corner < 4; ++corner) {
        const unsigned record_offset = 12 * corner;
        const unsigned kind_offset = 0x30 + corner;
        const bool use_overlay = overlay && memory.template read<uint8_t>(overlay + kind_offset) != 7;
        const uint64_t record = use_overlay ? overlay : ground;
        const auto lower = memory.template read<uint64_t>(record + record_offset);
        const auto upper = memory.template read<uint32_t>(record + record_offset + 8);
        memory.template write<uint32_t>(output.address + record_offset + 8, upper);
        memory.template write<uint64_t>(output.address + record_offset, lower);
        const auto kind = memory.template read<uint8_t>(record + kind_offset);
        memory.template write<uint8_t>(output.address + kind_offset, kind);
        if (use_overlay && kind == 9)
            memory.template write<uint8_t>(output.address + kind_offset,
                memory.template read<uint8_t>(ground + kind_offset));
    }
}

inline int32_t terrain_half_tile(float point) {
    constexpr auto min = std::numeric_limits<int32_t>::min();
    constexpr auto max = std::numeric_limits<int32_t>::max();
    const int32_t truncated = point >= static_cast<float>(max) ? max
        : point <= static_cast<float>(min) ? min : static_cast<int32_t>(point);
    return static_cast<int32_t>(static_cast<float>(truncated) / 5.0f) - (point < 0.0f);
}

inline TerrainRect shovel_terrain_rect(HalfTile tile) {
    const auto limits = [](int32_t axis) {
        const float position = static_cast<float>(axis) * 10.0f;
        const float far_edge = position + 10.0f;
        const auto lower = terrain_half_tile(position + 2.5f);
        const auto upper = terrain_half_tile(far_edge - 2.5f);
        return std::pair{std::min(lower, upper), std::max(lower, upper)};
    };
    const auto [min_x, max_x] = limits(tile.x);
    const auto [min_z, max_z] = limits(tile.z);
    return {{min_x, min_z}, {max_x, max_z}};
}

template<class Memory, class Services>
bool can_dig_terrain(uint64_t base, HalfTile tile, Memory& memory, Services& services) {
    const auto area = shovel_terrain_rect(tile);
    const auto owner_slot = memory.template read<uint64_t>(base + 0x4ab27c8);
    const TerrainOwner first{memory.template read<uint64_t>(owner_slot)};
    if (!first.address) return false;
    const TerrainGrid grid{memory.template read<uint64_t>(first.address + 0xa8)};
    if (!grid.address || !services.grid_allows(grid, area)) return false;

    const auto table_slot = memory.template read<uint64_t>(base + 0x4ab46c8);
    for (int32_t z = area.min.z; z <= area.max.z; ++z) {
        for (int32_t x = area.min.x; x <= area.max.x; ++x) {
            const TerrainOwner owner{memory.template read<uint64_t>(owner_slot)};
            if (!owner.address) return false;
            const TerrainGrid current{memory.template read<uint64_t>(owner.address + 0xa8)};
            if (!current.address) return false;
            if (x >= 0 && z >= 0) {
                const auto width = memory.template read<uint32_t>(current.address + 0x38);
                const auto height = memory.template read<uint32_t>(current.address + 0x3c);
                if (uint32_t(x) < width && uint32_t(z) < height) {
                    const auto index = width * uint32_t(z) + uint32_t(x);
                    const auto count = memory.template read<uint32_t>(current.address + 0x68);
                    const auto flags = memory.template read<uint64_t>(current.address + 0x70);
                    if (index < count && flags &&
                        (memory.template read<uint32_t>(flags + 4ll * std::bit_cast<int32_t>(index)) & 4))
                        return false;
                }
            }
            const HalfTile half_tile{x, z};
            std::array<uint32_t, 4> materials{};
            if (!services.quadrant_materials(owner, half_tile, materials)) return false;
            for (size_t corner = 0; corner < materials.size(); ++corner) {
                const TerrainTable table{memory.template read<uint64_t>(table_slot)};
                if (!services.material_allows(table, materials[corner], corner)) return false;
            }
            if (services.layer_blocks(half_tile)) return false;
        }
    }
    return true;
}

}  // namespace nx::acnh
