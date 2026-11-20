#include "../games/acnh/field_item_init.h"

#include <iostream>
#include <unordered_map>

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
    uint64_t base, address;
    size_t count;
    while (std::cin >> entry >> base >> address >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t byte_address;
            unsigned value;
            std::cin >> byte_address >> value;
            memory.bytes.emplace(byte_address, value);
        }
        unsigned finalized = 0;
        const nx::acnh::FieldItem item{address};
        const bool handled = entry == 0 ? nx::acnh::initialize_empty_field_item(base, item, memory,
            [&](nx::acnh::FieldItem final_item) {
                if (nx::acnh::empty_field_item_finalized(final_item, memory)) ++finalized;
            }) : nx::acnh::empty_field_item_finalized(item, memory);
        std::cout << handled << ' ' << finalized << ' ' << memory.read<uint64_t>(address) << '\n';
    }
}
