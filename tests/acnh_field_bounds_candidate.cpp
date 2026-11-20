#include "../games/acnh/field_queries.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    uint64_t base, layer_address, position, address;
    uint32_t channel, count, value;
    while (std::cin >> base >> layer_address >> channel >> position >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        const nx::acnh::FieldQueries fields{base, memory};
        const nx::acnh::FieldLayer layer{memory.read<uint32_t>(layer_address + 8)};
        const auto bounds = fields.bounds_for(layer, {channel});
        const bool inside = bounds && fields.contains(bounds,
            {memory.read<int32_t>(position + 8), memory.read<int32_t>(position + 12)});
        std::cout << inside << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
