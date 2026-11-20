#pragma once

#include <bit>
#include <cstdint>

namespace nx::acnh {

struct ProviderContextHandle { uint64_t address; };

template<class Memory>
uint64_t provider_handle_record(ProviderContextHandle handle, uint64_t key, const Memory& memory) {
    const uint32_t entries = memory.template read<uint32_t>(handle.address + 8);
    if (!entries) return 0;
    const uint64_t records = memory.template read<uint64_t>(handle.address + 0x10);
    for (uint32_t index = 0; index < entries; ++index) {
        const uint64_t record = records + 16ll * std::bit_cast<int32_t>(index);
        if (memory.template read<uint64_t>(record) == key) return record;
    }
    return 0;
}

template<class Memory, class Services>
uint64_t acquire_provider_handle_record(uint64_t base, ProviderContextHandle handle,
                                        uint32_t mode, Memory& memory, Services& services) {
    const uint64_t slot = memory.template read<uint64_t>(base + 0x4ab3350);
    const auto thread = [&](bool second) {
        const uint64_t manager = memory.template read<uint64_t>(slot);
        return services.resolve_thread(memory.template read<uint32_t>(manager + 0x68), second);
    };
    const uint64_t stored_thread = thread(false);
    const uint64_t search_thread = thread(true);
    const uint64_t found = provider_handle_record(handle, search_thread, memory);
    if (found) return found;

    const uint64_t guard = handle.address + 0x38;
    services.enter_records(guard);
    const uint64_t record = provider_handle_record(handle, 0, memory);
    if (record) {
        memory.template write<uint64_t>(record, stored_thread);
        memory.template write<uint32_t>(record + 0xc, 0);
        memory.template write<uint8_t>(record + 8, mode & 1);
    }
    services.leave_records(guard);
    return record;
}

template<class Memory, class Services>
void release_provider_handle(uint64_t base, ProviderContextHandle handle,
                             Memory& memory, Services& services) {
    const uint64_t slot = memory.template read<uint64_t>(base + 0x4ab3350);
    const uint64_t manager = memory.template read<uint64_t>(slot);
    const uint64_t thread = services.resolve_thread(memory.template read<uint32_t>(manager + 0x68));

    const uint64_t record = provider_handle_record(handle, thread, memory);
    if (!record) return;

    const auto count = memory.template read<int32_t>(record + 0xc);
    memory.template write<uint32_t>(record + 0xc, static_cast<uint32_t>(count) - 1);
    if (count > 1) return;

    const uint64_t holders = handle.address + 0x58;
    if (!memory.template read<uint8_t>(record + 8)) {
        services.release_shared(holders);
    } else if (services.decrement(holders) == 1 && services.decrement(handle.address + 0xf8) != 1) {
        services.signal_waiter(handle.address + 0xd0);
    }

    memory.template write<uint32_t>(record + 0xc, 0xacacacac);
    services.barrier();
    memory.template write<uint64_t>(record, 0);
}

template<class Services>
void prepare_provider_handle_counters(uint64_t counters, Services& services) {
    const auto tickets = counters + 0x50;
    if (services.adjust(tickets, 1) != 0) services.wait_readers(counters + 0x28, true);
    if (services.adjust(counters, 1) == 0 && services.adjust(counters + 0xa0, 1) != 0)
        services.wait_readers(counters + 0x78, false);
    if (services.adjust(tickets, uint32_t(-1)) != 1) services.wake_readers(counters + 0x28);
}

}  // namespace nx::acnh
