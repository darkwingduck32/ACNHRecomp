#include "../games/acnh/shovel_marker.h"
#include "fixture_memory.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;
using Words = std::vector<uint32_t>;
constexpr uint64_t fixture_corners = 0x70000000;

struct Memory : FixtureMemory {
    std::vector<uint64_t> writes;

    template<class T>
    void write(uint64_t address, T value) {
        writes.insert(writes.end(), {address, sizeof(T), uint64_t(value)});
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8*i)) & 0xff;
    }
};

struct Reply {
    uint32_t site;
    uint64_t result;
    std::array<uint32_t, 4> outputs;
};

struct Services {
    Memory& memory;
    const std::vector<Reply>& replies;
    size_t next = 0;
    Words shapes;

    const Reply& call(uint32_t expected) {
        if (next >= replies.size() || replies[next].site != expected) {
            std::cerr << "unexpected marker callback " << std::hex << expected << std::dec
                      << " in slot " << next << '\n';
            std::exit(1);
        }
        return replies[next++];
    }

    void address(uint64_t value) {
        shapes.push_back(uint32_t(value));
        shapes.push_back(uint32_t(value >> 32));
    }

    void tile(uint64_t type, HalfTile tile) {
        address(type);
        shapes.push_back(uint32_t(tile.x));
        shapes.push_back(uint32_t(tile.z));
    }

    void descriptor(uint64_t point) {
        tile(memory.read<uint64_t>(point), {memory.read<int32_t>(point + 8), memory.read<int32_t>(point + 12)});
    }

    bool test_direct(TerrainOwner owner, HalfTile point, uint64_t type, unsigned probe) {
        constexpr std::array sites{0x15ea1b0u, 0x15ea290u, 0x15ea398u, 0x15ea490u, 0x15ea58cu};
        address(owner.address);
        tile(type, point);
        shapes.insert(shapes.end(), {256, 0, 0});
        return (call(sites[probe]).result & 1) != 0;
    }

    bool test_global(HalfTile point, uint64_t type, unsigned probe) {
        constexpr std::array sites{0x15ea624u, 0x15ea6bcu, 0x15ea75cu, 0x15ea824u};
        tile(type, point);
        return (call(sites[probe]).result & 1) != 0;
    }

    bool test(TerrainOwner owner, uint64_t point) {
        address(owner.address);
        descriptor(point);
        shapes.insert(shapes.end(), {256, 0, 0});
        return (call(0x876398).result & 1) != 0;
    }

    TerrainQuadrants prepare_corners() {
        TerrainQuadrants corners{{fixture_corners, fixture_corners+4, fixture_corners+8, fixture_corners+12}};
        for (auto address : corners.output) memory.write<uint32_t>(address, 0);
        return corners;
    }

    bool sample_corners(TerrainOwner owner, uint64_t point, TerrainQuadrants corners) {
        address(owner.address);
        descriptor(point);
        for (auto address : corners.output) shapes.push_back(memory.read<uint32_t>(address));
        const auto& answer = call(0x849af8);
        for (size_t i = 0; i < corners.output.size(); ++i)
            memory.write<uint32_t>(corners.output[i], answer.outputs[i]);
        return (answer.result & 1) != 0;
    }

    std::array<uint8_t, 4> sample_kinds(TerrainGrid grid, uint64_t point) {
        address(grid.address);
        descriptor(point);
        const auto kinds = call(0x849458).outputs[0];
        return {uint8_t(kinds), uint8_t(kinds >> 8), uint8_t(kinds >> 16), uint8_t(kinds >> 24)};
    }

    uint32_t primary_kind(TerrainWorldPoint& point, unsigned corner) {
        constexpr std::array sites{0x8494bcu, 0x8494f4u, 0x849528u, 0x84955cu};
        shapes.push_back(std::bit_cast<uint32_t>(point.x));
        shapes.push_back(std::bit_cast<uint32_t>(point.z));
        const auto& answer = call(sites[corner]);
        point = {std::bit_cast<float>(answer.outputs[0]), std::bit_cast<float>(answer.outputs[1])};
        return uint32_t(answer.result);
    }
};
}

int main(int argc, char**) {
    uint64_t entry, base;
    std::array<uint64_t, 6> args;
    uint32_t radius;
    size_t byte_count, reply_count;
    while (std::cin >> entry >> base >> radius >> byte_count >> reply_count) {
        for (auto& arg : args) std::cin >> arg;
        Memory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Reply> replies(reply_count);
        for (auto& reply : replies) {
            std::cin >> reply.site >> reply.result;
            for (auto& value : reply.outputs) std::cin >> value;
        }
        Services services{memory, replies};
        const TerrainQuadrants corners{{args[2], args[3], args[4], args[5]}};
        bool result = false;
        if (argc == 1) {
            switch (entry) {
                case 0x15ea0c8:
                    result = shovel_marker_extent(base, {args[0], std::bit_cast<float>(radius)}, memory, services);
                    break;
                case 0x876320:
                    result = shovel_marker_global(base, args[0], memory, services);
                    break;
                case 0x849ac8:
                    result = shovel_marker_match(args[0], args[1], args[2], memory, services);
                    break;
                case 0x8493f8:
                    result = shovel_marker_corners(args[0], args[1], corners, memory, services);
                    break;
                default:
                    return 2;
            }
        }
        std::cout << result << ' ' << services.next;
        if (entry == 0x8493f8)
            for (auto address : corners.output) std::cout << ' ' << memory.read<uint32_t>(address);
        for (auto value : services.shapes) std::cout << ' ' << value;
        if (entry == 0x8493f8)
            for (auto value : memory.writes) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
