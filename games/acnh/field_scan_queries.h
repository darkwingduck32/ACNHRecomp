#pragma once

#include "field_queries.h"
#include "shovel_item_metadata.h"

namespace nx::acnh {

template<class Memory>
uint16_t field_scan_item_type(uint64_t base, ItemDescriptor descriptor, const Memory& memory) {
    const CatalogEntry row{memory.template read<uint64_t>(descriptor.address + 0x10)};
    return row.address ? metadata_enum(row, {base + 0x5226ec0, 0xbc}, memory) : 0xbc;
}

template<class Memory>
void field_scan_unit_tile(uint64_t base, HalfTileRef input, HalfTileRef output, Memory& memory) {
    const auto type = memory.template read<uint64_t>(base + 0x4ab2018) + 0x10;
    // The descriptor header is initialized before reading the input, which can alias it.
    memory.template write<uint64_t>(output.address, type);
    memory.template write<uint64_t>(output.address + 8, 0);
    const auto x = half_tile_coordinate(memory.template read<int32_t>(input.address + 8));
    const auto z = half_tile_coordinate(memory.template read<int32_t>(input.address + 12));
    memory.template write<uint32_t>(output.address + 8, x);
    memory.template write<uint32_t>(output.address + 12, z);
}

struct FieldScanVisited { uint64_t address; };

template<class Memory>
bool field_scan_mark_visited(uint64_t base, FieldScanVisited visited, HalfTileRef input, Memory& memory) {
    const auto bounds = base + 0x5133970;
    const auto x = memory.template read<int32_t>(input.address + 8);
    if (x < memory.template read<int32_t>(bounds) || x > memory.template read<int32_t>(bounds + 16))
        return false;
    const auto z = memory.template read<int32_t>(input.address + 12);
    if (z < memory.template read<int32_t>(bounds + 4) || z > memory.template read<int32_t>(bounds + 20))
        return false;
    const uint32_t index = ((uint32_t(z) << 7) & 0xffffff00u) + uint32_t(x >> 1);
    if (index < 0x8000) {
        const auto word = visited.address + 8 + 4u * (index >> 5);
        const auto bit = 1u << ((uint32_t(x) >> 1) & 31);
        memory.template write<uint32_t>(word, memory.template read<uint32_t>(word) | bit);
    }
    return true;
}

template<class Memory>
int8_t field_scan_tile_class(uint64_t base, FieldLayer layer, HalfTileRef input, const Memory& memory) {
    const auto grid = FieldQueries{base, memory}.grid_for(layer);
    if (!grid) return 0;
    const auto x = memory.template read<uint32_t>(input.address + 8);
    const auto z = memory.template read<uint32_t>(input.address + 12);
    const auto record = tile_record(base + 0x50a50f0, grid.address, x, z, memory);
    return memory.template read<int8_t>(record + 0x10);
}

} // namespace nx::acnh
