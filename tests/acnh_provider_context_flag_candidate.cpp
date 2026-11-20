#include "../games/acnh/field_provider_context_flag.h"

#include <iostream>
#include <map>
#include <tuple>
#include <vector>

namespace {

struct Memory {
    std::map<uint64_t, uint8_t> bytes;
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(value);
    }

    void write_pointer(uint64_t address, uint64_t value) {
        writes.emplace_back(address, 8, value);
        for (size_t i = 0; i < 8; ++i)
            bytes.at(address + i) = (value >> (8 * i)) & 255;
    }
};

struct Services {
    Memory& memory;
    uint64_t base, thread, slot, next_vtable;
    bool clear_slot, change_vtable;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> calls;

    uint64_t resolve_thread(uint32_t id) {
        calls.emplace_back(0x2601138, 0x3f033a0, id);
        if (clear_slot) memory.write_pointer(slot, 0);
        return thread;
    }

    void refresh_thread_name(uint64_t receiver, unsigned index) {
        constexpr uint64_t sites[]{0x2601170, 0x2601180, 0x26011f4, 0x2601204};
        const auto vtable = memory.read<uint64_t>(receiver);
        const auto target = memory.read<uint64_t>(vtable + 0x18);
        calls.emplace_back(sites[index], target - base, receiver);
        if (change_vtable && index == 0) memory.write_pointer(receiver, next_vtable);
    }
};

}

int main() {
    uint64_t base, owner, thread, slot, next_vtable;
    bool clear_slot, change_vtable;
    size_t count;
    while (std::cin >> base >> owner >> thread >> slot >> next_vtable >> clear_slot >> change_vtable >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, static_cast<uint8_t>(byte));
        }

        Services services{memory, base, thread, slot, next_vtable, clear_slot, change_vtable, {}};
        std::cout << nx::acnh::provider_context_flag(base, {owner}, memory, services);
        std::cout << ' ' << services.calls.size();
        for (const auto& [site, target, receiver] : services.calls)
            std::cout << ' ' << site << ' ' << target << ' ' << receiver;
        for (const auto& [address, width, value] : memory.writes)
            std::cout << ' ' << address << ' ' << width << ' ' << value;
        std::cout << '\n';
    }
}
