#pragma once

#include <array>
#include <bit>
#include <cstdint>

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

} // namespace nx::acnh
