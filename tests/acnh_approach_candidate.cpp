#include "../games/acnh/approach_position.h"

#include <bit>
#include <iostream>

int main() {
    uint32_t px, py, pz, tx, ty, tz, gain, maximum, minimum;
    while (std::cin >> px >> py >> pz >> tx >> ty >> tz >> gain >> maximum >> minimum) {
        const auto as_float = [](uint32_t bits) { return std::bit_cast<float>(bits); };
        const auto result = nx::acnh::approach_position(
            {as_float(px), as_float(py), as_float(pz)}, {as_float(tx), as_float(ty), as_float(tz)},
            as_float(gain), as_float(maximum), as_float(minimum));
        for (float value : result.position) std::cout << std::bit_cast<uint32_t>(value) << ' ';
        std::cout << std::bit_cast<uint32_t>(result.remaining) << ' ' << int(result.wrote) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
