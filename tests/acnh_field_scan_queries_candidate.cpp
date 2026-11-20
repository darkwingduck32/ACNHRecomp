#include "../games/acnh/field_scan_queries.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
        writes.push_back({address, sizeof(T), uint64_t(value)});
    }
};

} // namespace

int main() {
    using namespace nx::acnh;
    unsigned mode;
    uint64_t base, x0, x1;
    size_t size;
    while (std::cin >> mode >> base >> x0 >> x1 >> size) {
        Memory memory;
        for (size_t index = 0; index < size; ++index) {
            uint64_t address;
            uint32_t value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        uint64_t result = x0;
        switch (mode) {
        case 0: result = field_scan_item_type(base, {x0}, memory) == uint32_t(x1); break;
        case 1: field_scan_unit_tile(base, {x1}, {x0}, memory); break;
        case 2: result = field_scan_mark_visited(base, {x0}, {x1}, memory); break;
        case 3: result = field_scan_item_type(base, {x0}, memory); break;
        case 4: result = field_scan_item_type(base, {x0}, memory) == 0x7f; break;
        case 5: result = uint32_t(field_scan_tile_class(base, {uint32_t(x1)}, {x0}, memory)); break;
        default: return 2;
        }
        std::cout << result;
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
