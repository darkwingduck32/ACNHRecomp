#include "../games/acnh/shovel_sweep_collision.h"
#include "../games/acnh/shovel_terrain.h"
#include "fixture_memory.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
float real(uint32_t value) { return std::bit_cast<float>(value); }

struct WritableMemory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        for (size_t offset = 0; offset < sizeof(T); ++offset)
            bytes.at(address + offset) = uint8_t(uint64_t(value) >> (8 * offset));
        writes.push_back({address, sizeof(T), uint64_t(value)});
    }
};

struct Reply {
    uint32_t site;
    uint64_t result;
    bool has_effect;
    std::array<uint32_t, 3> effect;
};

struct Services {
    uint64_t base;
    const FixtureMemory& memory;
    const std::vector<Reply>& replies;
    size_t next = 0;
    std::vector<uint64_t> arguments;

    const Reply& call(uint32_t site, std::initializer_list<uint64_t> args) {
        if (next == replies.size() || replies[next].site != site) {
            std::cerr << "Unexpected child site " << std::hex << site << " at callback " << std::dec << next << '\n';
            std::exit(1);
        }
        arguments.push_back(site);
        arguments.insert(arguments.end(), args);
        return replies[next++];
    }

    bool height_probe(ShovelTargetPoint& point, bool center, float scalar) {
        const auto& answer = call(center ? 0x15ede4c : 0x15ef5ac,
                                  {bits(point.x), bits(point.y), bits(point.z), 1, 0, bits(scalar)});
        if (answer.has_effect)
            point = {real(answer.effect[0]), real(answer.effect[1]), real(answer.effect[2])};
        return (answer.result & 1) != 0;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, bool center) {
        const auto site = center ? 0x15ef99c : 0x15ef674;
        const auto name = call(site, {owner, index}).result;
        return static_cast<uint32_t>(call(site + 0xc, {memory.read<uint64_t>(name), 0}).result);
    }

    ShovelSweepCollisionRoots contact_roots(ShovelTargetPoint point, bool center) {
        const auto key = shovel_step_cast_stage_key(base, center, memory, *this);
        const auto allowed = call(center ? 0x15ef9e0 : 0x15ef6e8, {key}).result & 1;
        const auto& response = call(center ? 0x15ef9fc : 0x15ef704,
            {memory.read<uint64_t>(base + 0x4abfe68) + 0x10, 0, key, allowed, 0,
             memory.read<uint64_t>(base + 0x4ab2010) + 0x10, uint32_t(terrain_half_tile(point.x)),
             uint32_t(terrain_half_tile(point.z)), memory.read<uint64_t>(base + 0x4ab2010) + 0x10, 0, 0});
        const auto reference = memory.read<uint64_t>(base + 0x4ab5d40);
        return {{int32_t(response.effect[0]), int32_t(response.effect[1])},
                {memory.read<int32_t>(reference + 8), memory.read<int32_t>(reference + 12)}};
    }
};
} // namespace

int main() {
    uint64_t base, request, contact, flag;
    size_t byte_count, reply_count;
    while (std::cin >> base >> request >> contact >> flag >> byte_count >> reply_count) {
        WritableMemory memory;
        for (size_t index = 0; index < byte_count; ++index) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Reply> replies(reply_count);
        for (auto& answer : replies)
            std::cin >> answer.site >> answer.result >> answer.has_effect
                     >> answer.effect[0] >> answer.effect[1] >> answer.effect[2];
        Services services{base, memory, replies};
        const auto read = [&](uint64_t offset) { return real(memory.read<uint32_t>(request + offset)); };
        const ShovelSweepCollisionQuery query{{read(0), read(4), read(8)}, read(12) * read(16), contact, flag};
        const auto result = shovel_sweep_collision(base, query, memory, services);
        std::cout << result << ' ' << services.next << ' ' << memory.writes.size();
        for (const auto& write : memory.writes)
            for (auto value : write) std::cout << ' ' << value;
        for (auto value : services.arguments) std::cout << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
