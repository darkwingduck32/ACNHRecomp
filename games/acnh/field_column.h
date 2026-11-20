#pragma once

#include <cstdint>

namespace nx::acnh {

// Resolve a field by its hash and cache its row-relative offset.
template<uint32_t SlotCount, class Memory>
uint64_t cached_column(uint64_t row, uint32_t column, uint32_t hash,
                       uint64_t ready, uint64_t slots, Memory& memory) {
    static_assert(SlotCount == 14 || SlotCount == 25);
    if (!memory.template read<uint8_t>(ready)) {
        constexpr uint64_t unresolved = UINT64_MAX;
        constexpr uint64_t last = 4ull * (SlotCount & ~uint32_t{3});
        memory.template write<uint64_t>(slots, unresolved);
        memory.template write<uint64_t>(slots + 8, unresolved);
        memory.template write<uint8_t>(ready, 1);
        if constexpr (SlotCount == 14)
            memory.template write<uint64_t>(slots + last, unresolved);
        else
            memory.template write<uint32_t>(slots + last, UINT32_MAX);
        for (uint64_t offset = 16; offset < last; offset += 8)
            memory.template write<uint64_t>(slots + offset, unresolved);
    }

    const uint64_t entry = slots + 4ull * (column < SlotCount ? column : 0);
    const int32_t cached = memory.template read<int32_t>(entry);
    if (cached >= 0) return row + cached;
    if (cached != -1) return 0;

    const uint32_t back = memory.template read<uint32_t>(row);
    if (!back || row == back) return 0;
    const uint64_t header = row - back;
    const uint32_t prefix = memory.template read<uint8_t>(header + 10) ? 0x1cu : 0x0cu;
    uint64_t field = header + prefix;
    if (!field) return 0;

    const uint16_t count = memory.template read<uint16_t>(header + 8);
    memory.template write<int32_t>(entry, -2);
    for (uint16_t i = 0; i < count; ++i, field += 8) {
        if (memory.template read<uint32_t>(field) != hash) continue;
        const uint32_t offset = memory.template read<uint32_t>(field + 4);
        memory.template write<uint32_t>(entry, offset == UINT32_MAX ? 0 : offset);
        if (!memory.template read<uint32_t>(row)) return 0;
        const int32_t resolved = memory.template read<int32_t>(entry);
        return resolved >= 0 ? row + resolved : 0;
    }
    return 0;
}

template<class Memory>
uint64_t field_column(uint64_t row, uint32_t column, uint32_t hash, uint64_t cache, Memory& memory) {
    if (!memory.template read<uint32_t>(row)) return 0;
    return cached_column<14>(row, column, hash, cache, cache + 4, memory);
}

}  // namespace nx::acnh
