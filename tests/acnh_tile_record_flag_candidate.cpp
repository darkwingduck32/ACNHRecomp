#include "../games/acnh/tile_record_flag.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    uint64_t base, query, address;
    uint32_t count, value;
    while (std::cin >> base >> query >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        std::cout << nx::acnh::tile_record_flag(base, query, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
