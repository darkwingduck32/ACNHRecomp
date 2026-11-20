#include "../games/acnh/shovel_terrain.h"

#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t result = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            result |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(result);
    }
};

struct Services {
    unsigned mode;
    std::vector<uint64_t> evidence;

    void call(uint64_t entry, uint64_t object) {
        evidence.push_back(entry);
        evidence.push_back(object);
    }

    bool grid_allows(nx::acnh::TerrainGrid grid, nx::acnh::TerrainRect area) {
        call(0x871f80, grid.address);
        for (const auto coordinate : {area.min.x, area.min.z, area.max.x, area.max.z})
            evidence.push_back(uint32_t(coordinate));
        return mode != 3;
    }

    bool quadrant_materials(nx::acnh::TerrainOwner owner, nx::acnh::HalfTile tile,
                            std::array<uint32_t, 4>& materials) {
        call(0x848e68, owner.address + 0x28);
        evidence.push_back(uint32_t(tile.x));
        evidence.push_back(uint32_t(tile.z));
        materials = {0x31, 0x32, 0x33, 0x34};
        return mode != 5;
    }

    bool material_allows(nx::acnh::TerrainTable table, uint32_t material, size_t) {
        call(0x880870, table.address);
        evidence.push_back(material);
        return mode != 6;
    }

    bool layer_blocks(nx::acnh::HalfTile tile) {
        call(0x1285fe0, 0);
        evidence.push_back(uint32_t(tile.x));
        evidence.push_back(uint32_t(tile.z));
        return mode == 7;
    }
};

}

int main() {
    uint64_t base;
    nx::acnh::HalfTile tile{};
    unsigned mode;
    size_t byte_count;
    while (std::cin >> base >> tile.x >> tile.z >> mode >> byte_count) {
        Memory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        Services services{mode, {}};
        const bool result = nx::acnh::can_dig_terrain(base, tile, memory, services);
        std::cout << result;
        for (const auto value : services.evidence) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
