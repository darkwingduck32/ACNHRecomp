#pragma once

#include "shovel_terrain.h"

namespace nx::acnh {

struct GridHeightQuery {
    TerrainGrid grid;
    uint64_t point;
    uint64_t material;
    uint64_t override_present;
    bool ground_only;
};

// Each sector stores the height at the tile center and at its two outer vertices.
// The diagonal and center coordinates are data so nonstandard terrain uses the same interpolation.
inline float terrain_sector_height(float center_height, float first_height, float second_height,
                                   float center, float diagonal, float x, float z, unsigned sector) {
    const float low = 0.f - center;
    const float high = diagonal - center;
    const float first = first_height - center_height;
    const float second = second_height - center_height;
    const float side = sector < 2 ? low : high;
    const bool low_x_edge = sector == 0 || sector == 3;
    const float edge_x = low_x_edge ? low : high;
    const float edge_z = low_x_edge ? high : low;
    float normal_x = std::fma(first, edge_x, -(side * second));
    float normal_y = std::fma(side, side, -(low * high));
    float normal_z = std::fma(edge_z, second, -(first * side));
    const float length = std::sqrt(std::fma(normal_z, normal_z,
        std::fma(normal_x, normal_x, normal_y * normal_y)));
    if (length > 0.f) {
        const float inverse = 1.f / length;
        normal_x *= inverse;
        normal_y *= inverse;
        normal_z *= inverse;
    }
    const float intercept = std::fma(normal_z, center,
        std::fma(normal_x, center, center_height * normal_y));
    constexpr float close = 0x1p-23f;
    if (std::fabs(normal_x) <= close && std::fabs(normal_z) <= close) return intercept;
    if (std::fabs(normal_y) <= close) return 0.f;
    return -(z * normal_z + x * normal_x - intercept) / normal_y;
}

template<class Memory>
float query_grid_height(GridHeightQuery query, Memory& memory) {
    memory.template write<uint8_t>(query.override_present, 0);
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const float world_x = read_float(query.point);
    const float world_z = read_float(query.point + 8);
    const auto coordinate = [](float value) {
        return std::isnan(value) ? 0 : terrain_half_tile(value);
    };
    const int32_t tile_x = coordinate(world_x);
    if (tile_x < 0) return 100.f;
    const int32_t tile_z = coordinate(world_z);
    if (tile_z < 0) return 100.f;
    const uint32_t width = memory.template read<uint32_t>(query.grid.address + 0x38);
    const uint32_t height = memory.template read<uint32_t>(query.grid.address + 0x3c);
    if (uint32_t(tile_x) >= width || uint32_t(tile_z) >= height) return 100.f;

    const auto grid = query.grid.address;
    const uint32_t index = width * uint32_t(tile_z) + uint32_t(tile_x);
    const int64_t signed_index = std::bit_cast<int32_t>(index);
    const uint64_t flags = memory.template read<uint64_t>(grid + 0x70);
    const bool flat = (memory.template read<uint32_t>(flags + 4 * signed_index)
                      & (1u << (query.ground_only ? 17 : 16))) != 0;
    const float x = world_x - static_cast<float>(tile_x) * 5.f;
    const float z = world_z - static_cast<float>(tile_z) * 5.f;
    const float diagonal = read_float(grid + 0x28);
    const bool along_x = z - x < 0.f;
    const bool near_origin = x + z - diagonal < 0.f;
    const unsigned sector = near_origin ? (along_x ? 0u : 1u) : (along_x ? 3u : 2u);

    const uint64_t overlay_table = memory.template read<uint64_t>(grid + 0x60);
    const bool has_overlay = overlay_table && index < memory.template read<uint32_t>(grid + 0x58);
    const uint64_t overlay = has_overlay ? overlay_table + 0x34 * signed_index : 0;
    const uint32_t overlay_material = has_overlay ? memory.template read<uint8_t>(overlay + 0x30 + sector) : 7;
    const bool use_overlay = !query.ground_only && overlay_material != 7;
    uint64_t ground = 0;
    if (!use_overlay || overlay_material == 9) {
        if (index < memory.template read<uint32_t>(grid + 0x48))
            ground = memory.template read<uint64_t>(grid + 0x50) + 0x34 * signed_index;
    }
    const uint32_t material = use_overlay && overlay_material != 9 ? overlay_material
        : memory.template read<uint8_t>(ground + 0x30 + sector);
    const uint64_t record = (use_overlay ? overlay : ground) + 12 * sector;
    const float first_height = read_float(record);
    const float result = flat ? first_height : terrain_sector_height(
        first_height, read_float(record + 4), read_float(record + 8), read_float(grid + 0x2c),
        diagonal, x, z, sector);
    memory.template write<uint8_t>(query.override_present, overlay_material != 7);
    memory.template write<uint32_t>(query.material, material);
    return result;
}

} // namespace nx::acnh
