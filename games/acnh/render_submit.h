#pragma once

#include "render_resource_prefix.h"

namespace nx::acnh {

struct RenderSubmitBindingRef { uint64_t address; };
struct RenderCommandSinkRef { uint64_t address; };

struct RenderIndirectSubmit {
    bool selected{};
    uint64_t records{};
    uint64_t record{};
    uint64_t receiver{};
};

template<class Memory>
RenderIndirectSubmit prepare_indirect_render_submit(RenderSubmitBindingRef binding, RenderStateRef state,
                                                   RenderCommandSinkRef sink, int32_t pass, uint32_t mask,
                                                   const Memory& memory) {
    if (mask != 0) return {};
    const auto records = memory.template read<uint64_t>(binding.address + 0xe10);
    if (!records) return {};
    const uint64_t record = records + static_cast<int64_t>(pass) * 0x260;
    if (!(memory.template read<uint8_t>(record + 0x258) & 1) ||
        !(memory.template read<uint16_t>(state.address + 0x36) & 0x20)) return {};
    return {true, records, record, memory.template read<uint64_t>(sink.address + 0xb8)};
}

struct RenderSelectorSubmit {
    bool selected{};
    uint64_t record{};
    uint64_t receiver{};
    uint32_t lane{};
};

template<class Memory>
RenderSelectorSubmit prepare_render_selector_submit(RenderStateRef state, RenderSlotTableRef slots,
                                                      RenderSubmitBindingRef binding, const Memory& memory) {
    const auto resource = memory.template read<uint64_t>(slots.address + 8);
    if (!resource) return {};
    const uint32_t lane = memory.template read<uint8_t>(resource + 0x2e5) & 3;
    if (lane > 1) return {};
    if (!(memory.template read<uint16_t>(state.address + 0x36) & 0x20)) return {};
    const uint64_t record = binding.address + 0x20 + lane * 0x260;
    if ((memory.template read<uint8_t>(record + 0x258) & 3) != 3) return {};
    const RenderCommandSinkRef sink{memory.template read<uint64_t>(state.address + 0x18)};
    if (!sink.address) return {};
    return {true, record, memory.template read<uint64_t>(sink.address + 0xb8), lane};
}

} // namespace nx::acnh
