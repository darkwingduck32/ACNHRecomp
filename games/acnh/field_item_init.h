#pragma once

#include "shovel_item_queries.h"

namespace nx::acnh {

template<class Memory>
bool empty_field_item_finalized(FieldItem item, const Memory& memory) {
    const auto id = memory.template read<uint16_t>(item.address);
    return id == 0xfffd || id == 0xfffe;
}

template<class Memory, class Finalize>
bool initialize_empty_field_item(uint64_t base, FieldItem item, Memory& memory, Finalize finalize) {
    if (memory.template read<uint64_t>(item.address) != 0xfffe) return false;

    const uint64_t catalog_slot = memory.template read<uint64_t>(base + 0x4ab43c0);
    const uint64_t owner = memory.template read<uint64_t>(catalog_slot);
    if (owner) {
        const ItemCatalog catalog{memory.template read<uint64_t>(owner + 0x57e58)};
        if (ShovelItemQueries{base, memory}.entry(catalog, 0xfffe).address) return false;
    }

    finalize(item);
    return true;
}

}  // namespace nx::acnh
