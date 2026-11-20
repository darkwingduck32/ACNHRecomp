#include "../games/acnh/field_column.h"

#include <cstdlib>
#include <iostream>
#include <tuple>
#include <type_traits>
#include <vector>
#include <unordered_map>

struct FixtureMemory {
    std::unordered_map<uint64_t, uint32_t> bytes;
    uint64_t cache;
    uint64_t written = 0;
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<uint64_t>(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(value);
    }

    template<class T>
    void write(uint64_t address, T value) {
        writes.emplace_back(address - cache, sizeof(T), static_cast<std::make_unsigned_t<T>>(value));
        for (size_t i = 0; i < sizeof(T); ++i) {
            const auto offset = address + i - cache;
            if (offset >= 60) std::abort();
            bytes.at(address + i) = static_cast<uint32_t>((static_cast<uint64_t>(value) >> (8 * i)) & 0xff);
            written |= uint64_t{1} << offset;
        }
    }
};

int main() {
    uint64_t row, cache;
    uint32_t column, hash, count;
    while (std::cin >> row >> column >> hash >> cache >> count) {
        FixtureMemory memory{{}, cache};
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t address;
            uint32_t value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        const auto result = nx::acnh::field_column(row, column, hash, cache, memory);
        std::cout << result << ' ' << memory.written;
        if (memory.bytes.contains(cache)) {
            for (uint64_t i = 0; i < 60; ++i) std::cout << ' ' << memory.bytes.at(cache + i);
        }
        for (const auto& [offset, width, value] : memory.writes)
            std::cout << ' ' << offset << ' ' << width << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
