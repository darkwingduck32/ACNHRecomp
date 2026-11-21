#include "../games/acnh/render_binding_cache.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    mutable std::vector<std::array<uint64_t, 2>> reads;

    template<class T>
    T read(uint64_t address) const {
        reads.push_back({address, sizeof(T)});
        return FixtureMemory::read<T>(address);
    }
};
}

int main() {
    uint64_t state, slots;
    size_t size;
    while (std::cin >> state >> slots >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::cout << nx::acnh::render_binding_slots_empty({state}, {slots}, memory);
        for (auto [address, bytes] : memory.reads) std::cout << ' ' << address << ' ' << bytes;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
