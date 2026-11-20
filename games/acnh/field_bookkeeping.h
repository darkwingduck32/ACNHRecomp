#pragma once

#include "field_queries.h"
#include "shovel_terrain.h"

namespace nx::acnh {

struct TerrainPropertyOwner { uint64_t address; };

template<class Memory>
uint16_t terrain_property_u16(uint64_t base, TerrainPropertyOwner owner, uint32_t index, Memory& memory) {
    const auto object = owner.address;
    const auto count = memory.template read<int32_t>(object + 0x1c8);
    uint64_t row;
    if (count >= 1) {
        if (index >= uint32_t(count)) return 0xffff;
        const auto size = memory.template read<uint32_t>(object + 0x1b8);
        const auto list = memory.template read<uint64_t>(object + 0x1c0);
        row = memory.template read<uint64_t>(list + 8ull * (index < size ? index : 0));
    } else {
        const auto database = memory.template read<uint64_t>(object + 0xb8);
        if (!database) return 0xffff;
        const auto header = memory.template read<uint64_t>(database + 8);
        if (!header) return 0xffff;
        if (index < memory.template read<uint32_t>(object + 0xd0)) {
            const auto flags = memory.template read<uint64_t>(object + 0xd8);
            if (!memory.template read<uint8_t>(flags + std::bit_cast<int32_t>(index))) return 0xffff;
        }
        if (index >= memory.template read<uint32_t>(header)) return 0xffff;
        const auto stride = memory.template read<uint32_t>(header + 4);
        const auto columns = memory.template read<uint16_t>(header + 8);
        if (!stride || !columns) return 0xffff;
        const uint32_t prefix = memory.template read<uint8_t>(header + 10) ? 0x1c : 0xc;
        row = header + uint32_t(stride * index + 8u * columns + prefix);
    }
    if (!row) return 0xffff;
    if (!memory.template read<uint32_t>(row)) return 0;
    const auto ready = memory.template read<uint64_t>(base + 0x4ab5c58);
    const auto slots = memory.template read<uint64_t>(base + 0x4ab5c60);
    const auto field = cached_column<14>(row, 7, 0x54706054, ready, slots, memory);
    return field ? memory.template read<uint16_t>(field) : 0;
}

template<class Memory>
bool terrain_property_flag(uint64_t base, TerrainPropertyOwner owner, uint32_t code, Memory& memory) {
    const auto object = owner.address;
    const uint32_t index = uint32_t(uint16_t(code)) - memory.template read<uint16_t>(object + 0x200);
    if (index >= memory.template read<uint32_t>(object + 0x1e0)) return false;
    const auto list = memory.template read<uint64_t>(object + 0x1e8);
    const auto row = memory.template read<uint64_t>(list + 8ll * std::bit_cast<int32_t>(index));
    if (!row || !memory.template read<uint32_t>(row)) return false;
    const auto ready = memory.template read<uint64_t>(base + 0x4ab5c58);
    const auto slots = memory.template read<uint64_t>(base + 0x4ab5c60);
    const auto field = cached_column<14>(row, 8, 0xa4db9685, ready, slots, memory);
    return field && memory.template read<uint8_t>(field) != 0;
}

template<class Memory>
FieldBounds field_layer_bounds(uint64_t base, FieldLayerRef layer, const Memory& memory) {
    const auto index = memory.template read<uint32_t>(layer.address + 8);
    const auto owner = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab27a8));
    const auto offset = index == 0 ? 0x1f8ull : 0x30ull + 8ull * (index < 57 ? index : 0);
    const auto node = memory.template read<uint64_t>(owner + offset);
    return {node ? memory.template read<uint64_t>(owner + offset) + 0x38 : base + 0x5200388};
}

struct TerrainFlagSummary { uint64_t address; };

