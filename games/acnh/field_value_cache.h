#pragma once

#include "field_column.h"

namespace nx::acnh {

template<class Memory>
uint64_t field_column_8(uint64_t base, uint64_t row, uint32_t index, uint32_t hash, Memory& memory) {
    const uint32_t back = memory.template read<uint32_t>(row);
    if (!back) return 0;

    const uint64_t ready = base + 0x5050d18;
    const uint64_t slots = ready + 4;
    if (!(memory.template read<uint8_t>(ready) & 1)) {
        memory.template write<uint8_t>(ready, 1);
        for (uint64_t offset = 0; offset < 32; offset += 8)
            memory.template write<uint64_t>(slots + offset, UINT64_MAX);
    }

    const uint64_t slot = slots + 4ull * index;
    const int32_t cached = memory.template read<int32_t>(slot);
    if (cached >= 0) return row + cached;
    if (cached != -1 || !cache_field_offset(row, back, hash, slot, memory)) return 0;
    const int32_t resolved = memory.template read<int32_t>(slot);
    return resolved >= 0 ? row + resolved : 0;
}

struct CachedRowValueQuery {
    uint64_t owner;
    uint32_t slot;
    uint32_t key;
};

template<class Memory>
uint32_t cached_record_value(uint64_t base, CachedRowValueQuery query, Memory& memory) {
    const uint64_t row = memory.template read<uint64_t>(query.owner + 8);
    if (!row) return 0x244db4da;
    if (!memory.template read<uint32_t>(row)) return 0;

    const uint64_t ready = memory.template read<uint64_t>(base + 0x4ac4610);
    const uint64_t slots = memory.template read<uint64_t>(base + 0x4ac4618);
    const uint64_t slot = slots + 4ull * query.slot;
    for (;;) {
        if (!memory.template read<uint8_t>(ready)) {
            memory.template write<uint64_t>(slots, UINT64_MAX);
            memory.template write<uint64_t>(slots + 8, UINT64_MAX);
            memory.template write<uint8_t>(ready, 1);
            for (uint64_t offset = 16; offset < 144; offset += 8)
                memory.template write<uint64_t>(slots + offset, UINT64_MAX);
        } else {
            const int32_t cached = memory.template read<int32_t>(slot);
            if (cached >= 0) {
                const uint64_t address = row + cached;
                return address ? memory.template read<uint32_t>(address) : 0;
            }
            if (cached != -1) return 0;
        }

        const uint32_t back = memory.template read<uint32_t>(row);
        if (!cache_field_offset(row, back, query.key, slot, memory)) return 0;
        if (!memory.template read<uint32_t>(row)) return 0;
    }
}

}  // namespace nx::acnh
