#include "../games/acnh/shovel_item_metadata.h"

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

struct Services {
    unsigned flags;
    std::vector<uint64_t> calls;

    uint32_t item_id(nx::acnh::FieldItem) {
        calls.push_back(0x19ee3e0);
        return 7;
    }

    uint32_t alternate_kind(nx::acnh::ItemDescriptor) {
        calls.push_back(0x19ee424);
        return flags & 1 ? 10 : 0;
    }

    bool metadata_enabled(nx::acnh::ItemDescriptor, size_t path) {
        constexpr uint64_t sites[]{0x19ee440, 0x19ee44c, 0x19ee46c};
        calls.push_back(sites[path]);
        return (flags & 2) != 0;
    }

    nx::acnh::CatalogEntry extended_row(nx::acnh::ItemDescriptor) {
        calls.push_back(0x19ee718);
        return {flags & 4 ? 0x2b034ull : 0};
    }
};

}

int main() {
    uint64_t base, descriptor;
    unsigned flags;
    size_t count;
    while (std::cin >> base >> descriptor >> flags >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        Services services{flags, {}};
        std::cout << nx::acnh::shovel_metadata_kind(base, {descriptor}, memory, services);
        for (const auto site : services.calls) std::cout << ' ' << site;
        std::cout << '\n';
    }
}
