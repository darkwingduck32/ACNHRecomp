#include "../games/acnh/terrain_clearance.h"
#include "../games/acnh/field_bookkeeping.h"
#include "fixture_memory.h"

#include <bit>
#include <iostream>
#include <vector>

int main() {
    using namespace nx::acnh;
    uint64_t base, point;
    uint32_t radius, selector;
    size_t size;
    while (std::cin >> base >> point >> radius >> selector >> size) {
        FixtureMemory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            uint32_t byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<HalfTile> tiles;
        const auto flags = [&](TerrainGrid grid, HalfTile tile) {
            tiles.push_back(tile);
            return terrain_flag_bits(grid, tile, memory, [](uint8_t) {});
        };
        const auto result = terrain_clearance(base, {point}, std::bit_cast<float>(radius), selector, memory, flags);
        std::cout << result.blocked << ' ' << std::bit_cast<uint32_t>(result.scalar);
        for (auto [x, z] : tiles) std::cout << ' ' << x << ' ' << z;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
