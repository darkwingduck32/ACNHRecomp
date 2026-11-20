#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

struct FixtureMemory {
    std::unordered_map<uint64_t, uint32_t> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= uint64_t{bytes.at(address + i)} << (8 * i);
        return static_cast<T>(value);
    }
};
