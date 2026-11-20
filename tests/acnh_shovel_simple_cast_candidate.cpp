#include "../games/acnh/shovel_mixed_cast.h"
#include "fixture_memory.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

struct Reply {
    uint32_t site;
    uint64_t result;
    bool moves_point;
    std::array<uint32_t, 3> point;
    uint32_t material;
};

uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }

float read_float(const FixtureMemory& memory, uint64_t address) {
    return std::bit_cast<float>(memory.read<uint32_t>(address));
}

struct Services {
    uint64_t base;
    const FixtureMemory& memory;
    const std::vector<Reply>& replies;
    size_t next = 0;
    std::vector<uint32_t> shapes;

    const Reply& call(uint32_t site) {
        if (next == replies.size() || replies[next].site != site) {
            std::cerr << "unexpected simple cast callback " << std::hex << site << std::dec
                      << " in slot " << next << '\n';
            std::exit(1);
        }
        return replies[next++];
    }

    void address(uint64_t value) {
        shapes.push_back(uint32_t(value));
        shapes.push_back(uint32_t(value >> 32));
    }

    static void update_point(ShovelTargetPoint& point, const Reply& answer) {
        if (answer.moves_point)
            point = {std::bit_cast<float>(answer.point[0]), std::bit_cast<float>(answer.point[1]),
                     std::bit_cast<float>(answer.point[2])};
    }

    uint32_t cast_material(ShovelTargetPoint& point, ShovelCastMaterial use, bool center) {
        constexpr std::array center_sites{0x15ed35cu, 0x15ed370u, 0x15ed80cu, 0x15ed870u};
        constexpr std::array rim_sites{0x15eed74u, 0x15eed88u, 0x15ef1e0u, 0x15ef240u};
        const auto owner_slot = memory.read<uint64_t>(base + 0x4ab27c8);
        address(memory.read<uint64_t>(owner_slot) + 0x28);
        shapes.insert(shapes.end(), {bits(point.x), bits(point.y), bits(point.z), 0});
        const auto index = static_cast<size_t>(use);
        const auto& answer = call((center ? center_sites : rim_sites)[index]);
        update_point(point, answer);
        return answer.material;
    }

    bool cast_tile_flag(HalfTile tile, bool center) {
        address(memory.read<uint64_t>(base + 0x4ab2018) + 0x10);
        shapes.insert(shapes.end(), {uint32_t(tile.x), uint32_t(tile.z)});
        return (call(center ? 0x15ed698 : 0x15ef098).result & 1) != 0;
    }

    bool material_flag(uint32_t kind, ShovelCastProperty property, bool center) {
        const bool ground = property == ShovelCastProperty::base;
        const auto pointer = memory.read<uint64_t>(base + 0x4ab46c8);
        address(memory.read<uint64_t>(pointer) + 8);
        shapes.push_back(kind);
        const auto row_site = ground ? (center ? 0x15ed390 : 0x15eeda8) : (center ? 0x15ed890 : 0x15ef260);
        const auto row = call(row_site).result;
        if (!row) return false;
        address(row);
        shapes.insert(shapes.end(), {ground ? 13u : 11u, ground ? 0x58da05edu : 0xe7e965dbu});
        const auto cache = memory.read<uint64_t>(base + (ground ? 0x4ab5d10 : 0x4ab5d28));
        address(memory.read<uint64_t>(cache));
        const auto column_site = ground ? (center ? 0x15ed3b0 : 0x15eedc8) : (center ? 0x15ed8b0 : 0x15ef280);
        const auto column = call(column_site).result;
        return column && memory.read<uint8_t>(column) != 0;
    }


    bool probe(ShovelTargetPoint& point, ShovelStepCastMask mask, bool center) {
        const bool height = mask == ShovelStepCastMask::height;
        const auto site = height ? (center ? 0x15ede4c : 0x15ef5ac) : (center ? 0x15edfd4 : 0x15ef890);
        shapes.insert(shapes.end(), {bits(point.x), bits(point.y), bits(point.z), 1});
        if (height) shapes.push_back(0);
        const auto& answer = call(site);
        update_point(point, answer);
        return (answer.result & 1) != 0;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, bool center) {
        const auto site = center ? 0x15ef99c : 0x15ef674;
        address(owner);
        shapes.push_back(index);
        const auto name = call(site).result;
        address(memory.read<uint64_t>(name));
        shapes.push_back(0);
        return uint32_t(call(site + 0xc).result);
    }

    void record_height_contact(ShovelTargetPoint point, uint32_t key, bool center) {
        const auto coordinate = [](float value) {
            return std::isnan(value) ? 0u : uint32_t(terrain_half_tile(value));
        };
        shapes.push_back(key);
        const auto permitted = call(center ? 0x15ef9e0 : 0x15ef6e8).result & 1;
        address(memory.read<uint64_t>(base + 0x4abfe68) + 0x10);
        shapes.insert(shapes.end(), {0, key, uint32_t(permitted), 0});
        const auto type = memory.read<uint64_t>(base + 0x4ab2010) + 0x10;
        address(type);
        shapes.insert(shapes.end(), {coordinate(point.x), coordinate(point.z)});
        address(type);
        shapes.insert(shapes.end(), {0, 0});
        call(center ? 0x15ef9fc : 0x15ef704);
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t base, point;
    size_t byte_count, reply_count;
    while (std::cin >> base >> point >> byte_count >> reply_count) {
        FixtureMemory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Reply> replies(reply_count);
        for (auto& answer : replies)
            std::cin >> answer.site >> answer.result >> answer.moves_point
                     >> answer.point[0] >> answer.point[1] >> answer.point[2] >> answer.material;
        Services services{base, memory, replies};
        const ShovelStepCastQuery query{{read_float(memory, point), read_float(memory, point + 4),
            read_float(memory, point + 8)}, read_float(memory, point + 0xc) * read_float(memory, point + 0x10),
            static_cast<ShovelStepCastMask>(memory.read<uint32_t>(point + 0x14))};
        uint32_t result = 0;
        if (argc == 1) {
            result = query.mask == ShovelStepCastMask::material
                ? shovel_mixed_step_cast(base, query, memory, services)
                : shovel_simple_step_cast(base, query, memory, services);
        }
        std::cout << result << ' ' << services.next;
        for (auto value : services.shapes) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
