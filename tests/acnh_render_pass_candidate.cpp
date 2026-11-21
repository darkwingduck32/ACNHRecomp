#include "../games/acnh/render_pass.h"
#include "fixture_memory.h"

#include <array>
#include <bit>
#include <iostream>
#include <vector>

namespace {
struct Memory : FixtureMemory {
    mutable std::vector<std::array<uint64_t, 2>> reads;

    template<class T>
    T read(uint64_t address) const {
        reads.push_back({address, sizeof(T)});
        return FixtureMemory::read<T>(address);
    }
};
}

int main() {
    using namespace nx::acnh;
    unsigned mode;
    uint64_t base, input;
    uint32_t pass, index;
    size_t size;
    while (std::cin >> mode >> base >> input >> pass >> index >> size) {
        Memory memory;
        for (size_t i = 0; i < size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        if (mode == 0) {
            std::cout << render_format_byte_width(base, static_cast<uint32_t>(input), memory);
        } else {
            const auto span = [&](uint8_t format) { return render_format_byte_width(base, format, memory); };
            const auto sample = read_render_pass_float({input}, std::bit_cast<int32_t>(pass),
                                                       std::bit_cast<int32_t>(index), memory, span);
            std::cout << unsigned(sample.kind) << ' ' << sample.scalar_bits << ' ' << sample.format
                      << ' ' << sample.format_bytes << ' ' << static_cast<uint32_t>(sample.selected_index)
                      << ' ' << sample.values << ' ' << sample.buffer << ' ' << sample.descriptor;
        }
        for (const auto [address, bytes] : memory.reads) std::cout << ' ' << address << ' ' << bytes;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
