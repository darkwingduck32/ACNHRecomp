#include "../games/acnh/shovel_target.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {

using namespace nx::acnh;

struct Memory : FixtureMemory {
    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint64_t>(value) >> (8 * i) & 0xff;
    }
};

struct Services {
    uint32_t angle;
    std::array<uint32_t, 12> outcomes;
    std::array<uint32_t, 3> heights;
    std::vector<uint32_t> calls;
    std::vector<ShovelTargetPoint> points;
    unsigned measured = 0;

    uint32_t heading(float, float) {
        calls.push_back(0x15f3744);
        return angle;
    }

    float height(ShovelTargetRequest, ShovelTargetHeight which) {
        constexpr std::array sites{0x15f3928u, 0x15f3e70u, 0x15f3e88u};
        calls.push_back(sites[size_t(which)]);
        return std::bit_cast<float>(heights.at(measured++));
    }

    uint32_t probe(ShovelTargetPoint point, ShovelTargetRequest, ShovelTargetProbe method, size_t index) {
        constexpr std::array approach{0x15f39c4u, 0x15f3a20u, 0x15f3a74u};
        constexpr std::array nearby{0x15f3b80u, 0x15f3bd4u, 0x15f3c28u, 0x15f3c7cu, 0x15f3cd0u,
                                    0x15f3d24u, 0x15f3d84u, 0x15f3dd8u, 0x15f3e38u};
        calls.push_back(method == ShovelTargetProbe::approach ? approach.at(index)
            : method == ShovelTargetProbe::radius ? 0x15f3b3cu : nearby.at(index));
        points.push_back(point);
        return outcomes.at(points.size() - 1);
    }
};

} // namespace

int main() {
    uint32_t angle;
    while (std::cin >> angle) {
        Services services{angle};
        for (auto& value : services.outcomes) std::cin >> value;
        for (auto& value : services.heights) std::cin >> value;
        unsigned count;
        std::cin >> count;
        Memory memory;
        for (unsigned i = 0; i < count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        constexpr uint64_t request = 0x20000;
        std::cout << select_shovel_target(0x4000000000, {request}, memory, services);
        for (uint64_t off = 0; off < 0x48; off += 4)
            std::cout << ' ' << memory.read<uint32_t>(request + off);
        std::cout << ' ' << services.calls.size();
        for (auto site : services.calls) std::cout << ' ' << site;
        for (auto point : services.points)
            std::cout << ' ' << std::bit_cast<uint32_t>(point.x) << ' ' << std::bit_cast<uint32_t>(point.y)
                      << ' ' << std::bit_cast<uint32_t>(point.z);
        std::cout << '\n';
    }
}
