#pragma once

#include "shovel_terrain.h"

#include <cmath>

namespace nx::acnh {

enum class TerrainHeightMode { composed, base_only };
struct TerrainQuadrants { std::array<uint64_t, 4> output; };
struct TerrainWorldPoint { float x, z; };

template<class Memory, class Services>
bool terrain_is_level(TerrainGrid grid, TerrainRect area, TerrainHeightMode mode,
                      Memory& memory, Services& services) {
    float lowest = 0.0f;
    float highest = 0.0f;
    bool has_sample = false;
    for (int64_t z = area.min.z; z <= area.max.z; ++z) {
        for (int64_t x = area.min.x; x <= std::max(area.min.x, area.max.x); ++x) {
            if (x < 0 || z < 0) continue;
            const auto width = memory.template read<uint32_t>(grid.address + 0x38);
            if (x >= width || z >= memory.template read<uint32_t>(grid.address + 0x3c)) continue;

            const auto index = width * uint32_t(z) + uint32_t(x);
            const auto flags = memory.template read<uint64_t>(grid.address + 0x70);
            const uint32_t required_bit = mode == TerrainHeightMode::base_only ? 2 : 1;
            if (!(memory.template read<uint32_t>(flags + 4ll * std::bit_cast<int32_t>(index)) & required_bit))
                return false;

            float height;
            if (mode == TerrainHeightMode::base_only) {
                const auto ground = memory.template read<uint64_t>(grid.address + 0x50);
                const auto count = memory.template read<uint32_t>(grid.address + 0x48);
                const uint64_t record = index < count ? ground + 0x34ll * std::bit_cast<int32_t>(index) : 0;
                height = std::bit_cast<float>(memory.template read<uint32_t>(record));
            } else {
                height = services.sample_height(grid, {int32_t(x), int32_t(z)});
            }

            lowest = has_sample && lowest < height ? lowest : height;
            highest = has_sample && highest > height ? highest : height;
            if (highest - lowest > std::bit_cast<float>(0x3de147aeu)) return false;
            has_sample = true;
        }
    }
    return true;
}

template<class Memory>
uint64_t terrain_overlay_row(TerrainGrid grid, HalfTile tile, Memory& memory) {
    if (!grid.address || tile.x < 0 || tile.z < 0) return 0;
    const auto width = memory.template read<uint32_t>(grid.address + 0x38);
    if (uint32_t(tile.x) >= width ||
        uint32_t(tile.z) >= memory.template read<uint32_t>(grid.address + 0x3c)) return 0;
    const auto rows = memory.template read<uint64_t>(grid.address + 0x60);
    if (!rows) return 0;
    const auto index = width * uint32_t(tile.z) + uint32_t(tile.x);
    if (index >= memory.template read<uint32_t>(grid.address + 0x58)) return 0;
    return rows + 0x34ll * std::bit_cast<int32_t>(index);
}

template<class Memory, class Services>
bool terrain_quadrant_materials(uint64_t base, TerrainGrid grid, HalfTile tile,
                               TerrainQuadrants quadrants, Memory& memory, Services& services) {
    bool sampled = false;
    if (tile.x >= 0 && tile.z >= 0) {
        const auto width = memory.template read<uint32_t>(grid.address + 0x38);
        const auto height = memory.template read<uint32_t>(grid.address + 0x3c);
        if (uint32_t(tile.x) < width && uint32_t(tile.z) < height) {
            const auto kinds = services.sample_kinds(grid, tile);
            for (size_t corner = 0; corner < kinds.size(); ++corner)
                memory.template write<uint32_t>(quadrants.output[corner], kinds[corner]);
            sampled = true;
        }
    }

    const uint64_t owner = memory.template read<uint64_t>(base + 0x4bfae30);
    const TerrainGrid overlay{owner ? memory.template read<uint64_t>(owner + 0xa8) : 0};
    const uint64_t row = terrain_overlay_row(overlay, tile, memory);
    const std::array<TerrainWorldPoint, 4> offsets{{{0, -1.25f}, {-1.25f, 1.25f},
                                                    {1.25f, 1.25f}, {1.25f, -1.25f}}};
    TerrainWorldPoint point{std::fma(static_cast<float>(tile.x), 5.0f, 2.5f),
                            std::fma(static_cast<float>(tile.z), 5.0f, 2.5f)};
    std::array<uint8_t, 4> kinds{7, 7, 7, 7};
    if (row)
        for (size_t corner = 0; corner < kinds.size(); ++corner)
            kinds[corner] = memory.template read<uint8_t>(row + 0x30 + corner);

    for (size_t corner = 0; corner < offsets.size(); ++corner) {
        point.x += offsets[corner].x;
        point.z += offsets[corner].z;
        if (kinds[corner] != 7) continue;
        const auto apply = [&](uint32_t kind) {
            if (kind != 7) memory.template write<uint32_t>(quadrants.output[corner], kind);
        };
        apply(services.primary_kind(point, corner));
        apply(services.secondary_kind(point, corner));
    }
    return sampled;
}

}  // namespace nx::acnh
