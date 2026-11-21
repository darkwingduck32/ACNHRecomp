#pragma once

#include "render_resource_prefix.h"

namespace nx::acnh {

struct RenderDescriptorRef { uint64_t address; };

struct RenderDescriptorJoin {
    uint32_t flags;
    uint32_t optional;
    uint32_t kind;
    uint64_t first_word;
    bool copied;
};

template<class Memory>
RenderDescriptorJoin transfer_render_descriptor(RenderStateRef state, RenderDescriptorRef descriptor,
                                                uint32_t old_flags, bool copied, Memory& memory) {
    const auto source = descriptor.address;
    const auto target = state.address;
    uint64_t first_word = 0;
    if (copied) {
        memory.template write<uint32_t>(target + 0x1e4, memory.template read<uint8_t>(target + 0x35));
        const auto second_word = memory.template read<uint64_t>(source + 0x8c);
        first_word = memory.template read<uint64_t>(source + 0x84);
        memory.template write<uint64_t>(target + 0x1e8, first_word);
        memory.template write<uint64_t>(target + 0x1f0, second_word);
        for (unsigned offset = 0; offset < 0x30; offset += 0x0c) {
            const auto in = source + 0xa4 + offset;
            const auto out = target + 0x208 + offset;
            memory.template write<uint32_t>(out, memory.template read<uint32_t>(in));
            memory.template write<uint16_t>(out + 4, memory.template read<uint16_t>(in + 4));
            memory.template write<uint32_t>(out + 6, memory.template read<uint32_t>(in + 6));
            memory.template write<uint16_t>(out + 10, memory.template read<uint16_t>(in + 10));
        }
    } else {
        memory.template write<uint32_t>(target + 0x1e4, 0);
    }

    // Read the kind after the transfer, as the state and descriptor can share storage.
    const uint32_t kind = memory.template read<uint8_t>(source + 0x71) & 7;
    uint32_t optional = old_flags;
    if (kind == 5 && (old_flags & 0x10)) {
        memory.template write<uint16_t>(target + 0x1e0, memory.template read<uint16_t>(source + 0x7c));
        optional = memory.template read<uint8_t>(source + 0xe0);
        memory.template write<uint8_t>(target + 0x244, optional);
    }
    const uint32_t flags = (old_flags & ~0x200u) | 0x80;
    memory.template write<uint64_t>(target + 0x1d0, source + 0x68);
    memory.template write<uint16_t>(target + 0x36, flags);
    return {flags, optional, kind, first_word, copied};
}

} // namespace nx::acnh
