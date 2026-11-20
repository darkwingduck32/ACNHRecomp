#include "../games/acnh/field_flag.h"
#include "fixture_memory.h"

#include <bit>
#include <iostream>

int main() {
    uint64_t base, position;
    uint32_t radius, flags, count;
    while (std::cin >> base >> position >> radius >> flags >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t address;
            uint32_t value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        std::cout << nx::acnh::field_flag(base, position, std::bit_cast<float>(radius), flags, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
