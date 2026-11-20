#include "../games/acnh/field_bookkeeping.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
        const auto raw = static_cast<std::make_unsigned_t<T>>(value);
        writes.push_back({address, sizeof(T), uint64_t(raw)});
    }
};

} // namespace

int main() {
    using namespace nx::acnh;
    unsigned mode;
    uint64_t base, x0, x1, x2;
    size_t size;
    while (std::cin >> mode >> base >> x0 >> x1 >> x2 >> size) {
        Memory memory;
        for (size_t index = 0; index < size; ++index) {
            uint64_t address;
            uint32_t value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        uint64_t result = x0;
        switch (mode) {
        case 0: result = terrain_property_u16(base, {x0}, uint32_t(x1), memory); break;
        case 1: result = terrain_property_flag(base, {x0}, uint32_t(x1), memory); break;
        case 2: result = field_layer_bounds(base, {x0}, memory).address; break;
        case 3: terrain_flag_summary({x0}, {x1}, {x2}, memory); break;
        case 4: enqueue_half_tile({x0}, uint32_t(x1), uint32_t(x2), memory); break;
        default: return 2;
        }
        std::cout << result;
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
