#include "../games/acnh/render_submit.h"
#include "fixture_memory.h"

#include <array>
#include <bit>
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
    uint64_t binding, state, sink;
    uint32_t pass, mask;
    size_t size;
    while (std::cin >> binding >> state >> sink >> pass >> mask >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        const auto submit = nx::acnh::prepare_indirect_render_submit(
            {binding}, {state}, {sink}, std::bit_cast<int32_t>(pass), mask, memory);
        std::cout << submit.selected << ' ' << submit.records << ' ' << submit.record << ' ' << submit.receiver;
        for (const auto [address, bytes] : memory.reads) std::cout << ' ' << address << ' ' << bytes;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
