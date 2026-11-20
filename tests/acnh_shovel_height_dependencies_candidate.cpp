#include "../games/acnh/shovel_height.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;
constexpr uint64_t base = 0x4000000000;

struct Memory : FixtureMemory {
    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = uint64_t(value) >> (8 * i) & 0xff;
    }
};

struct Services {
    const Memory& memory;
    uint64_t entry, mode;
    std::vector<uint32_t> calls;

    HeightSample ground_grid(uint64_t, uint64_t, uint32_t kind) {
        calls.push_back(0x83b0c8);
        return {4.5f, kind + 1};
    }
    void lock_overlay(uint64_t) { calls.push_back(0x83aa00); }
    void unlock_overlay(uint64_t) { calls.push_back(0x83aafc); }
    OverlayHeightSample overlay_grid(uint64_t object, uint64_t, uint32_t& kind, bool capture) {
        calls.push_back(capture ? 0x83aa74 : 0x83aad4);
        kind = memory.read<uint32_t>(object + 0x10);
        return {std::bit_cast<float>(memory.read<uint32_t>(object + 0x18)),
                memory.read<uint8_t>(object + 0x14) != 0};
    }
    uint32_t adjustment(uint64_t, TerrainFallback route) {
        if (route == TerrainFallback::secondary) {
            calls.push_back(0x83ab28);
            return mode == 3 ? 6 : 7;
        }
        calls.push_back(entry == 0 ? 0x83b0e0 : 0x83ab0c);
        return mode == 2 ? 5 : 7;
    }
    uint8_t guard_byte(uint64_t address) { return memory.read<uint8_t>(address); }
    bool acquire_guard(uint64_t, HeightParameterGuard guard) {
        calls.push_back(guard == HeightParameterGuard::type ? 0x241ea84 : 0x241ea20);
        return mode == 3 || mode == 4;
    }
    void release_guard(uint64_t, HeightParameterGuard guard) {
        calls.push_back(guard == HeightParameterGuard::type ? 0x241eaac : 0x241ea74);
    }
    bool parameter_type_matches(uint64_t, uint64_t) {
        calls.push_back(0x241e9e4);
        return mode != 4 && mode != 5;
    }
    void construct_parameters(uint64_t) { calls.push_back(0x241ea50); }
    void register_parameter_destructor(uint64_t) { calls.push_back(0x241ea68); }
};
} // namespace

int main() {
    uint64_t number,mode,input,output,count,address,value;
    while (std::cin >> number >> mode >> input >> output >> count) {
        Memory memory;
        while (count--) {
            std::cin >> address >> value;
            memory.bytes.emplace(address, static_cast<uint32_t>(value));
        }
        Services services{memory, number, mode};
        if (number < 2) {
            const float height = number == 0 ? shovel_ground_height(base, input, 0x21000, output, memory, services)
                                             : shovel_overlay_height(base, input, 0x21000, output, memory, services);
            std::cout << std::bit_cast<uint32_t>(height) << ' ' << memory.read<uint32_t>(0x22000);
        } else if (number == 2) {
            const auto result = height_parameters(base, static_cast<uint32_t>(input), memory, services);
            std::cout << result << ' ' << memory.read<uint64_t>(0x2d000);
        } else {
            std::cout << height_float_address(input);
        }
        for (auto site : services.calls) std::cout << ' ' << site;
        std::cout << '\n';
    }
}
