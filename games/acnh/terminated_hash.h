#pragma once

#include <bit>
#include <cstdint>

namespace nx::acnh {

template<class Memory>
uint32_t terminated_hash(uint64_t address, uint32_t hash, const Memory& memory) {
    constexpr uint32_t block_multiplier = 0xcc9e2d51u;
    constexpr uint32_t mixed_multiplier = 0x1b873593u;
    uint32_t length = 0;
    for (;;) {
        uint32_t block = 0;
        unsigned count = 0;
        for (; count < 4; ++count) {
            const auto byte = memory.template read<uint8_t>(address + length);
            if (!byte) break;
            block |= uint32_t{byte} << (8 * count);
            ++length;
        }
        if (count != 0)
            hash ^= std::rotl(block * block_multiplier, 15) * mixed_multiplier;
        if (count < 4) break;
        hash = std::rotl(hash, 13) * 5u + 0xe6546b64u;
    }

    hash ^= length;
    hash = (hash ^ (hash >> 16)) * 0x85ebca6bu;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35u;
    return hash ^ (hash >> 16);
}

}  // namespace nx::acnh
