#pragma once

#include "field_flag.h"
#include "shovel_terrain.h"

namespace nx::acnh {

struct TerrainClearancePoint { uint64_t address; };
struct TerrainClearanceResult { bool blocked; float scalar; };

template<class Memory, class ReadFlags>
TerrainClearanceResult terrain_clearance(uint64_t base, TerrainClearancePoint point, float radius,
                                         uint32_t selector, const Memory& memory, const ReadFlags& read_flags) {
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const auto grid_for = [&](uint64_t owner) -> TerrainGrid {
        return {owner ? memory.template read<uint64_t>(owner + 0xa8) : 0};
    };
    const auto owner_link = memory.template read<uint64_t>(base + 0x4ab27c8);
    const auto center_grid = grid_for(memory.template read<uint64_t>(owner_link));
    const uint8_t mask = (selector & 1u) ? 0x20 : 0x40;
    const HalfTile center{field_cell(read_float(point.address)), field_cell(read_float(point.address + 8))};
    if (center_grid.address && (read_flags(center_grid, center) & mask)) {
        const float magnitude = radius < 0.f ? -radius : radius;
        if (magnitude < 0x1p-23f) return {true, magnitude};
    }

    const auto step_slot = base + 0x5140230;
    auto step = memory.template read<uint32_t>(step_slot);
    const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00);
    auto owner = memory.template read<uint64_t>(owner_link);
    float sample_z = 0.f;
    for (uint32_t sector = 0; sector != 8; ++sector) {
        const uint32_t angle = step * sector;
        const auto row = trig + 16ull * (angle >> 24);
        const float fraction = static_cast<float>(angle & 0xffffffu) * 0x1p-24f;
        const float dx = std::fma(read_float(row + 4), fraction, read_float(row)) * radius;
        const float dz = std::fma(read_float(row + 12), fraction, read_float(row + 8)) * radius;
        const float sample_x = read_float(point.address) + dx;
        sample_z = read_float(point.address + 8) + dz;
        const HalfTile tile{field_cell(sample_x), field_cell(sample_z)};
        const auto grid = grid_for(owner);
        if (!grid.address) continue;
        const uint8_t flags = read_flags(grid, tile);
        step = memory.template read<uint32_t>(step_slot);
        owner = memory.template read<uint64_t>(owner_link);
        if (flags & mask) return {true, sample_z};
    }
    return {false, sample_z};
}

} // namespace nx::acnh
