#include "../games/acnh/field_provider.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

int main() {
    uint64_t object, address, resolved;
    uint32_t operation, x, z, count, value;
    while (std::cin >> operation >> object >> x >> z >> resolved >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        std::vector<uint64_t> calls;
        uint64_t result = 0;
        switch (operation) {
            case 0:
                result = nx::acnh::provider_table({object}, memory, [&](uint64_t target) {
                    calls.push_back(target);
                    return nx::acnh::ProviderTable{resolved};
                }).address;
                break;
            case 1: result = nx::acnh::provider_ready(); break;
            case 2: result = nx::acnh::provider_width(); break;
            case 3: result = nx::acnh::provider_height(); break;
            case 4: result = nx::acnh::provider_cell_offset({object}, {x, z}, memory); break;
        }
        std::cout << result;
        for (uint64_t target : calls) std::cout << ' ' << target;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
