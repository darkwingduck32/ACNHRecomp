#pragma once

#include "shovel_stage.h"
#include "shovel_stage_lookup.h"
#include "shovel_terrain.h"

namespace nx::acnh {

struct ShovelStageContext { uint64_t address; };
struct ShovelStageBaseHeightQuery { uint64_t point, material; ShovelStageContext context; };
struct ShovelStageFieldQuery { HalfTileRef point, root; uint32_t layer; ShovelStageContext context; };
struct ShovelStageFieldKey { uint32_t table; ShovelStageKey key; };

enum class ShovelStageSelection { height, initial_field, current_field };

template<class Memory>
class ShovelStageContextQueries {
public:
    ShovelStageContextQueries(uint64_t base, const Memory& memory) : base_(base), memory_(memory) {}

    template<class Services>
    float base_height(ShovelStageBaseHeightQuery query, Services& services) const {
        const auto scene_slot = memory_.template read<uint64_t>(base_ + 0x4ab2720);
        const auto desired = ordinal(query.context);
        const auto current = current_stage(scene_slot, ShovelStageSelection::height, services);
        const bool same = desired == shovel_stage_ordinal(base_, current, memory_);
        if (same && !alternate(query.context)) return current_height(query, services);

        const auto table = stage_table(desired);
        if (!table) return same ? current_height(query, services) : 0.f;
        if (table == 1 || table == 0x32) {
            const auto owner = memory_.template read<uint64_t>(base_ + 0x4bfae30);
            if (!owner || !memory_.template read<uint64_t>(owner + 0x90)) return -500.f;
            return services.sample_special(TerrainOwner{owner}, query);
        }
        return services.sample_other(query);
    }

    template<class Services>
    FieldItem field_item(ShovelStageFieldQuery query, Services& services) const {
        const auto scene_slot = memory_.template read<uint64_t>(base_ + 0x4ab2720);
        const auto desired = ordinal(query.context);
        const auto current = current_stage(scene_slot, ShovelStageSelection::initial_field, services);
        const bool same = desired == shovel_stage_ordinal(base_, current, memory_);
        if (same && !alternate(query.context)) return current_item(scene_slot, query, services);
        const auto table = stage_table(desired);
        if (table) return services.lookup(query, ShovelStageFieldKey{table, {known_key(table)}});
        if (same) return current_item(scene_slot, query, services);
        return {base_ + 0x5486008};
    }

private:
    uint16_t ordinal(ShovelStageContext context) const {
        return memory_.template read<uint16_t>(context.address);
    }

    bool alternate(ShovelStageContext context) const {
        return memory_.template read<uint8_t>(context.address + 2) != 0;
    }

    uint32_t known_key(uint32_t table) const {
        return memory_.template read<uint32_t>(base_ + 0x506b108 + 4 * table);
    }

    uint32_t stage_table(uint16_t index) const {
        const auto head = base_ + 0x54c1b58;
        auto node = memory_.template read<uint64_t>(head + 8);
        while (node != head && index) {
            node = memory_.template read<uint64_t>(node + 8);
            --index;
        }
        const auto stage = node == head ? base_ + 0x54c04a0 : node - 0x40;
        const auto key = memory_.template read<uint32_t>(stage + 0x38);
        for (uint32_t table = 0; table < 0x39; ++table) {
            if (known_key(table) == key) return table;
        }
        return 0;
    }

    template<class Services>
    uint64_t current_stage(uint64_t scene_slot, ShovelStageSelection phase, Services& services) const {
        const auto scene = memory_.template read<uint64_t>(scene_slot);
        if (scene && memory_.template read<uint64_t>(scene + 0x330)) {
            const auto owner = memory_.template read<uint64_t>(base_ + 0x59ed668);
            const auto index = memory_.template read<uint32_t>(scene + 0x338);
            if (services.stage_hash(owner, index, phase) == 0x758f7eaf)
                return memory_.template read<uint64_t>(base_ + 0x4ac3a80);
        }
        return base_ + 0x54c04a0;
    }

    template<class Services>
    float current_height(ShovelStageBaseHeightQuery query, Services& services) const {
        const auto slot = memory_.template read<uint64_t>(base_ + 0x4ab27c8);
        return services.sample_current(TerrainOwner{memory_.template read<uint64_t>(slot)}, query);
    }

    template<class Services>
    FieldItem current_item(uint64_t scene_slot, ShovelStageFieldQuery query, Services& services) const {
        const auto stage = current_stage(scene_slot, ShovelStageSelection::current_field, services);
        const auto key = memory_.template read<uint32_t>(stage + 0x38);
        return services.lookup(query, ShovelStageFieldKey{0, {key}});
    }

    uint64_t base_;
    const Memory& memory_;
};

struct ShovelStageCellQuery {
    uint64_t stage;
    uint32_t layer;
    HalfTileRef point, root;
};

template<class Memory, class Services>
FieldItem shovel_stage_cell(uint64_t base, ShovelStageCellQuery query, const Memory& memory, Services& services) {
    const FieldItem empty{base + 0x5486008};
    if (!services.accept_stage(query.stage, query.layer, query.point)) return empty;

    const auto slot = memory.template read<uint64_t>(base + 0x4ab27a8);
    const auto owner = memory.template read<uint64_t>(slot);
    const uint32_t table = memory.template read<uint32_t>(query.stage + 8);
    const uint64_t grid_slot = table == 0 ? owner + 0x1f8 : owner + 0x30 + 8ull * (table < 0x39 ? table : 0);
    const auto grid = memory.template read<uint64_t>(grid_slot);
    const auto layer = query.layer < 8 ? query.layer : 0;
    const auto provider = memory.template read<uint64_t>(grid + 0xd0 + 16ull * layer);
    if (!provider) return empty;

    const auto selected = memory.template read<uint32_t>(provider + 0x198);
    return services.cell_at(provider + 0x130 + 0x30ull * (selected < 2 ? selected : 0), query.point, query.root);
}

} // namespace nx::acnh
