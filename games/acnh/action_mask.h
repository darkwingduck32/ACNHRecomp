#pragma once

#include <cstdint>

namespace nx::acnh {

template<class Memory>
void initialize_action_mask(uint64_t object, Memory& memory) {
    if (memory.template read<uint8_t>(object + 0x28)) return;

    const auto source = memory.template read<uint64_t>(object + 0x10);
    memory.template write<uint64_t>(object + 0x20, source);
    const auto low = memory.template read<uint32_t>(object + 0x20);
    memory.template write<uint32_t>(object + 0x20, low & ~(uint32_t{1} << 30));
    memory.template write<uint8_t>(object + 0x28, 1);
}

}  // namespace nx::acnh
