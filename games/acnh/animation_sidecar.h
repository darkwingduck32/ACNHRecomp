#pragma once

#include <cstdint>

namespace nx::acnh {

struct AnimationSidecars {
    uint64_t owner;
    uint32_t entry;
};

constexpr uint32_t animation_sidecar_stride = 0xe8;
constexpr uint32_t animation_counter_limit = 0x2932e1;

inline uint32_t animation_sidecar_index(uint32_t count, uint32_t requested) {
    return count > requested ? requested : 0;
}

inline uint64_t animation_sidecar_row(uint64_t base, uint32_t index) {
    return base + uint64_t{index} * animation_sidecar_stride;
}

struct AnimationScalarUpdate {
    uint64_t base;
    uint64_t row;
    uint64_t counter_address;
    uint32_t count;
    uint32_t selected;
    uint32_t tested_counter;
};

template<class Memory>
AnimationScalarUpdate update_animation_scalar(AnimationSidecars sidecars, uint32_t scalar_bits,
                                              Memory& memory) {
    const auto owner = sidecars.owner;
    const auto entry = sidecars.entry;
    auto count = memory.template read<uint32_t>(owner + 0x80);
    auto base = memory.template read<uint64_t>(owner + 0x88);
    auto row = animation_sidecar_row(base, animation_sidecar_index(count, entry));
    memory.template write<uint32_t>(row + 0xc0, scalar_bits);

    count = memory.template read<uint32_t>(owner + 0x80);
    base = memory.template read<uint64_t>(owner + 0x88);
    const auto epoch = memory.template read<uint32_t>(owner + 0xb0);
    row = animation_sidecar_row(base, animation_sidecar_index(count, entry));
    if (memory.template read<uint32_t>(row + 0xc4) != epoch) {
        const auto count_address = row + 0xc8;
        const auto previous = memory.template read<uint32_t>(count_address);
        memory.template write<uint32_t>(count_address, previous + 1);
        count = memory.template read<uint32_t>(owner + 0x80);
        base = memory.template read<uint64_t>(owner + 0x88);
    }

    const auto counter_address = animation_sidecar_row(base, animation_sidecar_index(count, entry)) + 0xc8;
    const auto counter = memory.template read<uint32_t>(counter_address);
    if (counter >= animation_counter_limit) {
        memory.template write<uint32_t>(counter_address, 0x278d00);
        count = memory.template read<uint32_t>(owner + 0x80);
        base = memory.template read<uint64_t>(owner + 0x88);
    }
    const auto selected = animation_sidecar_index(count, entry);
    return {base, animation_sidecar_row(base, selected), counter_address, count, selected, counter};
}

struct AnimationNameSelection {
    uint64_t base;
    uint64_t row;
    uint32_t index;
};

struct AnimationNameUpdate {
    uint64_t base;
    uint32_t count;
    uint32_t compared_count;
    uint32_t compared_index;
    uint8_t first_byte;
};

template<class Memory>
AnimationNameUpdate update_animation_name_links(AnimationSidecars sidecars, AnimationNameSelection previous,
                                                Memory& memory) {
    const auto owner = sidecars.owner;
    const auto entry = sidecars.entry;
    const auto cached = memory.template read<uint32_t>(animation_sidecar_row(previous.base, previous.index) + 0xb8);
    memory.template write<uint32_t>(previous.row + 0x58, cached);

    auto count = memory.template read<uint32_t>(owner + 0x80);
    auto base = memory.template read<uint64_t>(owner + 0x88);
    const auto clear_row = animation_sidecar_row(base, animation_sidecar_index(count, entry));
    memory.template write<uint64_t>(clear_row + 0xd0, 0);
    const auto collection = memory.template read<uint64_t>(owner + 0x78);
    const auto collection_count = collection ? memory.template read<uint32_t>(collection + 0x18) : 0;
    count = memory.template read<uint32_t>(owner + 0x80);
    const auto provisional_index = animation_sidecar_index(collection_count, entry);
    base = memory.template read<uint64_t>(owner + 0x88);
    const auto name_row = animation_sidecar_row(base, animation_sidecar_index(count, provisional_index));
    const auto name = memory.template read<uint64_t>(name_row + 0x68);
    const auto first_byte = memory.template read<uint8_t>(name);
    const auto compared_count = count;
    if (first_byte) {
        const auto copy_row = animation_sidecar_row(base, animation_sidecar_index(count, entry));
        const auto link = memory.template read<uint64_t>(copy_row + 0xd8);
        memory.template write<uint64_t>(copy_row + 0xd0, link);
        count = memory.template read<uint32_t>(owner + 0x80);
        base = memory.template read<uint64_t>(owner + 0x88);
    }
    return {base, count, compared_count, first_byte ? entry : provisional_index, first_byte};
}

} // namespace nx::acnh
