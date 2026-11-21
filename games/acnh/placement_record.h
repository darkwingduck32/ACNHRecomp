#pragma once

#include "field_flag.h"

#include <array>
#include <cstddef>

namespace nx::acnh {

struct PlacementRecord { uint64_t address; };
struct PlacementParameters { uint64_t address; };

struct PlacementInitialization {
    uint32_t scalar_bits;
    uint8_t last_byte;
    bool has_extra_flag;
};

template<class Memory>
PlacementInitialization initialize_placement_record(
        PlacementRecord record, PlacementParameters parameters, uint64_t type_table, Memory& memory) {
    const auto out = record.address;
    const auto in = parameters.address;
    memory.template write<uint16_t>(out + 8, 0);
    memory.template write<uint64_t>(out, type_table);
    const auto first_byte = static_cast<uint8_t>(memory.template read<uint32_t>(in + 4));
    memory.template write<uint64_t>(out + 0xc, 0);
    memory.template write<uint8_t>(out + 0xa, first_byte);
    memory.template write<uint16_t>(out + 0x14, memory.template read<uint16_t>(in));
    memory.template write<uint8_t>(out + 0x16, memory.template read<uint8_t>(in + 0x30));
    const auto selector = memory.template read<uint8_t>(in + 0x31);
    memory.template write<uint8_t>(out + 0x17, selector < 8 ? selector : uint8_t{0xff});
    memory.template write<uint64_t>(out + 0x18, memory.template read<uint64_t>(in + 0x38));
    memory.template write<uint32_t>(out + 0x20, 0xffff0000);
    memory.template write<uint16_t>(out + 0x24, memory.template read<uint16_t>(in + 0x32));
    memory.template write<uint8_t>(out + 0x26, memory.template read<uint8_t>(in + 0x4f));

    auto flags = memory.template read<uint8_t>(in + 0x40);
    memory.template write<uint8_t>(out + 8, flags);
    constexpr std::array<unsigned, 6> flag_offsets{0x43, 0x44, 0x45, 0x4c, 0x4d, 0x4e};
    constexpr std::array<unsigned, 6> flag_shifts{1, 2, 3, 4, 6, 5};
    for (std::size_t i = 0; i < flag_offsets.size(); ++i) {
        flags |= static_cast<uint8_t>(memory.template read<uint8_t>(in + flag_offsets[i]) << flag_shifts[i]);
        memory.template write<uint8_t>(out + 8, flags);
    }
    const bool extra = memory.template read<uint8_t>(in + 0x42) != 0;
    flags |= static_cast<uint8_t>(extra << 7);
    memory.template write<uint8_t>(out + 8, flags);

    constexpr std::array<unsigned, 4> halfword_sources{0x18, 0x1c, 0x28, 0x2c};
    for (std::size_t i = 0; i < halfword_sources.size(); ++i) {
        const auto word = memory.template read<uint32_t>(in + halfword_sources[i]);
        memory.template write<uint16_t>(out + 0xc + i * 2, static_cast<uint16_t>(word));
    }
    const auto scalar = memory.template read<uint32_t>(in + 0x48);
    const auto integral = truncate_coordinate(std::bit_cast<float>(scalar));
    memory.template write<uint16_t>(out + 0x20, static_cast<uint16_t>(integral));
    memory.template write<uint8_t>(out + 0x22, memory.template read<uint8_t>(in + 0x41));
    const auto last = memory.template read<uint8_t>(in + 0x46);
    memory.template write<uint8_t>(out + 0x23, last);
    return {scalar, last, extra};
}

} // namespace nx::acnh
