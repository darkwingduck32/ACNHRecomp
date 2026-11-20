#pragma once

#include "field_provider.h"

namespace nx::acnh {

struct ProviderBinding { uint64_t address; };

template<class Memory, class Resolve>
ProviderTable resolve_provider_table(uint64_t base, const Memory& memory, Resolve resolve) {
    const uint64_t manager = memory.template read<uint64_t>(base + 0x5474040);
    uint64_t found = 0;
    if (manager) {
        const auto flags = memory.template read<uint8_t>(manager + 0x453);
        const uint64_t binding = memory.template read<uint64_t>(manager + 0x10);
        if (flags & 1) {
            const uint64_t selected = binding ? memory.template read<uint64_t>(binding + 0x130) : 0;
            if (selected) found = resolve(ProviderBinding{selected}, true);
        } else {
            found = resolve(ProviderBinding{memory.template read<uint64_t>(binding)}, false);
            if (found) found += 0x70;
        }
    }
    return {found + 0xe7690};
}

}  // namespace nx::acnh
