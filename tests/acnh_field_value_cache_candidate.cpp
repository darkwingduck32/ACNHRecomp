#include "../games/acnh/field_value_cache.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <type_traits>
#include <vector>

struct WritableFixture : FixtureMemory {
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = static_cast<std::make_unsigned_t<T>>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint8_t>(bits >> (8 * i));
        writes.emplace_back(address, sizeof(T), bits);
    }
};

int main() {
    uint64_t base, row, address;
    uint32_t operation, index, hash, count, value;
    while (std::cin >> operation >> base >> row >> index >> hash >> count) {
        WritableFixture memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        const uint64_t result = operation == 0
            ? nx::acnh::field_column_8(base, row, index, hash, memory)
            : nx::acnh::cached_record_value(base, {row, index, hash}, memory);
        std::cout << result;
        for (const auto& [addr, size, bits] : memory.writes)
            std::cout << ' ' << addr << ' ' << size << ' ' << bits;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
