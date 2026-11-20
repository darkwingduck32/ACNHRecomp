#include "../games/acnh/field_queries.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {
struct TraceMemory : FixtureMemory {
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = static_cast<std::make_unsigned_t<T>>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint8_t>(bits >> (8 * i));
        writes.emplace_back(address, sizeof(T), bits);
    }
};

bool terrain_property(uint64_t base, uint64_t table, uint32_t index, bool requirement, TraceMemory& memory) {
    const auto property = requirement ? nx::acnh::TerrainProperty::alternate_requirement
                                      : nx::acnh::TerrainProperty::alternate_rejection;
    return nx::acnh::FieldQueries{base, memory}.terrain_has({table}, index, property);
}
} // namespace

int main(int argc, char**) {
    uint64_t base, table, bytes;
    unsigned mode, index;
    while (std::cin >> mode >> base >> table >> index >> bytes) {
        TraceMemory memory;
        for (uint64_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        if (argc == 1) {
            if (mode != 1) std::cout << terrain_property(base, table, index, false, memory);
            if (mode == 2) std::cout << ' ';
            if (mode != 0) std::cout << terrain_property(base, table, index, true, memory);
        } else {
            std::cout << 0;
        }
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
}
