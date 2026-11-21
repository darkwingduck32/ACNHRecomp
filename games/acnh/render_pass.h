#pragma once

#include <bit>
#include <cstdint>

namespace nx::acnh {

struct RenderPassCollectionRef { uint64_t address; };

enum class RenderPassFloatKind { unsupported, sentinel, sampled };

struct RenderPassFloatSample {
    RenderPassFloatKind kind{};
    uint32_t scalar_bits{};
    uint32_t format{};
    uint64_t format_bytes{};
    int32_t selected_index{};
    uint64_t values{};
    uint64_t buffer{};
    uint64_t descriptor{};
};

template<class Memory>
uint64_t render_format_byte_width(uint64_t main_base, uint32_t format, const Memory& memory) {
    if (std::bit_cast<int32_t>(format) <= 15) return 4 + 4 * (format & 3);
    if (format <= 27) return 16 * (2 + ((format - 16) >> 2));
    if (format <= 30) return memory.template read<uint64_t>(main_base + 0x4035398 + 8 * (format - 28));
    return 0;
}

template<class Memory, class FormatByteWidth>
RenderPassFloatSample read_render_pass_float(RenderPassCollectionRef collection, int32_t pass,
                                             int32_t requested_index, const Memory& memory,
                                             FormatByteWidth byte_width) {
    const int64_t pass_offset = static_cast<int64_t>(pass) * 0x80;
    const auto rows = memory.template read<uint64_t>(collection.address + 0x120);
    const auto descriptor = memory.template read<uint64_t>(rows + pass_offset);
    const auto selection = memory.template read<uint64_t>(descriptor + 0x80);
    const auto descriptor_index = memory.template read<int16_t>(selection + 0xc);
    if (descriptor_index == -1)
        return {RenderPassFloatKind::sentinel, 0x3f800000, 0, 0, 0, 0, 0, descriptor};

    const int64_t descriptor_offset = static_cast<int64_t>(descriptor_index) * 0x20;
    const auto table = memory.template read<uint64_t>(descriptor + 0x50);
    const auto format = memory.template read<uint8_t>(table + descriptor_offset + 0x10);
    const uint64_t bytes = byte_width(format);
    if (!bytes) return {};
    const auto float_count = std::bit_cast<int32_t>(static_cast<uint32_t>(bytes >> 2));
    const int32_t index = float_count > requested_index ? requested_index : 0;

    // Reload the row table and descriptor after the format query, as the original does.
    const auto row = memory.template read<uint64_t>(collection.address + 0x120) + pass_offset;
    const auto buffer = memory.template read<uint64_t>(row + 0x48);
    const auto current_descriptor = memory.template read<uint64_t>(row);
    const auto current_table = memory.template read<uint64_t>(current_descriptor + 0x50);
    const auto byte_offset = memory.template read<uint16_t>(current_table + descriptor_offset + 0x12);
    const auto values = buffer + byte_offset;
    const auto scalar = memory.template read<uint32_t>(values + static_cast<int64_t>(index) * 4);
    return {RenderPassFloatKind::sampled, scalar, format, bytes, index, values, buffer, descriptor};
}

} // namespace nx::acnh
