#include "../games/acnh/terrain_worker_visibility.h"
#include "fixture_memory.h"

#include <bit>
#include <iostream>

int main() {
    using namespace nx::acnh;
    constexpr uint64_t planes = 0x21000;
    unsigned count;
    uint32_t x, y, z, margin, first;
    while (std::cin >> count >> x >> y >> z >> margin >> first) {
        FixtureMemory memory;
        for (unsigned i = 0; i < count * 4; ++i) {
            uint32_t word;
            std::cin >> word;
            for (unsigned byte = 0; byte < 4; ++byte)
                memory.bytes[planes + 4*i + byte] = (word >> (8*byte)) & 0xff;
        }
        const auto f32 = [](uint32_t word) { return std::bit_cast<float>(word); };
        const auto result = terrain_worker_remaining_planes({planes, count}, {f32(x), f32(y), f32(z)},
                                                            f32(margin), f32(first), memory);
        std::cout << result.passes << ' ' << std::bit_cast<uint32_t>(result.scalar)
                  << ' ' << result.tested << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
