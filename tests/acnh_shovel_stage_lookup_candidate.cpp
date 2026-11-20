#include "../games/acnh/shovel_stage_lookup.h"

#include <iostream>

int main() {
    uint64_t base;
    nx::acnh::ShovelStageIndex index{};
    while (std::cin >> base >> index.value)
        std::cout << nx::acnh::shovel_stage_name_record(base, index).address << '\n';
}
