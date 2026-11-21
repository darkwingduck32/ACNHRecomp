#pragma once

#include "animation_sidecar.h"

#include <bit>

namespace nx::acnh {

constexpr uint32_t animation_resource_packed_limit = 0xfffeffff;
constexpr uint32_t animation_resource_reserved_index = 0xffff;
constexpr uint32_t animation_resource_packed_stride = 0xb8;
constexpr uint32_t animation_resource_bucket_stride = 0x28;

struct AnimationResourceLookup {
    uint64_t value;
    uint64_t packed_rows;
    uint64_t bucket_displacement;
    uint32_t packed;
    uint32_t bucket_count;
    uint32_t bucket_index;

    bool uses_sentinel() const {
        return packed > animation_resource_packed_limit ||
               (packed & 0xffff) == animation_resource_reserved_index;
    }
};

template<class Memory>
AnimationResourceLookup resolve_animation_resource(uint64_t owner, uint16_t resource_index,
                                                    const Memory& memory) {
    const auto resource_owner = memory.template read<uint64_t>(owner + 0xa0);
    const auto table = memory.template read<uint64_t>(resource_owner + 0x50);
    const auto count = memory.template read<uint32_t>(table + 0x38);
    const auto packed_rows = memory.template read<uint64_t>(table + 0x40);
    const auto selected = animation_sidecar_index(count, resource_index);
    const auto packed = memory.template read<uint32_t>(
        packed_rows + uint64_t{selected} * animation_resource_packed_stride);
    AnimationResourceLookup lookup{0, packed_rows, 0, packed, 0, 0};
    if (lookup.uses_sentinel()) return lookup;

    lookup.bucket_count = memory.template read<uint32_t>(table + 0x80);
    lookup.bucket_index = static_cast<uint32_t>(std::bit_cast<int16_t>(static_cast<uint16_t>(packed >> 16)));
    const auto buckets = memory.template read<uint64_t>(table + 0x88);
    const auto bucket = animation_sidecar_index(lookup.bucket_count, lookup.bucket_index);
    lookup.bucket_displacement = static_cast<uint64_t>(
        int64_t{std::bit_cast<int32_t>(bucket)} * animation_resource_bucket_stride);
    const auto entry = memory.template read<uint64_t>(buckets + lookup.bucket_displacement);
    const auto resource = memory.template read<uint64_t>(entry);
    if (resource) lookup.value = memory.template read<uint64_t>(resource + 8) + 2;
    return lookup;
}

struct AnimationResourceLinks {
    uint64_t row, base;
    uint32_t count;
};

template<class Memory>
AnimationResourceLinks update_animation_resource_links(AnimationSidecars sidecars, uint64_t token,
                                                       Memory& memory) {
    auto count = memory.template read<uint32_t>(sidecars.owner + 0x80);
    auto base = memory.template read<uint64_t>(sidecars.owner + 0x88);
    auto row = animation_sidecar_row(base, animation_sidecar_index(count, sidecars.entry));
    memory.template write<uint64_t>(row + 0xd8, token);
    count = memory.template read<uint32_t>(sidecars.owner + 0x80);
    base = memory.template read<uint64_t>(sidecars.owner + 0x88);
    row = animation_sidecar_row(base, animation_sidecar_index(count, sidecars.entry));
    memory.template write<uint64_t>(row + 0xe0, token);
    return {row, base, count};
}

} // namespace nx::acnh
