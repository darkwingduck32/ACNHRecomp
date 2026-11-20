#pragma once

#include <cstdint>

namespace nx::acnh {

struct ProviderContextPair { uint64_t address; };

template<class Memory, class Services>
bool increment_provider_counter(uint64_t handle, bool first, Memory& memory, Services& services) {
    if (memory.template read<int32_t>(handle) < 2) return false;

    const auto counter = services.counter(handle, first);
    auto count = memory.template read<int32_t>(counter + 0xc);
    if (count <= 0) {
        services.prepare_counter(handle + 0x58, first);
        count = 0;
    }
    memory.template write<uint32_t>(counter + 0xc, static_cast<uint32_t>(count) + 1);
    return true;
}

template<class Memory, class Services>
void acquire_provider_pair(ProviderContextPair pair, Memory& memory, Services& services) {
    auto first = memory.template read<uint64_t>(pair.address);
    const auto second = memory.template read<uint64_t>(pair.address + 8);
    if (!first || !second) return;

    if (increment_provider_counter(second, false, memory, services))
        first = memory.template read<uint64_t>(pair.address);
    increment_provider_counter(first, true, memory, services);
}

}  // namespace nx::acnh
