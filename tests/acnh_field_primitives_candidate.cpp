#include "../games/acnh/field_primitives.h"
#include "../games/acnh/field_queries.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <type_traits>
#include <vector>

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
        if (mode == 1) {
            const nx::acnh::FieldQueries queries{c, memory};
            result = queries.record_at({a}, {memory.read<int32_t>(b + 8), memory.read<int32_t>(b + 12)}).address;
        }
        if (mode == 2) nx::acnh::multiply_vector3(a, b, c, memory);
        if (mode == 3) result = nx::acnh::tile_category_mask(a, b, c, memory);
        if (mode == 4) {
            const nx::acnh::FieldQueries queries{c, memory};
            const auto grid = queries.grid_for({static_cast<uint32_t>(b)});
            result = grid && queries.record_flag(
                queries.record_at(grid, {memory.read<int32_t>(a + 8), memory.read<int32_t>(a + 12)}));
        }
        if (mode == 5 || mode == 6) {
            const nx::acnh::FieldQueries queries{c, memory};
            const auto property = mode == 5 ? nx::acnh::TerrainProperty::diggable : nx::acnh::TerrainProperty::alternate;
            result = queries.terrain_has({a}, static_cast<uint32_t>(b), property);
        }
        if (mode == 7) nx::acnh::FieldQueries{c, memory}.initialize_survey({a});
        if (mode == 8) result = nx::acnh::FieldQueries{c, memory}.auxiliary_item_index(static_cast<uint32_t>(a));
        if (mode == 9) result = nx::acnh::FieldQueries{c, memory}.item_id({a});
        std::cout << result;
        for (const auto& [addr, size, value] : memory.writes)
            std::cout << ' ' << addr << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
