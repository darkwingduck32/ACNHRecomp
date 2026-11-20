#include "../games/acnh/shovel_intermediate.h"
#include "fixture_memory.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;
uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }

struct Action { uint64_t site, result; uint32_t height; };

struct Services {
    const FixtureMemory& memory;
    const std::vector<Action>& actions;
    size_t next = 0;
    ShovelTargetPoint offset{};
    std::vector<uint32_t> shapes;

    const Action& invoke(uint64_t site) {
        if (next >= actions.size() || actions[next].site != site) {
            std::cerr << "unexpected intermediate callback " << std::hex << site << std::dec
                      << " in slot " << next << '\n';
            std::exit(1);
        }
        return actions[next++];
    }

    void shape(std::initializer_list<uint32_t> words) {
        shapes.insert(shapes.end(), words.begin(), words.end());
    }

    void xyz(ShovelTargetPoint point) {
        shape({bits(point.x), bits(point.y), bits(point.z)});
    }

    ShovelTargetPoint point_at(uint64_t address) const {
        const auto read = [&](uint64_t off) { return std::bit_cast<float>(memory.read<uint32_t>(address + off)); };
        return {read(0), read(4), read(8)};
    }

    bool blocked_at_center(uint64_t point, float radius) {
        xyz(point_at(point));
        shape({bits(radius)});
        return invoke(0x15e9ccc).result & 1;
    }

    float surface_height(uint64_t point, ShovelIntermediateHeight use) {
        xyz(point_at(point));
        shape({0, 0, 0});
        return std::bit_cast<float>(invoke(use == ShovelIntermediateHeight::center_surface ? 0x15e9d68 : 0x15e9cec).height);
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, bool center) {
        shape({uint32_t(owner), uint32_t(owner >> 32), index});
        const auto entry = invoke(center ? 0x15e9d2c : 0x15e9fe0).result;
        const auto name = memory.read<uint64_t>(entry);
        shape({uint32_t(name), uint32_t(name >> 32), 0});
        return uint32_t(invoke(center ? 0x15e9d38 : 0x15e9fec).result);
    }

    float stage_height(uint64_t point, uint16_t ordinal) {
        xyz(point_at(point));
        shape({ordinal, 0, 1, 0, 0, 0});
        return std::bit_cast<float>(invoke(0x15e9df8).height);
    }

    void prepare_offset(ShovelTargetPoint point) { offset = point; }

    float offset_surface_height() {
        xyz(offset);
        shape({0,0,0});
        return std::bit_cast<float>(invoke(0x15e9f9c).height);
    }

    float offset_stage_height(uint16_t ordinal) {
        xyz(offset);
        shape({ordinal, 0, 1, 0, 0, 0});
        return std::bit_cast<float>(invoke(0x15ea088).height);
    }

    bool cast(ShovelTargetPoint point, float radius, ShovelIntermediateCast use) {
        constexpr std::array sites{0x15e9e68u, 0x15e9f80u, 0x15e9edcu};
        xyz(point);
        shape({bits(radius), bits(1.f), use == ShovelIntermediateCast::skip_offset ? 1u : 0x10u, 0});
        return uint32_t(invoke(sites[static_cast<size_t>(use)]).result) != 0;
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t base, point;
    unsigned radius, flags;
    size_t memory_size, action_count;
    while (std::cin >> base >> point >> radius >> flags >> memory_size >> action_count) {
        FixtureMemory memory;
        for (size_t i = 0; i < memory_size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Action> actions(action_count);
        for (auto& action : actions)
            std::cin >> action.site >> action.result >> action.height;
        Services services{memory, actions};
        const ShovelIntermediateQuery query{point, std::bit_cast<float>(radius),
            (flags & 1) == 0, (flags & 2) != 0, (flags & 4) != 0};
        const bool result = argc == 1 && shovel_intermediate_point(base, query, memory, services);
        std::cout << result << ' ' << services.next;
        for (const auto word : services.shapes) std::cout << ' ' << word;
        std::cout << '\n';
    }
}
