#pragma once

#include "shovel_terrain.h"

#include <cmath>

namespace nx::acnh {

enum class TerrainHeightMode { composed, base_only };
struct TerrainQuadrants { std::array<uint64_t, 4> output; };
struct TerrainWorldPoint { float x, z; };
enum class TerrainFallback { primary, secondary };

template<class Memory, class Services>
uint32_t terrain_fallback_kind(uint64_t base, TerrainWorldPoint& point, TerrainFallback fallback,
                              const Memory& memory, Services& services) {
    const bool secondary = fallback == TerrainFallback::secondary;
    uint32_t kind = 7;
    if (secondary) {
        const auto coordinate = [](float value) {
            return std::isnan(value) ? 0 : terrain_half_tile(value);
        };
        if (services.has_field_material(HalfTile{coordinate(point.x), coordinate(point.z)}))
            kind = 0x34;
    }

    if (!memory.template read<uint64_t>(base + 0x5192338)) return kind;
    const auto context = memory.template read<uint64_t>(base + 0x59ed670);
    if (!context || !memory.template read<uint64_t>(context + 0x330)) return kind;
    const uint64_t name_slot = secondary ? base + 0x59ed668
        : memory.template read<uint64_t>(base + 0x4ab2640);
    const auto object = memory.template read<uint64_t>(name_slot);
    const auto index = memory.template read<uint32_t>(context + 0x338);
    if (services.stage_hash(object, index, fallback) != 0x758f7eaf) return kind;

    const auto current_context = memory.template read<uint64_t>(base + 0x59ed670);
    if (!current_context) return kind;
    const auto stage = memory.template read<uint64_t>(current_context + 0x330);
    if (!stage || !memory.template read<uint32_t>(stage + 0x244)) return kind;
    const auto owner = memory.template read<uint64_t>(base + 0x4bfae30);
    if (!owner || !memory.template read<uint64_t>(owner + 0xa8)
        || memory.template read<uint8_t>(owner + 0x11c8)) return kind;
    const uint32_t material = services.resolve_kind({owner}, point, fallback);
    return material == 7 ? kind : material;
}

template<class Memory>
bool terrain_grid_has_material(uint64_t base, FieldGrid grid, HalfTile point, const Memory& memory) {
    if (!grid) return false;
    const auto record = tile_record(base + 0x50a50f0, grid.address,
                                    half_tile_coordinate(point.x), half_tile_coordinate(point.z), memory);
    return !(memory.template read<uint8_t>(record + 0xe) & 0x20)
        && memory.template read<uint8_t>(record + 0x1e) != 0xff;
}

template<class Memory>
bool terrain_has_field_material(uint64_t base, HalfTile point, Memory& memory) {
    const FieldQueries fields{base, memory};
    return terrain_grid_has_material(base, fields.grid_for({0}), point, memory);
}

template<class Memory>
bool terrain_layer_one_has_material(uint64_t base, HalfTile point, const Memory& memory) {
    const uint64_t owner_slot = memory.template read<uint64_t>(base + 0x4ab27a8);
    const uint64_t owner = memory.template read<uint64_t>(owner_slot);
    const uint64_t node = memory.template read<uint64_t>(owner + 0x38);
    const FieldGrid grid{node ? memory.template read<uint64_t>(node + 0xb0) : 0};
    return terrain_grid_has_material(base, grid, point, memory);
}

enum class TerrainSetupRoute { layer_zero, overlay, layer_one };

template<class Memory, class Services>
uint32_t shovel_terrain_setup(uint64_t base, HalfTileRef point, const Memory& memory, Services& services) {
    const uint64_t terrain = base + 0x4bfae30;
    if (!memory.template read<uint64_t>(terrain)) return 0;

    uint64_t selected = base + 0x54c04a0;
    const uint64_t context_slot = memory.template read<uint64_t>(base + 0x4ab2720);
    const uint64_t context = memory.template read<uint64_t>(context_slot);
    if (context && memory.template read<uint64_t>(context + 0x330)) {
        const uint64_t object = memory.template read<uint64_t>(base + 0x59ed668);
        const uint32_t index = memory.template read<uint32_t>(context + 0x338);
        if (services.stage_hash(object, index) == 0x758f7eaf)
            selected = memory.template read<uint64_t>(base + 0x4ac3a80);
    }
    if (memory.template read<uint32_t>(selected + 0x38) == 0xe820ff3d)
        return services.test_material(point, TerrainSetupRoute::layer_zero);
    const uint64_t alternate = memory.template read<uint64_t>(base + 0x4ac3a80);
    if (memory.template read<uint32_t>(alternate + 0x38) == 0xdb5d97b8)
        return services.test_material(point, TerrainSetupRoute::layer_zero);

    const uint64_t owner = memory.template read<uint64_t>(terrain);
    const bool same_grid = memory.template read<uint64_t>(owner + 0xa8)
                         == memory.template read<uint64_t>(owner + 0x90);
    return services.test_material(point, same_grid ? TerrainSetupRoute::layer_one : TerrainSetupRoute::overlay);
}

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