template<class Memory>
void terrain_flag_summary(TerrainGrid grid, HalfTileRef tile, TerrainFlagSummary output, Memory& memory) {
    const auto target = output.address + 8;
    memory.template write<uint8_t>(target, 0);
    const auto x = memory.template read<int32_t>(tile.address + 8);
    const auto z = memory.template read<int32_t>(tile.address + 12);
    if (x < 0 || z < 0) return;
    const auto width = memory.template read<uint32_t>(grid.address + 0x38);
    if (uint32_t(x) >= width || uint32_t(z) >= memory.template read<uint32_t>(grid.address + 0x3c)) return;
    const uint32_t index = width * uint32_t(z) + uint32_t(x);
    const auto flags = memory.template read<uint64_t>(grid.address + 0x70);
    const uint64_t cell = index < memory.template read<uint32_t>(grid.address + 0x68)
        ? flags + 4ll * std::bit_cast<int32_t>(index) : 0;
    constexpr std::array<uint8_t, 7> bits{4, 8, 1, 2, 16, 32, 64};
    uint8_t result = 0;
    for (unsigned i = 0; i < bits.size(); ++i) {
        if (!(memory.template read<uint32_t>(cell) & (1u << (8 + i)))) continue;
        if (i == 4 && (memory.template read<uint32_t>(cell) & 0x40)) continue;
        result |= bits[i];
        memory.template write<uint8_t>(target, result);
    }
}

struct HalfTileWorkQueue { uint64_t address; };

template<class Memory>
void enqueue_half_tile(HalfTileWorkQueue queue, uint32_t packed_x, uint32_t packed_z, Memory& memory) {
    const auto owner = queue.address;
    const auto x = std::bit_cast<int16_t>(uint16_t(packed_x));
    const auto z = std::bit_cast<int16_t>(uint16_t(packed_z));
    const auto width = memory.template read<uint32_t>(owner + 8);
    if (uint32_t(x) >= width || uint32_t(z) >= memory.template read<uint32_t>(owner + 12)) return;
    const auto index = std::bit_cast<int32_t>(width * uint32_t(z) + uint32_t(x));
    const auto ref_count = owner + 0x1c;
    auto bitmap = memory.template read<uint64_t>(owner + 0x10);
    auto limit = memory.template read<int32_t>(owner + 0x18);
    auto references = memory.template read<uint32_t>(ref_count);
    const auto valid_bit = [&] { return index >= 0 && bitmap && index < limit; };
    const auto bit = 1u << (uint32_t(index) % 32);
    const auto bitmap_word = [&] { return bitmap + 4ull * (uint32_t(index) / 32); };
    memory.template write<uint32_t>(ref_count, references + 1);
    const bool present = valid_bit() && (memory.template read<uint32_t>(bitmap_word()) & bit);
    memory.template write<uint32_t>(ref_count, references);
    if (present) return;

    const auto capacity = memory.template read<int32_t>(owner + 0x28);
    const auto length = memory.template read<int32_t>(owner + 0x30);
    if (length < capacity) {
        const auto head = memory.template read<uint32_t>(owner + 0x2c);
        const uint32_t position = head + uint32_t(length);
        const int32_t slot = std::bit_cast<int32_t>(
            position - (std::bit_cast<int32_t>(position) < capacity ? 0 : uint32_t(capacity)));
        memory.template write<uint32_t>(owner + 0x30, uint32_t(length) + 1);
        const auto records = memory.template read<uint64_t>(owner + 0x20);
        memory.template write<uint32_t>(records + 4ll * slot, uint16_t(packed_x) | (packed_z << 16));
        bitmap = memory.template read<uint64_t>(owner + 0x10);
        limit = memory.template read<int32_t>(owner + 0x18);
        references = memory.template read<uint32_t>(ref_count);
    }

    ++references;
    memory.template write<uint32_t>(ref_count, references);
    if (valid_bit()) {
        memory.template write<uint32_t>(bitmap_word(), memory.template read<uint32_t>(bitmap_word()) | bit);
        references = memory.template read<uint32_t>(ref_count);
    }
    memory.template write<uint32_t>(ref_count, references - 1);
}

} // namespace nx::acnh
