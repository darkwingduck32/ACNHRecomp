#include "../games/acnh/shovel_height.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <type_traits>
#include <vector>

namespace {
using namespace nx::acnh;
constexpr uint64_t base = 0x4000000000;
constexpr uint64_t point = 0x20000, output = 0x21000;

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;
    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = static_cast<std::make_unsigned_t<T>>(value);
        writes.push_back({address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = (bits >> (8 * i)) & 0xff;
    }
};

struct Services {
    Memory& memory;
    uint32_t ground, overlay;
    std::vector<uint32_t> calls;

    HeightSample sample_base(uint64_t, uint64_t) {
        calls.push_back(0x15e02dc);
        return {-4.5f, ground};
    }
    HeightSample sample_overlay(uint64_t, uint64_t) {
        calls.push_back(0x15e02f4);
        return {7.5f, overlay};
    }
    float height_limit() {
        calls.insert(calls.end(), {0x15e0300, 0x15e0314});
        return 2.f;
    }
    bool material_property(uint32_t kind, HeightPropertyUse use) {
        constexpr std::array rec{0x15e034cu, 0x15e0394u, 0x15e03e8u, 0x15e0544u, 0x15e058cu};
        constexpr std::array col{0x15e0370u, 0x15e03a8u, 0x15e0408u, 0x15e0568u, 0x15e05a0u};
        const auto index = size_t(use);
        calls.insert(calls.end(), {rec[index], col[index]});
        return shovel_height_material_property(base, kind, memory);
    }
    float actor_height(uint64_t object) {
        calls.push_back(0x15e0670);
        return std::bit_cast<float>(memory.read<uint32_t>(object + 0x24));
    }
    float world_height() {
        calls.insert(calls.end(), {0x15e067c, 0x15e0694});
        return 1.f;
    }
};
} // namespace

int main() {
    uint32_t ground, overlay, controls, count, value;
    uint64_t address;
    while (std::cin >> ground >> overlay >> controls >> count) {
        Memory memory;
        while (count--) {
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        Services services{memory, ground, overlay};
        const auto query = nx::acnh::ShovelHeightQuery{point, controls == 1 || controls == 2 ? output : 0,
                                                       uint32_t(controls == 2), controls == 3 ? 0.f : 1.25f};
        std::cout << std::bit_cast<uint32_t>(shovel_height(base, query, memory, services))
                  << ' ' << memory.read<uint32_t>(output);
        for (uint64_t i = 0; i < 60; i += 4)
            std::cout << ' ' << memory.read<uint32_t>(base + shovel_height_column_cache + i);
        for (auto site : services.calls) std::cout << ' ' << site;
        for (const auto& write : memory.writes)
            for (const auto value : write) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
