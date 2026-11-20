#include "../games/acnh/terrain_cull_predicates.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>

struct ReplayMemory {
    std::array<uint32_t, 81> context{};
    std::array<uint32_t, 4> point{};
    uint32_t gate{};

    template<class T>
    T read(uint64_t address) const {
        static_assert(sizeof(T) == sizeof(uint32_t));
        if (address >= 0x20000 && address < 0x20000 + 4 * context.size())
            return std::bit_cast<T>(context[(address - 0x20000) / 4]);
        if (address >= 0x22000 && address < 0x22000 + 4 * point.size())
            return std::bit_cast<T>(point[(address - 0x22000) / 4]);
        if (address == 0x24004) return std::bit_cast<T>(gate);
        std::abort();
    }
};

int main() {
    unsigned mode;
    while (std::cin >> mode) {
        ReplayMemory memory;
        const auto read = [](auto& words) {
            for (auto& word : words)
                if (!(std::cin >> word)) return false;
            return true;
        };
        uint32_t radius_bits;
        if (!read(memory.point) || !(std::cin >> memory.gate >> radius_bits) || !read(memory.context)) return 1;
        if (mode == 0) {
            const float radius = std::bit_cast<float>(radius_bits);
            std::cout << nx::acnh::intersects_terrain_cull_volume({0x20000}, {0x22000}, radius, memory)
                      << ' ' << radius_bits << '\n';
        } else if (mode == 1) {
            const auto result = nx::acnh::passes_terrain_cull_plane({0x20000}, {0x22000}, {0x24000}, memory);
            std::cout << result.visible << ' ' << std::bit_cast<uint32_t>(result.scalar) << '\n';
        } else {
            return 1;
        }
    }
}
