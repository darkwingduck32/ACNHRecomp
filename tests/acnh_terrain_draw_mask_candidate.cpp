#include "../games/acnh/terrain_draw_pass.h"
#include "fixture_memory.h"
#include "../runtime/include/nx/cpu.h"

#include <iostream>
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
        reads.push_back({address, sizeof(T), value});
        return value;
    }
};
}

int main() {
    uint64_t table, node, sp;
    size_t bytes;
    while (std::cin >> table >> node >> sp >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned byte;
            if (!(std::cin >> address >> byte)) return 1;
            memory.bytes.emplace(address, byte);
        }
        const auto result = nx::acnh::test_terrain_draw_node(
            {sp + 0x58, sp + 0x24, sp + 8}, table, node, memory);
        std::cout << bool(result);
        if (result) {
            nx::Flags flags{};
            nx::logic32(flags, result->mask & result->flags);
            std::cout << ' ' << result->mask << ' ' << result->flags << ' '
                      << nx::get_nzcv(flags);
        }
        for (const auto& read : memory.reads)
            std::cout << ' ' << read.address << ' ' << read.width << ' ' << read.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
