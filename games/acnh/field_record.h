#pragma once

#include <cstdint>

namespace nx::acnh {

// Returns a guest address. The database can supply either prepared row
// pointers or a packed table header with offsets relative to itself.
template<class Memory>
uint64_t field_record(uint64_t db, uint32_t index, const Memory& memory) {
    const int32_t prepared_size = memory.template read<int32_t>(db + 0x1c0);
    if (prepared_size > 0) {
        if (index >= static_cast<uint32_t>(prepared_size)) return 0;
        const uint32_t row_count = memory.template read<uint32_t>(db + 0x1b0);
        const uint64_t pointers = memory.template read<uint64_t>(db + 0x1b8);
        const uint32_t slot = index < row_count ? index : 0;
        return memory.template read<uint64_t>(pointers + 8ull * slot);
    }

    const uint64_t registry = memory.template read<uint64_t>(db + 0xb0);
    if (!registry) return 0;
    const uint64_t header = memory.template read<uint64_t>(registry + 8);
    if (!header) return 0;
    if (index < memory.template read<uint32_t>(db + 0xc8)) {
        const uint64_t flags = memory.template read<uint64_t>(db + 0xd0);
        const int32_t offset = static_cast<int32_t>(index);
        if (!memory.template read<uint8_t>(flags + offset)) return 0;
    }

    if (index >= memory.template read<uint32_t>(header)) return 0;
    const uint32_t stride = memory.template read<uint32_t>(header + 4);
    if (!stride) return 0;
    const uint16_t columns = memory.template read<uint16_t>(header + 8);
    if (!columns) return 0;
    const uint32_t prefix = memory.template read<uint8_t>(header + 10) ? 0x1cu : 0x0cu;
    const uint32_t offset = stride * index + 8u * columns + prefix;
    return header + offset;
}

}  // namespace nx::acnh
