#include "../games/acnh/shovel_obstacle.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

constexpr std::array sites{0x15ea868u, 0x15ea8ccu, 0x15ea950u, 0x15ea9c4u, 0x15eaa3cu,
                           0x15eaab0u, 0x15eab28u, 0x15eaba4u, 0x15eac20u};

struct Services {
    FixtureMemory& memory;
    unsigned hit;
    uint32_t previous = 0;
    std::vector<std::array<uint64_t, 6>> probes;

    uint32_t material(uint64_t owner, ShovelTargetPoint point, unsigned index) {
        probes.push_back({sites[index], owner, std::bit_cast<uint32_t>(point.x), std::bit_cast<uint32_t>(point.y),
                          std::bit_cast<uint32_t>(point.z), previous});
        return previous = index == hit ? 0x1cu : 3u + index;
    }

    uint32_t material(uint64_t owner, uint64_t address, unsigned index) {
        const auto read = [&](uint64_t offset) { return std::bit_cast<float>(memory.read<uint32_t>(address + offset)); };
        return material(owner, ShovelTargetPoint{read(0), read(4), read(8)}, index);
    }
};
} // namespace

int main() {
    unsigned radius, hit, count, byte;
    uint64_t address;
    while (std::cin >> radius >> hit >> count) {
        FixtureMemory memory;
        while (count--) {
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        Services services{memory, hit};
        constexpr uint64_t base = 0x4000000000, point = 0x20000;
        std::cout << nx::acnh::shovel_point_obstructed(base, {point, std::bit_cast<float>(radius)}, memory, services)
                  << ' ' << services.probes.size();
        for (const auto& probe : services.probes) {
            for (const auto value : probe) std::cout << ' ' << value;
        }
        std::cout << '\n';
    }
}
