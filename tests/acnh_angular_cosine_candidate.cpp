#include "../games/acnh/angular_cosine.h"

#include <array>
#include <bit>
#include <iostream>

int main() {
    std::array<nx::acnh::CosineSample, 129> table;
    for (auto& sample : table) {
        uint32_t base, slope;
        if (!(std::cin >> base >> slope)) return 1;
        sample = {std::bit_cast<float>(base), std::bit_cast<float>(slope)};
    }
    uint32_t a, b;
    while (std::cin >> a >> b)
        std::cout << std::bit_cast<uint32_t>(nx::acnh::angular_cosine(a, b, table)) << '\n';
    return std::cin.eof() ? 0 : 1;
}
