#include "../games/acnh/field_record.h"

#include <iostream>
#include <unordered_map>

struct FixtureMemory {
    std::unordered_map<uint64_t, uint32_t> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<uint64_t>(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(value);
    }
};

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
