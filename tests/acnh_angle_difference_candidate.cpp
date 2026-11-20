#include "../games/acnh/angle_difference.h"

#include <bit>
#include <iostream>

int main() {
    uint32_t a, b;
    while (std::cin >> a >> b) {
        const auto result = nx::acnh::angle_difference(std::bit_cast<float>(a), std::bit_cast<float>(b));
        std::cout << static_cast<uint32_t>(result.direction) << ' '
                  << std::bit_cast<uint32_t>(result.distance) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
