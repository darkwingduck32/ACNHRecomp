#include "../games/acnh/shovel_terrain.h"

#include <iostream>
#include <unordered_map>

namespace {

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t result = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            result |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(result);
    }

    template<class T>
    void write(uint64_t address, T value) {
        std::cout << ' ' << address << ' ' << sizeof(T) << ' ' << uint64_t(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
    }
};

}

int main() {
    uint64_t grid, output;
    nx::acnh::HalfTile tile{};
    size_t byte_count;
    while (std::cin >> grid >> tile.x >> tile.z >> output >> byte_count) {
        Memory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        std::cout << grid;
        nx::acnh::sample_terrain({grid}, tile, {output}, memory);
        std::cout << '\n';
    }
}
