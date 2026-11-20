#pragma once

#include "field_provider.h"

namespace nx::acnh {

struct ProviderAnchorLease { uint64_t address; };

template<class Memory, class Services>
void acquire_provider_anchor(uint64_t base, FieldCellProvider provider, ProviderCell position,
                             ProviderAnchorLease result, Memory& memory, Services& services) {
    const auto table = services.table(ProviderReader{provider.address + 0x28});
    const uint32_t width = services.width(table);
    const uint32_t height = services.height(table);
    memory.template write<uint64_t>(result.address + 8, table.address);
    memory.template write<uint8_t>(result.address + 0x10, 1);

    const uint64_t handle = memory.template read<uint64_t>(table.address + 0x28);
    const uint64_t context = handle ? memory.template read<uint64_t>(handle) : 0;
    const bool release = context && services.begin({context});
    const uint64_t type = memory.template read<uint64_t>(base + 0x4ab4c28);
    memory.template write<uint8_t>(result.address + 0x11, release);
    memory.template write<uint64_t>(result.address, type + 0x10);
    memory.template write<uint32_t>(result.address + 0x14, position.x < width ? position.x : 0);
    memory.template write<uint32_t>(result.address + 0x18, position.z < height ? position.z : 0);
}

template<class Memory, class Release>
void release_provider_anchor(uint64_t base, ProviderAnchorLease lease, Memory& memory, Release release) {
    memory.template write<uint64_t>(lease.address, base + 0x49882d0);
    if (!memory.template read<uint8_t>(lease.address + 0x11)) return;
    const uint64_t table = memory.template read<uint64_t>(lease.address + 8);
    const bool ordinary = memory.template read<uint8_t>(lease.address + 0x10) != 0;
    const uint64_t handle = memory.template read<uint64_t>(table + 0x28);
    if (!handle) return;
    const uint64_t context = memory.template read<uint64_t>(handle + (ordinary ? 8 : 0x18));
    if (context) release(ProviderContext{context});
}

}  // namespace nx::acnh
