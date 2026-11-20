#include "../games/acnh/field_provider_binding.h"
#include "../runtime/src/features/acnh_provider_member.h"

#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t result = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            result |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(result);
    }
};

}

int main() {
    unsigned entry;
    uint64_t base, context, selected;
    size_t count;
    while (std::cin >> entry >> base >> context >> selected >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }

        if (entry == 0) {
            std::vector<uint64_t> calls;
            const auto table = nx::acnh::resolve_provider_table(base, memory,
                [&](nx::acnh::ProviderBinding binding, bool extra_layer) {
                    calls.push_back(extra_layer ? 0x12673c0 : 0x1267390);
                    calls.push_back(binding.address);
                    return selected;
                });
            std::cout << table.address;
            for (const auto value : calls) std::cout << ' ' << value;
        } else {
            const auto member = nx::features::provider_member_target(context, memory);
            if (!member.present) std::cout << (entry == 1 ? 0 : context);
            else std::cout << member.receiver << ' ' << member.target << ' ' << member.receiver;
        }
        std::cout << '\n';
    }
}
