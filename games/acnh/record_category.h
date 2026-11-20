#pragma once

#include <cstdint>
#include <limits>

namespace nx::acnh {

// A packed record stores a backlink to the table header. The game keeps a
// parallel category array indexed by the record's position in that table.
template<class Memory>
bool record_category(uint64_t base, uint64_t row, uint16_t category, const Memory& memory) {
    const uint32_t backlink = memory.template read<uint32_t>(row);
    if (!backlink || row == backlink) return false;
    const uint64_t header = row - backlink;

    const uint32_t record_count = memory.template read<uint32_t>(header);
    if (!record_count) return false;
    const uint32_t stride = memory.template read<uint32_t>(header + 4);
    if (!stride) return false;
    const uint16_t columns = memory.template read<uint16_t>(header + 8);
    if (!columns) return false;
    const uint32_t prefix = memory.template read<uint8_t>(header + 10) ? 0x1cu : 0x0cu;
    const uint64_t records = header + 8u * columns + prefix;
    if (!records) return false;

    const uint32_t row_index = static_cast<uint32_t>((row - records) / stride);
    const uint32_t index = row_index < record_count ? row_index : std::numeric_limits<uint32_t>::max();
    const uint64_t descriptor = base + 0x5390120;
    const uint32_t count = memory.template read<uint32_t>(descriptor);
    if (index >= count) return false;
    const uint64_t types = memory.template read<uint64_t>(descriptor + 8);
    const int64_t offset = 2ll * static_cast<int32_t>(index);
    const uint16_t actual = memory.template read<uint16_t>(types + offset);
    return actual == 2 || actual == category;
}

}  // namespace nx::acnh
