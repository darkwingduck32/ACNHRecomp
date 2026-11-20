#pragma once

#include "shovel_terrain.h"

namespace nx::acnh {

struct AlternateTerrainQuery { uint64_t rectangle; };

template<class Memory, class Services>
bool shovel_alternate_terrain(uint64_t base, AlternateTerrainQuery query,
                              const Memory& memory, Services& services) {
    const auto field = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab27a8));
    const auto layer = memory.template read<uint32_t>(field + 0x200) == 1 ? 0x32u : 1u;
    const auto terrain = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab27c8));
    const auto grid = [&] { return memory.template read<uint64_t>(terrain + 0x90); };
    if (!services.level({grid()}, query)) return false;

    const auto address = query.rectangle;
    const auto min_x = memory.template read<int32_t>(address + 0x10);
    const auto min_z = memory.template read<int32_t>(address + 0x14);
    const auto beyond_z = memory.template read<uint32_t>(address + 0x24) + 1;
    const auto has_tile = [&](uint64_t view, HalfTile tile) {
        return tile.x >= 0 && tile.z >= 0
            && uint32_t(tile.x) < memory.template read<uint32_t>(view + 0x38)
            && uint32_t(tile.z) < memory.template read<uint32_t>(view + 0x3c);
    };
    const auto index = [&](uint64_t view, HalfTile tile) {
        return memory.template read<uint32_t>(view + 0x38) * uint32_t(tile.z) + uint32_t(tile.x);
    };

    HalfTile tile{min_x, min_z};
    while (uint32_t(tile.z) != beyond_z) {
        const auto view = grid();
        if (has_tile(view, tile)) {
            const auto cell = index(view, tile);
            const auto flags = memory.template read<uint64_t>(view + 0x70);
            if (cell < memory.template read<uint32_t>(view + 0x68) && flags
                && (memory.template read<uint32_t>(flags + 4ll * std::bit_cast<int32_t>(cell)) & 4))
                return false;
        }
        if (services.layer_flag(tile, layer)) return false;

        const auto ground = grid();
        if (!has_tile(ground, tile)) return false;
        const auto cell = index(ground, tile);
        if (cell >= memory.template read<uint32_t>(ground + 0x48)) return false;
        const auto records = memory.template read<uint64_t>(ground + 0x50);
        if (!records) return false;
        const auto row = records + 0x34ll * std::bit_cast<int32_t>(cell);
        std::array<uint8_t, 4> kinds{};
        for (unsigned corner = 0; corner < kinds.size(); ++corner)
            kinds[corner] = memory.template read<uint8_t>(row + 0x30 + corner);
        for (unsigned corner = 0; corner < kinds.size(); ++corner) {
            const auto kind = kinds[corner];
            if (services.rejected(kind, corner) || (kind <= 46 && ((0x480000000080ull >> kind) & 1u))
                || !services.requirement(kind, corner))
                return false;
        }

        if (tile.x < memory.template read<int32_t>(address + 0x20)) {
            ++tile.x;
        } else {
            tile.x = memory.template read<int32_t>(address + 0x10);
            ++tile.z;
        }
    }
    return true;
}

} // namespace nx::acnh
