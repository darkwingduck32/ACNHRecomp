#include "../games/acnh/shovel_traversal_rectangle.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    using namespace nx::acnh;
    uint64_t grid;
    int32_t min_x, min_z, max_x, max_z;
    unsigned ground_only;
    size_t bytes;
    while (std::cin >> grid >> min_x >> min_z >> max_x >> max_z >> ground_only >> bytes) {
        FixtureMemory memory;
        for (size_t index = 0; index < bytes; ++index) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const TraversalRectangleQuery query{{grid}, {{min_x, min_z}, {max_x, max_z}}, bool(ground_only)};
        std::cout << shovel_traversal_rectangle_has_height_break(query, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
