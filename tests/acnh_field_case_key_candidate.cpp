#include "../games/acnh/field_primitives.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    uint64_t base, a, b, c, d;
    unsigned first, second, third;
    while (std::cin >> base >> a >> b >> c >> d >> first >> second >> third) {
        FixtureMemory memory;
        const uint64_t table = base + 0x40749e0;
        memory.bytes.emplace(table, first);
        memory.bytes.emplace(table + 1, second);
        memory.bytes.emplace(table + 2, third);
        const std::array<uint8_t, 4> codes{uint8_t(a), uint8_t(b), uint8_t(c), uint8_t(d)};
        std::cout << nx::acnh::field_case_key(base, codes, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
