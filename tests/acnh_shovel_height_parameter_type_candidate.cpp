#include "../games/acnh/shovel_height.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    std::vector<uint64_t> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = value;
        writes.insert(writes.end(), {address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i) bytes.at(address + i) = (bits >> (8 * i)) & 0xff;
    }
};
using nx::acnh::HeightParameterType;

struct Services {
    Memory& memory;
    unsigned acquired, delegated;
    std::vector<std::array<uint64_t, 2>> calls;

    uint8_t guard_byte(uint64_t address) const { return memory.read<uint8_t>(address); }
    bool acquire_guard(uint64_t address, HeightParameterType kind) {
        calls.push_back({kind == HeightParameterType::primary ? 0x226895cull : 0x22689a8ull, address});
        return acquired;
    }
    void release_guard(uint64_t address, HeightParameterType kind) {
        calls.push_back({kind == HeightParameterType::primary ? 0x2268988ull : 0x22689d4ull, address});
        memory.write<uint8_t>(address, 1);
    }
    uint32_t matches_base_type(uint64_t requested) {
        calls.push_back({0x226893c, requested});
        return delegated;
    }
};
} // namespace

int main() {
    unsigned acquired, delegated, count, value;
    uint64_t requested, address;
    while (std::cin >> acquired >> delegated >> requested >> count) {
        Memory memory;
        while (count--) {
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        Services services{memory, acquired, delegated, {}};
        std::cout << nx::acnh::height_parameter_type_matches(0x4000000000, requested, memory, services)
                  << ' ' << services.calls.size();
        for (const auto& call : services.calls) std::cout << ' ' << call[0] << ' ' << call[1];
        for (const auto value : memory.writes) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
