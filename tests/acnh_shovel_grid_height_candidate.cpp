#include "../games/acnh/shovel_grid_height.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    std::vector<uint64_t> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const auto bits = uint64_t(value);
        writes.insert(writes.end(), {address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = (bits >> (8 * i)) & 0xff;
    }
};
} // namespace

int main() {
    unsigned ground_only, count, value;
    uint64_t address;
    while (std::cin >> ground_only >> count) {
        Memory memory;
        while (count--) {
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        using namespace nx::acnh;
        constexpr uint64_t material = 0x22000, override_flag = 0x23000;
        const auto height = query_grid_height({{0x20000}, 0x21000, material, override_flag, ground_only != 0}, memory);
        std::cout << std::bit_cast<uint32_t>(height) << ' ' << memory.read<uint32_t>(material)
                  << ' ' << unsigned(memory.read<uint8_t>(override_flag));
        for (uint64_t word : memory.writes) std::cout << ' ' << word;
        std::cout << '\n';
    }
}
