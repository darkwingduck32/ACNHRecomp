#pragma once

#include "field_column.h"

namespace nx::acnh {

template<class Memory>
uint64_t field_column_25(uint64_t base, uint64_t row, uint32_t index, uint32_t hash, Memory& memory) {
    if (!memory.template read<uint32_t>(row)) return 0;
    const auto ready = memory.template read<uint64_t>(base + 0x4abbc70);
    const auto slots = memory.template read<uint64_t>(base + 0x4abbc78);
    return cached_column<25>(row, index, hash, ready, slots, memory);
}

template<class Memory>
bool cached_field_bit(uint64_t base, uint64_t row, int32_t index, Memory& memory) {
    if (!row) return false;
    const uint64_t column = field_column_25(base, row, 22, 0x623dc307, memory);
    if (!column) return false;
    const uint32_t offset = static_cast<uint32_t>(index / 8);
    const uint32_t shift = static_cast<uint32_t>(index) & 7;
    return (memory.template read<uint8_t>(column + offset) >> shift) & 1;
}

}  // namespace nx::acnh
