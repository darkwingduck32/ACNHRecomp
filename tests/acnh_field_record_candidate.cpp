#include "../games/acnh/field_record.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    uint64_t db;
    uint32_t index, count;
    while (std::cin >> db >> index >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t address;
            uint32_t value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        std::cout << nx::acnh::field_record(db, index, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
