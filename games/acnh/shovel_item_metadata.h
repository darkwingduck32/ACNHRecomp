#pragma once

#include "shovel_item_queries.h"
#include "field_column.h"
#include "field_record.h"

namespace nx::acnh {

struct MetadataEnumTable {
    uint64_t address;
    uint16_t maximum;
};

template<class Memory>
uint16_t metadata_enum(CatalogEntry row, MetadataEnumTable table, const Memory& memory) {
    if (!row.address) return 0;
    const auto relative = memory.template read<uint32_t>(row.address);
    if (!relative || relative == row.address) return 0;
    const uint64_t header = row.address - relative;
    const uint32_t count = memory.template read<uint32_t>(header);
    if (!count) return 0;
    const uint32_t stride = memory.template read<uint32_t>(header + 4);
    if (!stride) return 0;
    const uint16_t columns = memory.template read<uint16_t>(header + 8);
    if (!columns) return 0;
    const uint64_t prefix = memory.template read<uint8_t>(header + 10) ? 0x1c : 0xc;
    const uint64_t first = header + 8ull * columns + prefix;
    if (!first) return 0;
    const uint32_t index = static_cast<uint32_t>((row.address - first) / stride);
    if (index >= count || index >= memory.template read<uint32_t>(table.address)) return 0;
    const uint64_t values = memory.template read<uint64_t>(table.address + 8);
    const auto value = memory.template read<uint16_t>(values + 2ll * std::bit_cast<int32_t>(index));
    return value <= table.maximum ? value : 0;
}

template<class Memory, class Services>
uint32_t shovel_alternate_kind(uint64_t base, ItemDescriptor descriptor, const Memory& memory,
                               Services& services) {
    const CatalogEntry row{memory.template read<uint64_t>(descriptor.address + 0x10)};
    if (!row.address) return 0;

    const auto type = metadata_enum(
        row, {memory.template read<uint64_t>(base + 0x4ac00c8), 0xbc}, memory);
    if (type == 0xb1) {
        const auto item = memory.template read<uint32_t>(descriptor.address + 0xc);
        if (!(item & 0xf800)) {
            const auto slot = memory.template read<uint64_t>(base + 0x4ab46f0);
            const auto owner = memory.template read<uint64_t>(slot);
            const auto bits = memory.template read<uint8_t>(owner + 0xe99e0 + ((item >> 3) & 0x1fff));
            if ((bits >> (item & 7)) & 1) return 0x3a;
        }
    }
    if (type == 1 || (type == 0 && metadata_enum(row, {base + 0x5227298, 1}, memory) == 1))
        return 0x13;
    if (type == 2) return 0x12;
    const auto kind = metadata_enum(row, {base + 0x5226f68, 0xd9}, memory);
    if (kind == 0x17 && services.has_special_kind(descriptor)) return 0x18;
    return kind;
}

template<class Memory>
bool shovel_metadata_enabled(uint64_t base, ItemDescriptor descriptor, Memory& memory) {
    const uint64_t row = memory.template read<uint64_t>(descriptor.address + 0x10);
    if (!row) return false;

    const uint64_t type_slot = base + 0x4ac00c8;
    const auto type = metadata_enum(
        {row}, {memory.template read<uint64_t>(type_slot), 0xbc}, memory);
    const uint64_t db = memory.template read<uint64_t>(base + 0x522a778);
    const uint64_t category = field_record(db, type, memory);
    if (!category || !memory.template read<uint32_t>(category)) return false;

    const uint64_t ready = memory.template read<uint64_t>(base + 0x4abfeb0);
    const uint64_t slots = memory.template read<uint64_t>(base + 0x4abfeb8);
    const uint64_t column = cached_column<31>(category, 11, 0x90bc0855, ready, slots, memory);
    if (!column || !memory.template read<uint8_t>(column)) return false;

    const uint64_t updated_row = memory.template read<uint64_t>(descriptor.address + 0x10);
    const auto updated_type = updated_row ? metadata_enum(
        {updated_row}, {memory.template read<uint64_t>(type_slot), 0xbc}, memory) : 0xbc;
    const uint64_t updated_category = field_record(db, updated_type, memory);
    const uint64_t updated_column = updated_category + (column - category);
    return !updated_category || !memory.template read<uint32_t>(updated_category) ||
           !updated_column || !memory.template read<uint8_t>(updated_column) ||
           !(memory.template read<uint8_t>(descriptor.address + 0xa) & 0x20);
}

template<class Memory, class Services>
uint32_t shovel_metadata_kind(uint64_t base, ItemDescriptor descriptor, Memory& memory, Services& services) {
    const auto row = [&] { return CatalogEntry{memory.template read<uint64_t>(descriptor.address + 0x10)}; };
    const auto flags = [&] { return memory.template read<uint8_t>(descriptor.address + 0xa); };
    if (!row().address) return 0;

    const uint64_t slot = memory.template read<uint64_t>(base + 0x4ab43c0);
    const uint64_t owner = memory.template read<uint64_t>(slot);
    const uint32_t id = services.item_id({descriptor.address + 8});
    if (memory.template read<uint64_t>(slot)) {
        const ItemCatalog catalog{memory.template read<uint64_t>(owner + 0x57e58)};
        const ShovelItemQueries queries{base, memory};
        if (queries.entry(catalog, id).address && services.alternate_kind(descriptor) != 0 &&
            !services.metadata_enabled(descriptor, 0)) {
            services.metadata_enabled(descriptor, 1);
            if (!(flags() & 4)) return 0;
        }
    }

    services.metadata_enabled(descriptor, 2);
    const uint8_t item_flags = flags();
    if (item_flags & 4) return 0;
    const auto descriptor_row = row();
    const auto type = metadata_enum(descriptor_row, {base + 0x5226ec0, 0xbc}, memory);
    const bool extended = (item_flags & 8) != 0;
    const MetadataEnumTable direct{base + 0x5226e60, 0x1b};
    if ((type & ~2u) == 0xa4) {
        const auto value = metadata_enum(descriptor_row, direct, memory);
        if (extended) return value;
        return uint32_t(value) - 0xfu < 3;
    }
    if (!extended) return metadata_enum(descriptor_row, direct, memory);
    if (metadata_enum(descriptor_row, {base + 0x5226af0, 0xa1}, memory) == 0x21) return 0xc;
    const auto from_descriptor = services.extended_row(descriptor);
    if (!from_descriptor.address) return 0xc;
    return metadata_enum(from_descriptor, {base + 0x5226b70, 0x1b}, memory);
}

}  // namespace nx::acnh
