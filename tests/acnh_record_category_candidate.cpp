#include "../games/acnh/record_category.h"

#include "fixture_memory.h"

#include <iostream>

int main() {
    uint64_t base, row;
    uint32_t category, count;
    while (std::cin >> base >> row >> category >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t address;
            uint32_t value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        std::cout << nx::acnh::record_category(base, row, static_cast<uint16_t>(category), memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
