#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace nx::acnh {

struct TerrainDrawManager { uint64_t address; };
struct TerrainDrawDescriptor { uint64_t address; };

struct TerrainDrawPass {
    TerrainDrawManager manager;
    TerrainDrawDescriptor descriptor;
    uint64_t table{};
    uint64_t context{};
    uint64_t binding{};
    uint32_t context_index{};
    uint32_t selected_index{};
    uint8_t pass{};
    bool supported{};
};

template<class Memory>
TerrainDrawPass prepare_terrain_draw(TerrainDrawManager manager, TerrainDrawDescriptor descriptor,
                                     uint64_t module_base, const Memory& memory) {
    TerrainDrawPass result{manager, descriptor};
    const auto owner = memory.template read<uint64_t>(descriptor.address + 8);
    if (!owner) return result;
    const auto draw = memory.template read<uint64_t>(owner + 8);
    if (!draw) return result;
    const auto selection = memory.template read<uint64_t>(draw + 0x70);
    if (!selection) return result;

    const auto pass_info = memory.template read<uint64_t>(descriptor.address);
    result.context_index = memory.template read<uint32_t>(selection + 4);
    const auto count = memory.template read<uint32_t>(manager.address + 0x500);
    result.table = memory.template read<uint64_t>(manager.address + 0x508);
    if (!pass_info || !result.table) return result;
    result.pass = memory.template read<uint8_t>(pass_info + 0x21);
    result.selected_index = result.context_index < count ? result.context_index : 0;
    result.context = result.table + int64_t{std::bit_cast<int32_t>(result.selected_index)} * 0x4a8;

    // Scope the ordinary passes to the renderer paths witnessed in gameplay.
    switch (result.pass) {
    case 0:
    case 2:
    case 9:
    case 12:
        break;
    default:
        return result;
    }
    const auto binding_slot = memory.template read<uint64_t>(module_base + 0x4ab31c8);
    if (!binding_slot) return result;
    const auto binding_source = memory.template read<uint64_t>(binding_slot);
    if (!binding_source) return result;
    result.binding = memory.template read<uint64_t>(binding_source + 0x58);
    result.supported = true;
    return result;
}

struct TerrainDrawList {
    uint64_t address;
    unsigned lane;
};

template<class Draw>
void draw_terrain_lists(const TerrainDrawPass& pass, Draw draw) {
    constexpr std::array<uint64_t, 3> offsets{0x20, 0x38, 0x50};
    for (unsigned lane = 0; lane < offsets.size(); ++lane)
        draw(TerrainDrawList{pass.context + offsets[lane], lane});
}

struct TerrainDrawEntry {
    TerrainDrawManager manager;
    TerrainDrawDescriptor descriptor;
    uint64_t head;
    uint64_t context;
    uint64_t binding_cell;
};

struct TerrainDrawEntryScratch {
    uint64_t binding;
    uint64_t pass;
    uint64_t manager;
    uint64_t table;
};

struct TerrainDrawEntryPlan {
    uint64_t first;
    uint64_t rows;
    uint32_t index;
    uint64_t table;
    uint64_t table_end;
    uint8_t pass;
};

template<class Memory>
TerrainDrawEntryPlan prepare_terrain_draw_entry(TerrainDrawEntry input, TerrainDrawEntryScratch scratch,
                                                Memory& memory) {
    const auto pass_info = memory.template read<uint64_t>(input.descriptor.address);
    const auto index_info = memory.template read<uint64_t>(input.descriptor.address + 8);
    memory.template write<uint64_t>(scratch.binding, input.binding_cell);
    const auto count = memory.template read<uint32_t>(input.context + 0x498);
    const auto index = memory.template read<uint16_t>(index_info + 0x16);
    const auto rows = memory.template read<uint64_t>(input.context + 0x4a0);
    const auto pass = memory.template read<uint8_t>(pass_info + 0x21);
    const auto first = memory.template read<uint64_t>(input.head + 8);
    memory.template write<uint64_t>(scratch.pass, pass);
    memory.template write<uint64_t>(scratch.manager, input.manager.address);
    const uint32_t selected = index < count ? index : 0;
    const auto row = rows + selected * 0x108ull + 8;
    const auto table_end = row + pass * 16ull;
    const auto table = pass < 16 ? table_end : row;
    memory.template write<uint64_t>(scratch.table, table);
    return {first, rows, selected, table, table_end, pass};
}

struct TerrainDrawNonempty {
    uint64_t head;
    uint64_t first_link;
    uint64_t rows;
    uint32_t ordinal;
};

struct TerrainDrawEntryFields {
    uint64_t pass;
    uint64_t manager;
    uint64_t header;
};

struct TerrainDrawNonemptyPlan {
    uint64_t first_node;
    uint64_t end_node;
    uint64_t row;
    uint64_t row_flags;
    uint64_t manager_data;
    uint64_t binding;
    int64_t signed_pass;
    uint64_t pass;
    int64_t displacement;
    uint32_t gated;
};

template<class Memory>
TerrainDrawNonemptyPlan prepare_terrain_draw_nonempty(TerrainDrawNonempty input, TerrainDrawEntryFields fields,
                                                      const Memory& memory) {
    const auto pass = memory.template read<uint64_t>(fields.pass);
    const auto manager = memory.template read<uint64_t>(fields.manager);
    const auto header = memory.template read<uint64_t>(fields.header);
    const int64_t displacement = memory.template read<int32_t>(input.head + 0x14);
    const auto binding = memory.template read<uint64_t>(header + 8);
    const auto row = input.rows + input.ordinal * 0x108ull;
    const auto signed_pass = std::bit_cast<int8_t>(static_cast<uint8_t>(pass));
    const uint32_t gated = (0x207u >> (pass & 31)) & 1;
    return {input.first_link - static_cast<uint64_t>(displacement),
            input.head - static_cast<uint64_t>(displacement), row, row + 4, manager + 0x560,
            binding, signed_pass, pass, displacement, gated};
}

struct TerrainDrawNodeFields {
    uint64_t pass;
    uint64_t gate;
    uint64_t index;
};

struct TerrainDrawNodeTest {
    uint32_t mask;
    uint32_t flags;
};

template<class Memory>
std::optional<TerrainDrawNodeTest> test_terrain_draw_node(
        TerrainDrawNodeFields fields, uint64_t table, uint64_t node, const Memory& memory) {
    const auto pass = static_cast<uint32_t>(memory.template read<uint64_t>(fields.pass));
    if (pass != 0 && pass != 2 && pass != 9) return std::nullopt;
    if (!memory.template read<uint32_t>(fields.gate)) return std::nullopt;
    const auto index = memory.template read<uint64_t>(fields.index);
    if (index != pass || !node) return std::nullopt;
    const auto mask = memory.template read<uint32_t>(table + index * 4);
    const auto flags = memory.template read<uint32_t>(node + 0x18);
    return TerrainDrawNodeTest{mask, flags};
}

} // namespace nx::acnh
