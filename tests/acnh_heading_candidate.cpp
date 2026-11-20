#include "../games/acnh/heading.h"

#include <array>
#include <bit>
#include <iostream>

int main() {
    std::array<nx::acnh::HeadingSample, 129> table;
    for (auto& sample : table) {
        uint32_t slope;
        if (!(std::cin >> sample.angle >> slope)) return 1;
        sample.slope = std::bit_cast<float>(slope);
    }
    uint32_t x, z;
    while (std::cin >> x >> z) {
        std::cout << nx::acnh::heading(std::bit_cast<float>(x), std::bit_cast<float>(z), table) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
