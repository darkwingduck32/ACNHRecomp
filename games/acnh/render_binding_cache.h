#pragma once

#include "render_resource_prefix.h"

namespace nx::acnh {

template<class Memory>
bool render_binding_slots_empty(RenderStateRef state, RenderSlotTableRef context, const Memory& memory) {
    constexpr uint32_t absent = 0xffffffffu;
    return memory.template read<uint8_t>(context.address + 0x261) != 0
        && memory.template read<uint32_t>(state.address + 0x188) == absent
        && memory.template read<uint32_t>(state.address + 0x18c) == absent;
}

} // namespace nx::acnh
