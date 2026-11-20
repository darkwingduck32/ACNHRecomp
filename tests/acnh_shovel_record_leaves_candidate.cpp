#include "../games/acnh/shovel_provider_record.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = value;
        writes.push_back({address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i) bytes.at(address + i) = (bits >> (8*i)) & 0xff;
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t base, entry, arg0, arg1, arg2;
    size_t size;
    while (std::cin >> base >> entry >> arg0 >> arg1 >> arg2 >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        using namespace nx::acnh;
        uint64_t result = 0;
        if (argc == 1 && entry == 0x194f798) {
            const FieldQueries queries{base, memory};
            const FieldLayer layer{memory.read<uint32_t>(arg0 + 8)};
            const auto bounds = queries.bounds_for(layer, {uint32_t(arg1)});
            result = bounds && queries.contains(bounds, {arg2});
        } else if (argc == 1) {
            result = shovel_provider_record(base, {arg0}, {arg1}, {arg2}, memory).address;
        }
        std::cout << result;
        for (const auto& write : memory.writes)
            for (const auto value : write) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
