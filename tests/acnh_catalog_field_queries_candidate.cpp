#include "../games/acnh/catalog_field_queries.h"
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
        const auto raw = static_cast<std::make_unsigned_t<T>>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(raw) >> (8 * i)) & 0xff;
        writes.push_back({address, sizeof(T), uint64_t(raw)});
    }
};

} // namespace

int main() {
    using namespace nx::acnh;
    unsigned mode;
    uint64_t base, x0, x8;
    size_t size;
    while (std::cin >> mode >> base >> x0 >> x8 >> size) {
        Memory memory;
        for (size_t index = 0; index < size; ++index) {
            uint64_t address;
            uint32_t value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        uint64_t result = 0;
        switch (mode) {
        case 0: result = catalog_category_attribute(base, uint32_t(x0), memory); break;
        case 1: result = catalog_item_attribute(base, {x0}, memory); break;
        case 2: result = catalog_matches_ground_categories(base, {x0}, memory); break;
        case 3: result = catalog_matches_edge_categories(base, {x0}, memory); break;
        case 4: catalog_description(base, uint32_t(x0), {x8}, memory); break;
        default: return 2;
        }
        std::cout << result;
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
