#include "../games/acnh/render_resource_prefix.h"
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
    using namespace nx::acnh;
    uint64_t state, slots;
    size_t size;
    while (std::cin >> state >> slots >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            uint32_t byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        const auto request = memory.read<int8_t>(state + 0x38);
        const auto prefix = select_render_resource_prefix({state}, {slots}, request, memory);
        std::cout << unsigned(prefix.route) << ' ' << uint32_t(prefix.lane) << ' ' << prefix.record
                  << ' ' << prefix.resource << ' ' << prefix.key << ' ' << prefix.binding
                  << ' ' << prefix.previous_binding << ' ' << prefix.packed << ' ' << prefix.shifted;
        for (auto [address, bytes] : memory.reads) std::cout << ' ' << address << ' ' << bytes;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
