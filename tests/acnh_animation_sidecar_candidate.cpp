#include "../games/acnh/animation_sidecar.h"
#include "../runtime/include/nx/cpu.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

namespace {
struct Access {
    char kind;
    uint64_t address, width, value;
};

struct Memory : FixtureMemory {
    mutable std::vector<Access> accesses;

    template<class T>
    T read(uint64_t address) const {
        const auto value = FixtureMemory::read<T>(address);
        accesses.push_back({'L', address, sizeof(T), value});
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        accesses.push_back({'S', address, sizeof(T), value});
        for (size_t byte = 0; byte < sizeof(T); ++byte)
            bytes[address + byte] = static_cast<uint8_t>(static_cast<uint64_t>(value) >> (8 * byte));
    }
};
}

int main() {
    using namespace nx::acnh;
    uint64_t owner, cached_base, cached_row;
    uint32_t part, index, cached_index, scalar;
    size_t bytes;
    while (std::cin >> part >> owner >> index >> cached_base >> cached_row >> cached_index >> scalar >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        nx::Flags flags{};
        if (part == 0) {
            const auto update = update_animation_scalar({owner, index}, scalar, memory);
            nx::subs32(flags, update.count, index);
            std::cout << update.count << ' ' << update.counter_address << ' ' << update.tested_counter
                      << ' ' << animation_counter_limit << ' ' << update.selected << ' ' << update.row
                      << ' ' << update.base << ' ' << nx::get_nzcv(flags);
        } else if (part == 1) {
            const auto update = update_animation_name_links(
                {owner, index}, {cached_base, cached_row, cached_index}, memory);
            nx::subs32(flags, update.compared_count, update.compared_index);
            std::cout << update.base << ' ' << update.count << ' ' << unsigned(update.first_byte)
                      << ' ' << nx::get_nzcv(flags);
        } else {
            return 1;
        }
        for (const auto& access : memory.accesses)
            std::cout << ' ' << access.kind << ' ' << access.address << ' ' << access.width << ' ' << access.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
