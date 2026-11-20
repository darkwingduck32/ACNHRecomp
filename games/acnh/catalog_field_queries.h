#pragma once

#include "field_scan_queries.h"

namespace nx::acnh {

template<class Memory>
uint32_t catalog_category_attribute(uint64_t base, uint32_t category, const Memory& memory) {
    const auto catalog = memory.template read<uint64_t>(base + 0x522a7a0);
    return metadata_enum({field_record(catalog, category, memory)}, {base + 0x5226b00, 0x74}, memory);
}

template<class Memory>
uint32_t catalog_item_attribute(uint64_t base, ItemDescriptor item, const Memory& memory) {
    const CatalogEntry row{memory.template read<uint64_t>(item.address + 0x10)};
    const auto category = row.address ? metadata_enum(row, {base + 0x5226af0, 0xa1}, memory) : 0x21;
    return catalog_category_attribute(base, category, memory);
}

template<class Memory>
bool catalog_matches_ground_categories(uint64_t base, ItemDescriptor item, const Memory& memory) {
    const auto type = field_scan_item_type(base, item, memory);
    return type == 0xab || type == 7;
}

template<class Memory>
bool catalog_matches_edge_categories(uint64_t base, ItemDescriptor item, const Memory& memory) {
    const auto row = memory.template read<uint64_t>(item.address + 0x10);
    if (!row) return false;
    const auto back = memory.template read<uint32_t>(row);
    if (!back || row == back) return false;
    const auto header = row - back;
    const auto count = memory.template read<uint32_t>(header);
    const auto stride = memory.template read<uint32_t>(header + 4);
    const auto columns = memory.template read<uint16_t>(header + 8);
    if (!count || !stride || !columns) return false;
    const auto prefix = memory.template read<uint8_t>(header + 10) ? 0x1c : 0xc;
    const auto first = header + 8ull * columns + prefix;
    if (!first) return false;
    const auto index = uint32_t((row - first) / stride);
    const auto table = memory.template read<uint64_t>(base + 0x4ac00c8);
    const auto type_count = memory.template read<uint32_t>(table);
    if (index >= count || index >= type_count) return false;
    const auto offset = 2ll * std::bit_cast<int32_t>(index);
    const auto primary = memory.template read<uint64_t>(table + 8);
    if (memory.template read<uint16_t>(primary + offset) == 0xae) return true;
    // The first table's count also limits the original's secondary lookup.
    const auto secondary = memory.template read<uint64_t>(base + 0x5226ec8);
    return memory.template read<uint16_t>(secondary + offset) == 0xad;
}

struct CatalogTextOutput { uint64_t address; };

template<class Memory>
void catalog_description(uint64_t base, uint32_t category, CatalogTextOutput output, Memory& memory) {
    const auto catalog = memory.template read<uint64_t>(base + 0x522a7a8);
    const auto row = field_record(catalog, category, memory);
    uint64_t text;
    if (!row) {
        text = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab2238) + 8);
    } else {
        text = 0;
        if (memory.template read<uint32_t>(row)) {
            const auto ready = memory.template read<uint64_t>(base + 0x4abe218);
            const auto slots = memory.template read<uint64_t>(base + 0x4abe220);
            text = cached_column<14>(row, 5, 0x87bf00e8, ready, slots, memory);
        }
        if (!text) text = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4abff10));
    }
    const auto type = memory.template read<uint64_t>(base + 0x4ab2020) + 0x10;
    memory.template write<uint64_t>(output.address, type);
    memory.template write<uint64_t>(output.address + 8, text);
}

} // namespace nx::acnh
