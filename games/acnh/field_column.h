#pragma once

#include <cstdint>

namespace nx::acnh {

// Resolves a field by its hash and remembers its row-relative offset.
template<class Memory>
uint64_t field_column(uint64_t row, uint32_t column, uint32_t hash, uint64_t cache, Memory& memory) {
    if (!memory.template read<uint32_t>(row)) return 0;
    const uint64_t slots = cache + 4;
    if (!memory.template read<uint8_t>(cache)) {
        constexpr uint64_t unresolved = UINT64_MAX;
        memory.template write<uint64_t>(slots, unresolved);
        memory.template write<uint64_t>(slots + 8, unresolved);
        memory.template write<uint8_t>(cache, 1);
        memory.template write<uint64_t>(slots + 48, unresolved);
        for (uint64_t offset = 16; offset < 48; offset += 8)
            memory.template write<uint64_t>(slots + offset, unresolved);
    }

    const uint64_t entry = slots + 4ull * (column <= 13 ? column : 0);
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

}  // namespace nx::acnh
