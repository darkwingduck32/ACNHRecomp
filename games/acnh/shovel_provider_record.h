#pragma once

#include "field_queries.h"

#include <bit>

namespace nx::acnh {

struct ShovelProviderView { uint64_t address; };

template<class Memory>
FieldItem shovel_provider_record(uint64_t base, ShovelProviderView provider, HalfTileRef point,
                                 HalfTileRef root, Memory& memory) {
    const FieldItem empty{base + 0x5486008};
    const auto x = memory.template read<uint32_t>(point.address + 8);
    const auto min_x = memory.template read<uint32_t>(provider.address + 0x28);
    const auto width = memory.template read<uint32_t>(provider.address + 8);
    const auto within = [](uint32_t value, uint32_t start, uint32_t span) {
        const auto coordinate = std::bit_cast<int32_t>(value);
        return coordinate >= std::bit_cast<int32_t>(start)
            && coordinate <= std::bit_cast<int32_t>(start + span - 1);
    };
    if (!within(x, min_x, width)) return empty;

    const auto z = memory.template read<uint32_t>(point.address + 12);
    const auto min_z = memory.template read<uint32_t>(provider.address + 0x2c);
    const auto stride = memory.template read<uint32_t>(provider.address + 0xc);
    const FieldItem outside{memory.template read<uint64_t>(base + 0x4ab37e0)};
    if (!within(z, min_z, stride)) return outside;

    uint32_t count = memory.template read<uint32_t>(provider.address + 0x10);
    const auto index_at = [&](uint32_t px, uint32_t pz) {
        return (px - min_x) * stride + (pz - min_z);
    };
    const auto index = index_at(x, z);
    if (index >= count) return outside;

    uint64_t records = memory.template read<uint64_t>(provider.address + 0x18);
    const auto record_address = [&](uint32_t number) {
        return records + 8ll * std::bit_cast<int32_t>(number);
    };
    const FieldItem first{record_address(index)};
    if (memory.template read<uint16_t>(first.address) != 0xfffd) {
        if (root.address) {
            memory.template write<uint32_t>(root.address + 8, x);
            memory.template write<uint32_t>(root.address + 12, memory.template read<uint32_t>(point.address + 12));
        }
        return first;
    }

    const auto offset = memory.template read<uint32_t>(first.address + 4);
    const auto anchor_x = x - ((offset >> 16) & 0xffu);
    const auto anchor_z = z - (offset >> 24);
    const auto anchor = index_at(anchor_x, anchor_z);
    if (anchor >= count) return empty;
    if (root.address) {
        memory.template write<uint32_t>(root.address + 8, anchor_x);
        memory.template write<uint32_t>(root.address + 12, anchor_z);
        count = memory.template read<uint32_t>(provider.address + 0x10);
        records = memory.template read<uint64_t>(provider.address + 0x18);
    }
    return {record_address(anchor < count ? anchor : 0)};
}

} // namespace nx::acnh
