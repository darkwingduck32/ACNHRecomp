#include "../games/acnh/animation_resource.h"
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
    uint64_t owner, token;
    uint32_t part, index;
    size_t bytes;
    while (std::cin >> part >> owner >> index >> token >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        nx::Flags flags{};
        if (part == 0) {
            const auto lookup = resolve_animation_resource(owner, static_cast<uint16_t>(index), memory);
            uint64_t x9 = lookup.bucket_displacement;
            uint32_t x10 = lookup.bucket_count;
            if (lookup.uses_sentinel()) {
                x9 = lookup.packed;
                x10 = lookup.packed & 0xffff;
                if (lookup.packed > animation_resource_packed_limit) nx::set_nzcv(flags, 4);
                else nx::subs32(flags, x10, animation_resource_reserved_index);
            } else {
                nx::subs32(flags, lookup.bucket_count, lookup.bucket_index);
            }
            std::cout << lookup.value << ' ' << x9 << ' ' << x10 << ' ' << lookup.packed_rows
                      << ' ' << nx::get_nzcv(flags);
        } else if (part == 1) {
            const auto links = update_animation_resource_links({owner, index}, token, memory);
            nx::subs64(flags, index, links.count);
            std::cout << token << ' ' << links.row << ' ' << links.base << ' ' << nx::get_nzcv(flags);
        } else {
            return 1;
        }
        for (const auto& access : memory.accesses)
            std::cout << ' ' << access.kind << ' ' << access.address << ' ' << access.width << ' ' << access.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
