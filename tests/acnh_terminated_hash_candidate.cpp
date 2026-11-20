#include "../games/acnh/terminated_hash.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    uint64_t address;
    uint32_t seed, count, value;
    while (std::cin >> address >> seed >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> value)) return 1;
            memory.bytes.emplace(address + i, value);
        }
        std::cout << nx::acnh::terminated_hash(address, seed, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
