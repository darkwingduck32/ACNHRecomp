#include "../games/acnh/shovel_stage.h"

#include <iostream>
#include <vector>

namespace {

struct Services {
    unsigned mode;
    std::vector<uint64_t> calls;

    bool primary(nx::acnh::ShovelStageKey key) {
        calls.insert(calls.end(), {0x16f37d0, key.hash});
        return mode == 1 || (mode == 3 && nx::acnh::shovel_stage_primary(key));
    }

    bool secondary(nx::acnh::ShovelStageKey key) {
        calls.insert(calls.end(), {0x16f3a18, key.hash});
        return mode == 2 || (mode == 3 && nx::acnh::shovel_stage_secondary(key));
    }
};

}

int main() {
    unsigned entry, mode;
    nx::acnh::ShovelStageKey key{};
    while (std::cin >> entry >> mode >> key.hash) {
        Services services{mode, {}};
        const bool result = entry == 0 ? nx::acnh::shovel_stage_primary(key)
            : entry == 1 ? nx::acnh::shovel_stage_secondary(key)
            : nx::acnh::shovel_stage_matches(key, services);
        std::cout << result;
        for (const auto value : services.calls) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
