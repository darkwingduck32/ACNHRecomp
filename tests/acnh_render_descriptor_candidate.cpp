#include "../games/acnh/render_descriptor.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    mutable std::vector<std::array<uint64_t, 4>> effects;

    template<class T>
    T read(uint64_t address) const {
        const auto value = FixtureMemory::read<T>(address);
        effects.push_back({0, address, sizeof(T), value});
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        effects.push_back({1, address, sizeof(T), value});
        for (size_t byte = 0; byte < sizeof(T); ++byte)
            bytes[address + byte] = (uint64_t(value) >> (8 * byte)) & 255;
    }
};
}

int main() {
    using namespace nx::acnh;
    uint64_t state, descriptor, x13, x14;
    unsigned flags;
    size_t size;
    while (std::cin >> state >> descriptor >> flags >> x13 >> x14 >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const bool copied = memory.read<uint32_t>(descriptor + 0x80) != 0;
        const auto result = transfer_render_descriptor({state}, {descriptor}, flags, copied, memory);
        const uint32_t nzcv = result.kind < 5 ? 0x80000000 : result.kind == 5 ? 0x60000000 : 0x20000000;
        std::cout << result.flags << ' ' << result.optional << ' ' << result.kind << ' '
                  << (result.copied ? state + 0x1e0 : x13) << ' '
                  << (result.copied ? result.first_word : x14) << ' ' << nzcv;
        for (auto [operation, address, width, value] : memory.effects)
            std::cout << ' ' << operation << ' ' << address << ' ' << width << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
