#include "../games/acnh/terrain_draw_pass.h"
#include "fixture_memory.h"

#include <iostream>
#include <vector>

namespace {
struct Event {
    char op;
    uint64_t address, width, value;
};

struct Memory : FixtureMemory {
    mutable std::vector<Event> events;

    template<class T>
    T read(uint64_t address) const {
        auto value = FixtureMemory::read<T>(address);
        events.push_back({'L',address,sizeof(T),uint64_t{value}});
        return value;
    }

    template<class T>
    void write(uint64_t address,T value) {
        events.push_back({'S',address,sizeof(T),uint64_t{value}});
        for (size_t i=0; i<sizeof(T); ++i)
            bytes[address+i] = (uint64_t{value}>>(8*i)) & 255;
    }
};
}

int main() {
    using namespace nx::acnh;
    uint64_t manager,descriptor,head,context,cell,stack;
    size_t bytes;
    while (std::cin >> manager >> descriptor >> head >> context >> cell >> stack >> bytes) {
        Memory memory;
        for (size_t i=0; i<bytes; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address,value);
        }
        const auto output = prepare_terrain_draw_entry({{manager},{descriptor},head,context,cell},
            {stack+0x18,stack+0x58,stack+0x60,stack},memory);
        std::cout << output.first << ' ' << output.rows << ' ' << output.index << ' ' << output.table
                  << ' ' << output.table_end << ' ' << unsigned(output.pass);
        for (const auto& event : memory.events)
            std::cout << ' ' << event.op << ' ' << event.address << ' ' << event.width << ' ' << event.value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
