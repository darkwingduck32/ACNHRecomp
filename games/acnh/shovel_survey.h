#pragma once

#include "field_queries.h"

#include <bit>
#include <limits>

namespace nx::acnh {

enum class SurveyItemUse { anchor, metadata_key, metadata_detail, actor, extent };

struct SurveyCell {
    uint64_t item;
    HalfTile anchor;
};

template<class Memory, class Services>
void gather_shovel_neighborhood(uint64_t base, SurveyScratch scratch,
                                Memory& memory, Services& services) {
    const uint64_t stage_context = memory.template read<uint64_t>(
        memory.template read<uint64_t>(base + 0x4ab2720));
    uint64_t stage = base + 0x54c04a0;
    if (stage_context && memory.template read<uint64_t>(stage_context + 0x330) &&
        services.stage_hash(memory.template read<uint32_t>(stage_context + 0x338)) == 0x758f7eaf)
        stage = memory.template read<uint64_t>(base + 0x4ac3a80);
    const auto stage_key = memory.template read<uint32_t>(stage + 0x38);
    services.set_stage(stage_key, services.stage_rule(stage_key));

    const auto step = [](int32_t value, uint32_t count = 1) {
        return std::bit_cast<int32_t>(static_cast<uint32_t>(value) + count);
    };
    const auto half_coordinate = [](int32_t tile, bool upper) {
        const float origin = static_cast<float>(tile) * 10.0f;
        const float world = upper ? (origin + 10.0f) - 2.5f : origin + 2.5f;
        const auto integral = world >= 2147483648.0f ? std::numeric_limits<int32_t>::max()
            : world <= -2147483648.0f ? std::numeric_limits<int32_t>::min()
            : static_cast<int32_t>(world);
        return static_cast<int32_t>(static_cast<float>(integral) / 5.0f) -
               static_cast<int32_t>(world < 0.0f);
    };
    const auto store_item = [&](uint64_t slot, uint64_t item) {
        memory.template write<uint32_t>(slot, static_cast<uint32_t>(item));
        memory.template write<uint32_t>(slot + 4, static_cast<uint32_t>(item >> 32));
    };
    const auto find_item_row = [&](uint64_t table, uint32_t id) -> uint64_t {
        const uint32_t index = (id & 0xffff) - memory.template read<uint16_t>(table + 0x1f8);
        if (index >= memory.template read<uint32_t>(table + 0x1d8)) return 0;
        const uint64_t rows = memory.template read<uint64_t>(table + 0x1e0);
        return memory.template read<uint64_t>(rows + 8ll * static_cast<int32_t>(index));
    };
    const auto has_actor_item = [&](uint64_t actor, uint64_t item) {
        constexpr uint64_t links[]{0xb0, 0x488, 0x2b0};
        for (uint64_t offset : links) {
            const uint64_t link = memory.template read<uint64_t>(actor + offset);
            if (!link || !memory.template read<uint64_t>(link + 8)) return false;
        }
        return find_item_row(actor, services.item_id(item, SurveyItemUse::actor)) != 0;
    };
    const auto occupied = [&](uint64_t item) {
        const uint64_t catalog_slot = memory.template read<uint64_t>(base + 0x4ab43c0);
        const uint64_t catalog_before = memory.template read<uint64_t>(catalog_slot);
        const uint32_t id = services.item_id(item, SurveyItemUse::anchor);
        const uint64_t catalog = memory.template read<uint64_t>(catalog_slot);
        if (catalog && find_item_row(memory.template read<uint64_t>(catalog_before + 0x57e58), id)) {
            const uint32_t rechecked = services.item_id(item, SurveyItemUse::metadata_key);
            const uint64_t current_slot = memory.template read<uint64_t>(base + 0x4ab43c0);
            uint64_t detail = 0;
            if (memory.template read<uint64_t>(current_slot) &&
                find_item_row(memory.template read<uint64_t>(catalog + 0x57e58), rechecked)) {
                const uint64_t detail_table = memory.template read<uint64_t>(
                    memory.template read<uint64_t>(base + 0x4ac0428));
                detail = find_item_row(detail_table, services.item_id(item, SurveyItemUse::metadata_detail));
            }
            return services.metadata_occupied(item, detail);
        }

        const uint64_t actor = memory.template read<uint64_t>(
            memory.template read<uint64_t>(base + 0x4abbc68));
        if (actor && has_actor_item(actor, item)) return true;
        return services.item_id(item, SurveyItemUse::extent) - 0xf619u <= 6 && services.single_tile_extent(item);
    };
    const auto mark_occupied = [&](int32_t x, int32_t z) {
        const auto origin_x = memory.template read<int32_t>(scratch.address + 0x28);
        if (x < origin_x || x > step(origin_x, 3)) return;
        const auto origin_z = memory.template read<int32_t>(scratch.address + 0x2c);
        if (z < origin_z || z > step(origin_z, 3)) return;
        const uint32_t col = static_cast<uint32_t>(x) - static_cast<uint32_t>(origin_x);
        const uint32_t row = static_cast<uint32_t>(z) - static_cast<uint32_t>(origin_z);
        if (col > 2 || row > 2) return;
        const uint64_t mask = scratch.address + 0x18;
        memory.template write<uint32_t>(mask, memory.template read<uint32_t>(mask) | (1u << (row * 3 + col)));
    };

    const auto stop_z = step(memory.template read<int32_t>(scratch.address + 0x174));
    auto z = memory.template read<int32_t>(scratch.address + 0x164);
    if (z == stop_z) return;
    const auto initial_x = memory.template read<int32_t>(scratch.address + 0x160);
    auto x = initial_x;

    do {
        const uint32_t col = static_cast<uint32_t>(x) -
                             memory.template read<uint32_t>(scratch.address + 0x160);
        const uint32_t row = static_cast<uint32_t>(z) -
                             memory.template read<uint32_t>(scratch.address + 0x164);
        const uint64_t output = scratch.address + 0x30 + (col <= 2 && row <= 2 ? 32 * (row * 3 + col) : 0);
        const HalfTile first{half_coordinate(x, false), half_coordinate(z, false)};
        const HalfTile opposite{half_coordinate(x, true), half_coordinate(z, true)};
        const auto cell = services.read(first, true);
        if (cell.anchor.x == first.x && cell.anchor.z == first.z && occupied(cell.item))
            mark_occupied(x, z);
        store_item(output, cell.item);
        store_item(output + 8, services.read({opposite.x, first.z}, false).item);
        store_item(output + 16, services.read(opposite, false).item);
        store_item(output + 24, services.read({first.x, opposite.z}, false).item);

        if (x >= memory.template read<int32_t>(scratch.address + 0x170)) {
            x = memory.template read<int32_t>(scratch.address + 0x160);
            z = step(z);
        } else {
            x = step(x);
        }
    } while (x != initial_x || z != stop_z);
}

}  // namespace nx::acnh
