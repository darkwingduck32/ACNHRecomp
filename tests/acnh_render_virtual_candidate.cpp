#include "../games/acnh/render_virtual.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

namespace {
struct Event {
    char op;
    uint64_t address;
    unsigned width;
    uint64_t value;
};

struct Memory : FixtureMemory {
    mutable std::vector<Event> events;

    template<class T>
    T read(uint64_t address) const {
        const auto value = FixtureMemory::read<T>(address);
        events.push_back({'L', address, sizeof(T), value});
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        events.push_back({'S', address, sizeof(T), value});
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t{value} >> (8 * i)) & 255;
    }
};
}

int main() {
    using namespace nx::acnh;
    unsigned mode;
    uint64_t state, slots, stack, object, field, base;
    uint32_t scale, stride, count, first, second;
    size_t bytes;
    while (std::cin >> mode >> state >> slots >> stack >> object >> field >> base >>
           scale >> stride >> count >> first >> second >> bytes) {
        Memory memory;
        for (size_t i = 0; i < bytes; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        if (mode == 0) {
            const auto plan = prepare_render_virtual({state}, {slots}, {stack + 0x18, stack + 0x20}, memory);
            std::cout << plan.prepared << ' ' << plan.object << ' ' << plan.selected_field << ' '
                      << plan.index_field << ' ' << 0xbc << ' ' << 0xdc << ' ' << plan.object << ' '
                      << plan.record_base << ' ' << plan.record_offset << ' ' << plan.record << ' '
                      << plan.scale << ' ' << plan.stride << ' ' << plan.count << ' ' << plan.index_word;
        } else {
            const auto plan = prepare_render_virtual_commands(
                {object, field, base, stack + 0x18, scale, stride, count}, {first, second}, memory);
            std::cout << plan.prepared << ' ' << plan.index_word << ' ' << plan.sink << ' ' << plan.ordinal
                      << ' ' << plan.mask << ' ' << plan.row << ' ' << plan.object_word << ' ' << plan.commands;
        }
        for (const auto& event : memory.events)
            std::cout << ' ' << event.op << ' ' << event.address << ' ' << event.width << ' ' << event.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
