#include "../games/acnh/field_flag.h"

#include <bit>
#include <iostream>
#include <unordered_map>

struct FixtureMemory {
    std::unordered_map<uint64_t, uint32_t> bytes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= uint64_t{bytes.at(address + i)} << (8 * i);
        return static_cast<T>(value);
    }
};

int main() {
    uint64_t base, position;
    uint32_t radius, flags, count;
    while (std::cin >> base >> position >> radius >> flags >> count) {
        FixtureMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t address;
            uint32_t value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        std::cout << nx::acnh::field_flag(base, position, std::bit_cast<float>(radius), flags, memory) << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
