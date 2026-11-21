#include "../games/acnh/terrain_draw_pass.h"
#include "fixture_memory.h"

#include <iostream>
#include <utility>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    mutable std::vector<std::pair<uint64_t, uint64_t>> reads;

    template<class T>
    T read(uint64_t address) const {
        reads.emplace_back(address, sizeof(T));
        return FixtureMemory::read<T>(address);
    }
};
}

int main() {
    using namespace nx::acnh;
    uint64_t manager, descriptor, base;
    size_t bytes;
    while (std::cin >> manager >> descriptor >> base >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const auto plan = prepare_terrain_draw({manager}, {descriptor}, base, memory);
        std::cout << plan.supported << ' ' << unsigned(plan.pass) << ' ' << plan.context << ' ' << plan.binding
                  << ' ' << plan.context_index << ' ' << plan.selected_index << ' ' << memory.reads.size();
        for (const auto& [address, width] : memory.reads) std::cout << ' ' << address << ' ' << width;
        if (plan.supported)
            draw_terrain_lists(plan, [&](TerrainDrawList list) {
                std::cout << ' ' << list.lane << ' ' << list.address;
            });
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
