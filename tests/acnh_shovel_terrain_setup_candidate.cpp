#include "../games/acnh/shovel_terrain_helpers.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

namespace {

using namespace nx::acnh;

constexpr uint64_t base = 0x4000000000;
constexpr uint64_t point_address = 0x20000;

HalfTile coordinates(HalfTileRef point, const FixtureMemory& memory) {
    return {memory.read<int32_t>(point.address + 8), memory.read<int32_t>(point.address + 12)};
}

struct Services {
    FixtureMemory& memory;
    unsigned mode;
    std::vector<uint32_t> calls;

    uint32_t stage_hash(uint64_t, uint32_t) {
        calls.insert(calls.end(), {0x87b4bc, 0x87b4c8});
        return mode == 3 ? 0 : 0x758f7eaf;
    }

    uint32_t test_material(HalfTileRef point, TerrainSetupRoute route) {
        switch (route) {
        case TerrainSetupRoute::layer_zero:
            calls.push_back(0x87b544);
            return terrain_has_field_material(base, coordinates(point, memory), memory);
        case TerrainSetupRoute::overlay:
            calls.push_back(0x87b56c);
            return 1;
        case TerrainSetupRoute::layer_one:
            calls.push_back(0x87b57c);
            return terrain_layer_one_has_material(base, coordinates(point, memory), memory);
        }
        return 0;
    }
};

} // namespace

int main() {
    unsigned number, mode, count, value;
    while (std::cin >> number >> mode >> count) {
        FixtureMemory memory;
        for (unsigned i = 0; i < count; ++i) {
            uint64_t address;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        Services services{memory, mode};
        constexpr HalfTileRef point{point_address};
        std::cout << (number ? terrain_layer_one_has_material(base, coordinates(point, memory), memory)
                            : shovel_terrain_setup(base, point, memory, services));
        for (auto site : services.calls) std::cout << ' ' << site;
        std::cout << '\n';
    }
}
