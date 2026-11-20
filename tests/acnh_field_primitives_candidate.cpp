#include "../games/acnh/field_primitives.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <vector>

struct TraceMemory : FixtureMemory {
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint8_t>(uint64_t{value} >> (8 * i));
        writes.emplace_back(address, sizeof(T), value);
    }
};

int main() {
    uint64_t a, b, c, address;
    uint32_t mode, count, byte;
    while (std::cin >> mode >> a >> b >> c >> count) {
        TraceMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> byte)) return 1;
            memory.bytes.emplace(address, byte);
        }
        uint64_t result = a;
        if (mode == 0) nx::acnh::set_action_bit(a, static_cast<uint32_t>(b), static_cast<uint32_t>(c), memory);
        if (mode == 1) result = nx::acnh::half_tile_record(c, a, b, memory);
        if (mode == 2) nx::acnh::multiply_vector3(a, b, c, memory);
        if (mode == 3) result = nx::acnh::tile_category_mask(a, b, c, memory);
        std::cout << result;
        for (const auto& [addr, size, value] : memory.writes)
            std::cout << ' ' << addr << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
