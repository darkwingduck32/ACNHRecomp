#include "../games/acnh/field_provider_context_handle.h"

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

    template<class T>
    void write(uint64_t address, T value) {
        writes.emplace_back(address, sizeof(T), value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = (value >> (8 * i)) & 255;
    }
};

struct Services {
    Memory& memory;
    uint64_t thread, handle, replacement_table, manager_slot, next_manager, first_thread;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> calls;

    uint64_t resolve_thread(uint32_t id) {
        calls.emplace_back(0x37568e4, 0x3f033a0, id);
        return thread;
    }

    uint64_t resolve_thread(uint32_t id, bool second) {
        calls.emplace_back(second ? 0x3756808 : 0x37567f8, 0x3f033a0, id);
        if (!second && next_manager) {
            memory.write<uint64_t>(manager_slot, next_manager);
            return first_thread;
        }
        return thread;
    }

    void enter_records(uint64_t guard) {
        calls.emplace_back(0x3756848, 0x3f03880, guard);
        if (replacement_table) memory.write<uint64_t>(handle + 0x10, replacement_table);
    }

    void leave_records(uint64_t guard) { calls.emplace_back(0x37568a8, 0x3f038c0, guard); }

    uint32_t decrement(uint64_t address) {
        const auto prior = memory.read<uint32_t>(address);
        memory.write<uint32_t>(address, prior - 1);
        return prior;
    }

    void release_shared(uint64_t address) { calls.emplace_back(0x3756994, 0x37e3040, address); }
    void signal_waiter(uint64_t address) { calls.emplace_back(0x375698c, 0x3f05830, address); }
    uint32_t adjust(uint64_t address, uint32_t delta) {
        const auto prior = memory.read<uint32_t>(address);
        memory.write<uint32_t>(address, prior + delta);
        return prior;
    }

    void wait_readers(uint64_t address, bool initial) {
        calls.emplace_back(initial ? 0x37e2eac : 0x37e2ee8, 0x3f05820, address);
    }

    void wake_readers(uint64_t address) { calls.emplace_back(0x37e2f20, 0x3f05830, address); }
    void barrier() {}
};

}

int main() {
    uint64_t base, handle, thread, replacement_table, manager_slot, next_manager, first_thread;
    uint32_t mode;
    unsigned operation;
    size_t count;
    while (std::cin >> base >> handle >> thread >> operation >> mode >> replacement_table >> manager_slot >> next_manager >> first_thread >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, static_cast<uint8_t>(byte));
        }

        Services services{memory, thread, handle, replacement_table, manager_slot, next_manager, first_thread, {}};
        if (operation == 2) nx::acnh::prepare_provider_handle_counters(handle, services);
        else if (operation == 1)
            std::cout << nx::acnh::acquire_provider_handle_record(base, {handle}, mode, memory, services) << ' ';
        else
            nx::acnh::release_provider_handle(base, {handle}, memory, services);
        std::cout << services.calls.size();
        for (const auto& [site, target, receiver] : services.calls)
            std::cout << ' ' << site << ' ' << target << ' ' << receiver;
        for (const auto& [address, width, value] : memory.writes)
            std::cout << ' ' << address << ' ' << width << ' ' << value;
        std::cout << '\n';
    }
}
