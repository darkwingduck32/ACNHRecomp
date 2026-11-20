#pragma once

#include "field_provider_context.h"

namespace nx::acnh {

template<class Memory, class Services>
uint64_t provider_context_flag(uint64_t base, ProviderContextOwner owner,
                               const Memory& memory, Services& services) {
    const uint64_t slot = memory.template read<uint64_t>(base + 0x4ab3350);
    const uint64_t manager = memory.template read<uint64_t>(slot);
    uint64_t thread = 0;
    if (manager) {
        thread = services.resolve_thread(memory.template read<uint32_t>(manager + 0x68));
        const uint64_t current = memory.template read<uint64_t>(slot);
        if (current && memory.template read<uint64_t>(current + 0x60) == thread)
            return owner.address + 0x450;
    }

    const uint64_t receiver = thread + 0x20;
    const auto matches_name = [&](uint64_t expected, unsigned first_call) {
        services.refresh_thread_name(receiver, first_call);
        services.refresh_thread_name(receiver, first_call + 1);
        const uint64_t name = memory.template read<uint64_t>(receiver + 8);
        if (name == expected) return true;

        for (uint64_t i = 0; i <= 0x80000; ++i) {
            const uint8_t actual_byte = memory.template read<uint8_t>(name + i);
            if (actual_byte != memory.template read<uint8_t>(expected + i)) return false;
            if (!actual_byte) return true;
        }
        return false;
    };

    if (matches_name(base + 0x3fa5738, 0)) return owner.address + 0x451;
    if (matches_name(base + 0x3f9b10c, 2)) return owner.address + 0x452;
    return 0;
}

}  // namespace nx::acnh
