#include "../games/acnh/render_submit.h"
#include "fixture_memory.h"

#include <iostream>
#include <utility>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    mutable std::vector<std::pair<uint64_t, unsigned>> reads;

    template<class T>
    T read(uint64_t address) const {
        reads.emplace_back(address, sizeof(T));
        return FixtureMemory::read<T>(address);
    }
};
}

int main() {
    uint64_t state, slots, binding;
    size_t size;
    while (std::cin >> state >> slots >> binding >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const auto submit = nx::acnh::prepare_render_selector_submit({state}, {slots}, {binding}, memory);
        std::cout << submit.selected << ' ' << submit.lane << ' ' << submit.record << ' ' << submit.receiver;
        for (const auto& [address, width] : memory.reads) std::cout << ' ' << address << ' ' << width;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
