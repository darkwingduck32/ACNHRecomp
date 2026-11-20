#pragma once

#include <cstdint>

namespace nx::acnh {

template<class Memory>
uint64_t tile_record(uint64_t fallback, uint64_t grid, uint32_t x, uint32_t z,
                     const Memory& memory) {
    const uint32_t dx = x - memory.template read<uint32_t>(grid + 0x28);
    const uint32_t dz = z - memory.template read<uint32_t>(grid + 0x2c);
    const uint32_t width = memory.template read<uint32_t>(grid + 0x18);
    const uint32_t height = memory.template read<uint32_t>(grid + 0x1c);
    uint32_t index;
    if (dx < width && dz < height) {
        index = width * dz + dx;
    } else {
        if (!memory.template read<uint8_t>(grid + 0x88)) return fallback;
        const uint32_t sx = x - memory.template read<uint32_t>(grid + 0x98);
        const uint32_t sz = z - memory.template read<uint32_t>(grid + 0x9c);
        const uint32_t secondary_width = memory.template read<uint32_t>(grid + 0xa0);
        const uint32_t secondary_height = memory.template read<uint32_t>(grid + 0xa4);
        if (sx >= secondary_width || sz >= secondary_height) return fallback;
        index = secondary_width * sz + sx + memory.template read<uint32_t>(grid + 0x8c);
    }
    const uint32_t count = memory.template read<uint32_t>(grid + 0x30);
    const uint64_t records = memory.template read<uint64_t>(grid + 0x38);
    return records + 0x68ll * static_cast<int32_t>(index < count ? index : 0);
}

template<class Memory>
bool tile_record_flag(uint64_t base, uint64_t query, const Memory& memory) {
    const uint64_t owner = memory.template read<uint64_t>(base + 0x509d040);
    const uint64_t node = memory.template read<uint64_t>(owner + 0x1f8);
    if (!node) return false;
    const uint64_t grid = memory.template read<uint64_t>(node + 0xb0);
    if (!grid) return false;
    const uint32_t x = memory.template read<uint32_t>(query + 8);
    const uint32_t z = memory.template read<uint32_t>(query + 12);
    const uint64_t record = tile_record(base + 0x50a50f0, grid, x, z, memory);
    return (memory.template read<uint8_t>(record + 0xc) & 2) != 0;
}

}  // namespace nx::acnh
