#include "../games/acnh/shovel_collision.h"
#include "fixture_memory.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

struct Reply {
    uint32_t site;
    uint64_t result;
    uint32_t scalar;
};

struct Services {
    uint64_t base;
    const FixtureMemory& memory;
    const std::vector<Reply>& replies;
    size_t next = 0;
    std::vector<uint32_t> shapes;
    uint32_t scalar = 0;

    const Reply& call(uint32_t site) {
        if (next == replies.size() || replies[next].site != site) {
            std::cerr << "unexpected collision callback " << std::hex << site << std::dec
                      << " in slot " << next << '\n';
            std::exit(1);
        }
        const auto& reply = replies[next++];
        scalar = reply.scalar;
        return reply;
    }

    void address(uint64_t value) {
        shapes.push_back(uint32_t(value));
        shapes.push_back(uint32_t(value >> 32));
    }

    void xyz(uint64_t point) {
        for (unsigned offset : {0u, 4u, 8u})
            shapes.push_back(memory.read<uint32_t>(point + offset));
    }

    float surface_height(uint64_t point, ShovelCollisionProbe probe) {
        xyz(point);
        shapes.insert(shapes.end(), {0, 0, 0});
        const auto site = probe == ShovelCollisionProbe::height_difference ? 0x15e11d0 : 0x15ec7b4;
        return std::bit_cast<float>(call(site).scalar);
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, ShovelCollisionProbe probe) {
        const auto site = probe == ShovelCollisionProbe::height_difference ? 0x15e1210 : 0x15ec85c;
        address(owner);
        shapes.push_back(index);
        const auto descriptor = call(site).result;
        address(memory.read<uint64_t>(descriptor));
        shapes.push_back(0);
        return uint32_t(call(site + 0xc).result);
    }

    float stage_height(uint64_t point, bool alternate, uint16_t ordinal) {
        xyz(point);
        shapes.insert(shapes.end(), {0, unsigned(alternate), 0, ordinal, 0, 1, scalar});
        return std::bit_cast<float>(call(0x15e12bc).scalar);
    }

    uint32_t stage_item_id(HalfTile tile, uint32_t key) {
        shapes.push_back(key);
        const auto flags = call(0x15ec898).result & 1;
        address(memory.read<uint64_t>(base + 0x4abfe68) + 0x10);
        shapes.insert(shapes.end(), {0, key, uint32_t(flags), 0});
        const auto type = memory.read<uint64_t>(base + 0x4ab2010) + 0x10;
        address(type);
        shapes.insert(shapes.end(), {uint32_t(tile.x), uint32_t(tile.z)});
        address(type);
        shapes.insert(shapes.end(), {0, 0});
        const auto record = call(0x15ec8b4).result;
        address(record);
        return uint32_t(call(0x15ec8b8).result);
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t entry, base, point, first, second;
    size_t byte_count, reply_count;
    while (std::cin >> entry >> base >> point >> first >> second >> byte_count >> reply_count) {
        FixtureMemory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Reply> replies(reply_count);
        for (auto& reply : replies)
            std::cin >> reply.site >> reply.result >> reply.scalar;
        Services services{base, memory, replies};
        bool result = false;
        if (argc == 1) {
            if (entry == 0x15e11a0) {
                result = shovel_collision_height_difference(base, {point, bool(first & 1), bool(second & 1)}, memory, services);
            } else if (entry == 0x15ec764) {
                result = shovel_collision_stage_item(base, {point, bool(first & 1)}, memory, services);
            } else {
                return 2;
            }
        }
        std::cout << result << ' ' << services.next;
        for (auto value : services.shapes) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
