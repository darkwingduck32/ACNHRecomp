#include "../games/acnh/shovel_item_metadata.h"

#include <iostream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;

    std::vector<uint64_t> writes;

    template<class T>
    void write(uint64_t address, T value) {
        uint64_t bits = static_cast<std::make_unsigned_t<T>>(value);
        writes.insert(writes.end(), {address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = (bits >> (8 * i)) & 0xff;
    }

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

    bool has_special_kind(nx::acnh::ItemDescriptor descriptor) {
        calls.push_back(0x19f9760);
        calls.push_back(descriptor.address);
        return (flags & 1) != 0;
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

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
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
        if (mode == "--predicate") {
            std::cout << nx::acnh::shovel_metadata_enabled(base, {descriptor}, memory);
            for (const auto value : memory.writes) std::cout << ' ' << value;
        } else if (mode == "--alternate") {
            std::cout << nx::acnh::shovel_alternate_kind(base, {descriptor}, memory, services);
        } else {
            std::cout << nx::acnh::shovel_metadata_kind(base, {descriptor}, memory, services);
        }
        for (const auto site : services.calls) std::cout << ' ' << site;
        std::cout << '\n';
    }
}
