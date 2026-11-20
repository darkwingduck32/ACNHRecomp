#pragma once

#include "tile_record_flag.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace nx::acnh {

template<class Memory>
void set_action_bit(uint64_t object, uint32_t bit, uint32_t flag, Memory& memory) {
    if (bit > 54) return;
    const uint64_t address = object + 4 * (bit / 32);
    const uint32_t mask = uint32_t{1} << (bit % 32);
    const uint32_t word = memory.template read<uint32_t>(address);
    memory.template write<uint32_t>(address, (flag & 1) ? word | mask : word & ~mask);
}

inline uint32_t half_tile_coordinate(int32_t coordinate) {
    return static_cast<uint32_t>(static_cast<int32_t>(std::floor(static_cast<float>(coordinate) * 0.5f)));
}

template<class Memory>
uint64_t half_tile_record(uint64_t base, uint64_t grid, uint64_t query, const Memory& memory) {
    const int32_t x = memory.template read<int32_t>(query + 8);
    const int32_t z = memory.template read<int32_t>(query + 12);
    const auto tile_x = half_tile_coordinate(x);
    const auto tile_z = half_tile_coordinate(z);
    const uint64_t record = tile_record(base + 0x50a50f0, grid, tile_x, tile_z, memory);
    for (uint64_t slot = record + 0x28; slot < record + 0x68; slot += 16) {
        if (memory.template read<int16_t>(slot + 12) == x &&
            memory.template read<int16_t>(slot + 14) == z) {
            return slot;
        }
    }
    return 0;
}

template<class Memory>
void multiply_vector3(uint64_t out, uint64_t matrix, uint64_t point, Memory& memory) {
    float m[9];
    float v[3];
    for (int i = 0; i < 9; ++i) {
        m[i] = std::bit_cast<float>(memory.template read<uint32_t>(matrix + 4 * i));
    }
    for (int i = 0; i < 3; ++i) {
        v[i] = std::bit_cast<float>(memory.template read<uint32_t>(point + 4 * i));
    }
    for (int i = 0; i < 3; ++i) {
        const float result = std::fma(m[6 + i], v[2], std::fma(m[i], v[0], m[3 + i] * v[1]));
        memory.template write<uint32_t>(out + 4 * i, std::bit_cast<uint32_t>(result));
    }
}

template<class Memory>
uint64_t tile_category_record(uint64_t owner, int32_t x, int32_t z, const Memory& memory) {
    if (x < 0 || z < 0) return 0;
    const uint64_t grid = memory.template read<uint64_t>(owner + 0x80);
    const uint32_t width = memory.template read<uint32_t>(grid + 0x38);
    const uint32_t height = memory.template read<uint32_t>(grid + 0x3c);
    if (static_cast<uint32_t>(x) >= width || static_cast<uint32_t>(z) >= height) return 0;
    const uint32_t index = width * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
    const uint32_t count = memory.template read<uint32_t>(grid + 0x48);
    const uint64_t records = memory.template read<uint64_t>(grid + 0x50);
    if (index >= count || !records) return 0;
    return records + 0x34ll * static_cast<int32_t>(index);
}

template<class Memory>
bool tile_category_mask(uint64_t owner, uint64_t query, uint64_t mask, const Memory& memory) {
    const int32_t x = memory.template read<int32_t>(query + 8);
    if (x < 0) return false;
    const int32_t z = memory.template read<int32_t>(query + 12);
    const uint64_t record = tile_category_record(owner, x, z, memory);
    if (!record) return false;
    const uint8_t categories[] = {
        memory.template read<uint8_t>(record + 0x30), memory.template read<uint8_t>(record + 0x31),
        memory.template read<uint8_t>(record + 0x32), memory.template read<uint8_t>(record + 0x33),
    };
    for (const uint32_t category : categories) {
        if (category < 73 && ((memory.template read<uint32_t>(mask + 4 * (category / 32)) >>
                               (category % 32)) & 1)) return true;
    }
    return false;
}

template<class Memory>
uint32_t field_case_key(uint64_t base, const std::array<uint8_t, 4>& codes, const Memory& memory) {
    for (const auto code : codes) {
        if (code > 2) return 0;
    }
    const uint64_t table = base + 0x40749e0;
    const uint32_t c = memory.template read<uint8_t>(table + codes[2]);
    const uint32_t d = memory.template read<uint8_t>(table + codes[3]);
    const uint32_t b = memory.template read<uint8_t>(table + codes[1]);
    const uint32_t a = memory.template read<uint8_t>(table + codes[0]);
    return a | (b << 2) | (c << 4) | (d << 6);
}

}  // namespace nx::acnh
