#include "../games/acnh/shovel_item_queries.h"

#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint64_t base = 0x4000000000;
constexpr uint64_t item_address = 0x20000;
constexpr uint64_t descriptor = 0x30000;

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t result = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            result |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(result);
    }

    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
    }
};

struct Services {
    unsigned mode;
    unsigned category_number;
    Memory& memory;
    std::vector<uint64_t> calls;

    void call(uint64_t entry, uint64_t arg) {
        calls.push_back(entry);
        calls.push_back(arg == item_address ? arg : 0);
    }

    uint32_t item_id(nx::acnh::FieldItem item, nx::acnh::ShovelItemIdUse) {
        call(0x267d6c4, item.address);
        return memory.read<uint16_t>(item.address);
    }

    nx::acnh::ItemDescriptor copy_alternate(nx::acnh::FieldItem item) {
        memory.write(descriptor + 8, memory.read<uint64_t>(item.address));
        memory.write<uint64_t>(descriptor + 0x10, 0);
        return {descriptor};
    }

    nx::acnh::FieldItem copy_special(nx::acnh::FieldItem item, uint16_t) {
        return {copy_alternate(item).address + 8};
    }

    nx::acnh::FieldItem copy_fallback(nx::acnh::FieldItem item) {
        return {copy_alternate(item).address + 8};
    }

    bool recognized(nx::acnh::FieldItem item) {
        call(0x267da10, item.address);
        return mode != 0;
    }

    bool blocked_by_alternate(nx::acnh::FieldItem item) {
        call(0x267e024, item.address);
        return mode == 1;
    }

    bool metadata_enabled(nx::acnh::ItemDescriptor item) {
        call(0x19e59b0, item.address);
        calls.push_back(memory.read<uint16_t>(item.address + 8));
        calls.push_back(memory.read<uint64_t>(item.address + 0x10));
        return mode != 2;
    }

    bool metadata_suitable(nx::acnh::ItemDescriptor item) {
        call(0x19e6020, item.address);
        return mode != 3;
    }

    nx::acnh::ItemDescriptor describe(nx::acnh::FieldItem item) {
        call(0x114c030, 0);
        return copy_alternate(item);
    }

    bool has_category(nx::acnh::ItemDescriptor item) {
        call(0x114dcb0, item.address);
        return mode == 4;
    }

    bool category_flag(nx::acnh::CatalogEntry row, uint32_t) {
        call(0x113d890, row.address);
        return mode == 5;
    }

    uint32_t category(nx::acnh::FieldItem item, nx::acnh::CatalogEntry, size_t) {
        call(0x113db50, item.address);
        return category_number;
    }
};

}

int main() {
    unsigned entry, mode, category;
    size_t byte_count;
    while (std::cin >> entry >> mode >> category >> byte_count) {
        Memory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const nx::acnh::ShovelItemQueries queries{base, memory};
        constexpr nx::acnh::FieldItem item{item_address};
        Services services{mode, category, memory, {}};
        const bool result = entry == 0 ? queries.recognized(item, services)
            : entry == 1 ? queries.blocked_by_alternate(item, services) : queries.suitable(item, services);
        std::cout << result;
        for (const auto value : services.calls) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
