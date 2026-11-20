#pragma once

#include <cstdint>

namespace nx::acnh {

struct ProviderContextOwner { uint64_t address; };

template<class Memory, class Services>
bool acquire_provider_context(ProviderContextOwner owner, Memory& memory, Services& services) {
    const uint64_t flag = services.disabled_flag(owner);
    if (flag && memory.template read<uint8_t>(flag)) return false;

    const auto address = owner.address;
    if (!memory.template read<uint64_t>(address + 0x308) ||
        !memory.template read<uint64_t>(address + 0x310)) return true;

    const auto mode = memory.template read<uint32_t>(address + 0x358);
    if (memory.template read<uint8_t>(address + 0x35c)) {
        if (memory.template read<uint8_t>(address + 0x35e)) services.enter_guard(address + 0x338);
        else if (!services.try_guard(address + 0x338)) return true;
    }

    if (mode == 1) services.enter_pair(address + 0x308);
    if (mode != 2) return true;

    auto first = memory.template read<uint64_t>(address + 0x308);
    if (!first) return true;
    const auto second = memory.template read<uint64_t>(address + 0x310);
    if (!second) return true;
    if (memory.template read<int32_t>(second) >= 2) {
        const auto counter = services.counter(second);
        auto count = memory.template read<int32_t>(counter + 0xc);
        if (count <= 0) {
            services.prepare_counter(second + 0x58);
            count = 0;
        }
        memory.template write<uint32_t>(counter + 0xc, static_cast<uint32_t>(count) + 1);
        first = memory.template read<uint64_t>(address + 0x308);
    }
    services.enter_first(first);
    return true;
}

template<class Memory, class Services>
void release_provider_context(ProviderContextOwner owner, Memory& memory, Services& services) {
    const auto address = owner.address;
    const auto first = memory.template read<uint64_t>(address + 0x308);
    const auto second = memory.template read<uint64_t>(address + 0x310);
    if (!first || !second) return;

    const auto mode = memory.template read<uint32_t>(address + 0x358);
    if (memory.template read<uint8_t>(address + 0x35c)) services.leave_guard(address + 0x338);
    if (mode != 1 && mode != 2) return;
    if (!memory.template read<uint64_t>(address + 0x308)) return;
    const auto current_second = memory.template read<uint64_t>(address + 0x310);
    if (!current_second) return;
    services.leave_second(current_second, mode == 2);
    services.leave_first(memory.template read<uint64_t>(address + 0x308), mode == 2);
}

}  // namespace nx::acnh
