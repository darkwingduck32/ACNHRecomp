#include "../games/acnh/terrain_draw_pass.h"
#include "fixture_memory.h"

#include <iostream>
#include <type_traits>
#include <vector>

namespace {
struct Read {
    uint64_t address, width, value;
};

struct Memory : FixtureMemory {
    mutable std::vector<Read> reads;

    template<class T>
    T read(uint64_t address) const {
        const auto value = FixtureMemory::read<T>(address);
        const auto raw = static_cast<std::make_unsigned_t<T>>(value);
        reads.push_back({address, sizeof(T), raw});
        return value;
    }
};
}

int main() {
    using namespace nx::acnh;
    uint64_t list, first, rows, stack;
    uint32_t ordinal;
    size_t bytes;
    while (std::cin >> list >> first >> rows >> ordinal >> stack >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const auto plan = prepare_terrain_draw_nonempty(
            {list, first, rows, ordinal}, {stack+0x58,stack+0x60,stack}, memory);
        std::cout << plan.first_node << ' ' << plan.end_node << ' ' << plan.row << ' ' << plan.row_flags
                  << ' ' << plan.manager_data << ' ' << plan.binding << ' '
                  << static_cast<uint64_t>(plan.signed_pass) << ' ' << plan.pass << ' '
                  << static_cast<uint64_t>(plan.displacement) << ' ' << plan.gated;
        for (const auto& read : memory.reads)
            std::cout << ' ' << read.address << ' ' << read.width << ' ' << read.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
