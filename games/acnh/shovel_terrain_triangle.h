#pragma once

#include "shovel_terrain.h"

namespace nx::acnh {

enum class TerrainTriangleTest { flat, sloped };

struct TerrainTriangleQuery {
    TerrainOwner owner;
    HalfTile tile;
    uint8_t quadrant;
    TerrainTriangleTest test;
};

template<class Memory>
bool shovel_terrain_triangle(TerrainTriangleQuery query, const Memory& memory) {
    const auto root = query.owner.address;
    const auto [x, z] = query.tile;
    if (x < 0 || z < 0) return false;
    const auto columns = memory.template read<uint32_t>(root + 0x38);
    if (uint32_t(x) >= columns || uint32_t(z) >= memory.template read<uint32_t>(root + 0x3c)) return false;
    const uint32_t cell = columns * uint32_t(z) + uint32_t(x);
    const auto offset = int64_t(std::bit_cast<int32_t>(cell));
    const bool sloped = query.test == TerrainTriangleTest::sloped;
    if (sloped && cell < memory.template read<uint32_t>(root + 0x68)) {
        const auto flags = memory.template read<uint64_t>(root + 0x70);
        if (flags && (memory.template read<uint32_t>(flags + offset * 4) & 1))
            return false;
    }

    const auto material = [&](uint64_t row) {
        return memory.template read<uint8_t>(row + 0x30 + query.quadrant);
    };
    const auto overlay = memory.template read<uint64_t>(root + 0x60);
    if (overlay && cell < memory.template read<uint32_t>(root + 0x58)
        && material(overlay + offset * 0x34) != 7)
        return false;
    if (cell >= memory.template read<uint32_t>(root + 0x48)) return false;
    const auto ground = memory.template read<uint64_t>(root + 0x50);
    if (!ground) return false;
    const auto row = ground + offset * 0x34;
    if (material(row) != 5) return false;

    const auto point = row + 12u * query.quadrant;
    const auto height = [&](unsigned index) {
        return std::bit_cast<float>(memory.template read<uint32_t>(point + 4u * index));
    };
    const auto nearly_equal = [](float difference) { return difference >= -0x1p-18f && difference <= 0x1p-18f; };
    const float first = height(0), second = height(1);
    if (!nearly_equal(first - second)) return sloped;
    const float third = height(2);
    if (!nearly_equal(second - third)) return sloped;
    return nearly_equal(third - first) != sloped;
}

} // namespace nx::acnh
