#include "../games/acnh/shovel_terrain_triangle.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    using namespace nx::acnh;
    uint64_t owner;
    int32_t x, z;
    unsigned quadrant, sloped;
    size_t bytes;
    while (std::cin >> owner >> x >> z >> quadrant >> sloped >> bytes) {
        FixtureMemory memory;
        for (size_t index = 0; index < bytes; ++index) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const TerrainTriangleQuery query{{owner}, {x, z}, uint8_t(quadrant),
            sloped ? TerrainTriangleTest::sloped : TerrainTriangleTest::flat};
        std::cout << shovel_terrain_triangle(query, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
