#pragma once

#include "shovel_terrain.h"

namespace nx::acnh {

enum class TerrainMaterialLayer { ground, overlay };

struct TerrainMaterialMaskQuery {
    TerrainOwner owner;
    HalfTile tile;
    uint64_t mask;
    TerrainMaterialLayer layer;
};

template<class Memory>
bool terrain_material_mask_matches(TerrainMaterialMaskQuery query, const Memory& memory) {
    const auto [x, z] = query.tile;
    if (x < 0 || z < 0) return false;

    const auto grid = memory.template read<uint64_t>(query.owner.address + 0x68);
    const auto width = memory.template read<uint32_t>(grid + 0x38);
    if (uint32_t(x) >= width || uint32_t(z) >= memory.template read<uint32_t>(grid + 0x3c)) return false;

    const uint32_t index = width * uint32_t(z) + uint32_t(x);
    const unsigned count_offset = query.layer == TerrainMaterialLayer::ground ? 0x48 : 0x58;
    const auto rows = memory.template read<uint64_t>(grid + count_offset + 8);
    if (!rows || index >= memory.template read<uint32_t>(grid + count_offset)) return false;
    const auto row = rows + 0x34ll * std::bit_cast<int32_t>(index);

    std::array<uint8_t, 4> kinds;
    for (size_t corner = 0; corner < kinds.size(); ++corner)
        kinds[corner] = memory.template read<uint8_t>(row + 0x30 + corner);
    for (uint32_t kind : kinds) {
        if (terrain_kind_in_mask(kind, query.mask, memory))
            return true;
    }
    return false;
}

} // namespace nx::acnh
