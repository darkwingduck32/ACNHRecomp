#include "../games/acnh/shovel_alternate_terrain.h"
#include "fixture_memory.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;
struct Reply { uint32_t site, result; };

struct Services {
    uint64_t base;
    const FixtureMemory& memory;
    const std::vector<Reply>& replies;
    size_t next = 0;
    std::vector<uint32_t> shapes;

    bool invoke(uint32_t site) {
        if (next == replies.size() || replies[next].site != site) {
            std::cerr << "unexpected alternate terrain callback " << std::hex << site << std::dec
                      << " in slot " << next << '\n';
            std::exit(1);
        }
        return (replies[next++].result & 1) != 0;
    }

    void address(uint64_t value) {
        shapes.push_back(uint32_t(value));
        shapes.push_back(uint32_t(value >> 32));
    }

    bool level(TerrainGrid grid, AlternateTerrainQuery query) {
        address(grid.address);
        address(query.rectangle);
        shapes.push_back(0);
        return invoke(0x87bf88);
    }

    bool layer_flag(HalfTile tile, uint32_t layer) {
        address(memory.read<uint64_t>(base + 0x4ab2010) + 0x10);
        shapes.insert(shapes.end(), {uint32_t(tile.x), uint32_t(tile.z), layer});
        return invoke(0x87c048);
    }

    bool property(uint32_t material, uint32_t site) {
        address(memory.read<uint64_t>(memory.read<uint64_t>(base + 0x4ab46c8)));
        shapes.push_back(material);
        return invoke(site);
    }

    bool rejected(uint32_t material, unsigned corner) {
        constexpr std::array sites{0x87c0bcu, 0x87c0fcu, 0x87c13cu, 0x87c17cu};
        return property(material, sites[corner]);
    }

    bool requirement(uint32_t material, unsigned corner) {
        constexpr std::array sites{0x87c0ecu, 0x87c12cu, 0x87c16cu, 0x87c1acu};
        return property(material, sites[corner]);
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t base, query;
    size_t byte_count, reply_count;
    while (std::cin >> base >> query >> byte_count >> reply_count) {
        FixtureMemory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Reply> replies(reply_count);
        for (auto& reply : replies)
            std::cin >> reply.site >> reply.result;
        Services services{base, memory, replies};
        const bool result = argc == 1 && shovel_alternate_terrain(base, {query}, memory, services);
        std::cout << result << ' ' << services.next;
        for (auto value : services.shapes) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
