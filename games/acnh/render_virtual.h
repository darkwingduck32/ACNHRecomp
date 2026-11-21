#pragma once

#include "render_resource_prefix.h"

namespace nx::acnh {

struct RenderVirtualScratch {
    uint64_t command_sink;
    uint64_t selected_record;
};

struct RenderVirtualPreparation {
    bool prepared{};
    uint64_t object{};
    uint64_t record{};
    uint64_t record_base{};
    uint64_t index_field{};
    uint64_t selected_field{};
    uint32_t index_word{};
    uint32_t record_offset{};
    uint32_t scale{};
    uint32_t stride{};
    uint32_t count{};
};

template<class Memory>
RenderVirtualPreparation prepare_render_virtual(RenderStateRef state, RenderSlotTableRef slots,
                                                RenderVirtualScratch scratch, Memory& memory) {
    const auto record = memory.template read<uint64_t>(state.address + 0x1c8);
    const auto resource = memory.template read<uint64_t>(slots.address + 8);
    if (!record || !resource) return {};
    const auto object = memory.template read<uint64_t>(resource + 0x58);
    if (!object) return {};
    const auto sink = memory.template read<uint64_t>(state.address + 0x18);
    memory.template write<uint64_t>(scratch.command_sink, sink);
    const auto base = memory.template read<uint64_t>(object + 0x70);
    const auto index_field = record + 0xbc;
    const auto index_word = memory.template read<uint32_t>(index_field);
    const auto scale = memory.template read<uint8_t>(object + 0x39);
    const auto stride = memory.template read<uint16_t>(object + 0x3a);
    const auto count = memory.template read<uint32_t>(object + 0x30);
    const uint32_t offset = index_word == 0xffffffffu ? 0xdc : 0xbc;
    const auto selected = record + offset;
    memory.template write<uint64_t>(scratch.selected_record, selected);
    return {true, object, record, base, index_field, selected, index_word, offset, scale, stride, count};
}

struct RenderVirtualReturns {
    uint32_t first;
    uint32_t second;
};

struct RenderVirtualContinuation {
    uint64_t object;
    uint64_t index_field;
    uint64_t record_base;
    uint64_t command_sink_slot;
    uint32_t scale;
    uint32_t stride;
    uint32_t count;
};

struct RenderVirtualCommands {
    bool prepared{};
    uint32_t index_word{};
    uint32_t mask{};
    uint32_t ordinal{};
    uint64_t row{};
    uint64_t sink{};
    uint64_t commands{};
    uint32_t object_word{};
};

template<class Memory>
RenderVirtualCommands prepare_render_virtual_commands(RenderVirtualContinuation input,
                                                      RenderVirtualReturns returns, const Memory& memory) {
    const auto word = memory.template read<uint32_t>(input.index_field);
    if ((word & 0xff) == 0xff) return {};
    const uint32_t mask = 0u - returns.second;
    const uint32_t ordinal = (input.count + returns.first - 1) & mask;
    const uint32_t stride = input.stride * input.scale;
    const auto row = input.record_base + uint64_t{stride} * ordinal;
    const auto sink = memory.template read<uint64_t>(input.command_sink_slot);
    const auto object_word = memory.template read<uint32_t>(input.object + 0x30);
    if (!sink) return {};
    const auto commands = memory.template read<uint64_t>(sink + 0xb8);
    return {true, word, mask, ordinal, row, sink, commands, object_word};
}

} // namespace nx::acnh
