#include "../games/acnh/placement_record.h"
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
    uint64_t record, parameters, type_table;
    size_t bytes;
    while (std::cin >> record >> parameters >> type_table >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        const auto result = nx::acnh::initialize_placement_record({record}, {parameters}, type_table, memory);
        nx::Flags flags{};
        nx::subs32(flags, result.has_extra_flag, 0);
        std::cout << record << ' ' << unsigned(result.last_byte) << ' ' << result.has_extra_flag
                  << ' ' << result.scalar_bits << ' ' << nx::get_nzcv(flags);
        for (const auto& access : memory.accesses)
            std::cout << ' ' << access.kind << ' ' << access.address << ' ' << access.width << ' ' << access.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
