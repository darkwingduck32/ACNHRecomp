#include "../games/acnh/shovel_terrain_material_mask.h"
#include "fixture_memory.h"

#include <iostream>

int main() {
    using namespace nx::acnh;
    uint64_t owner, mask;
    int32_t x, z;
    unsigned overlay;
    size_t bytes;
    while (std::cin >> owner >> mask >> x >> z >> overlay >> bytes) {
        FixtureMemory memory;
        for (size_t index = 0; index < bytes; ++index) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const TerrainMaterialMaskQuery query{{owner}, {x, z}, mask,
            overlay ? TerrainMaterialLayer::overlay : TerrainMaterialLayer::ground};
        std::cout << terrain_material_mask_matches(query, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
