#pragma once

#include "shovel_item_queries.h"
#include "shovel_stage_lookup.h"
#include "shovel_terrain.h"

#include <cmath>

namespace nx::acnh {

struct ShovelStageHeightQuery {
    uint64_t point;
    uint64_t material;
    uint64_t stage;
    uint32_t flags;
    bool include_special_items;
};

enum class StageHeightLayer { primary, secondary };
enum class StageHeightItemId { direct_entry, direct_confirm, primary_entry, copied, metadata, secondary_entry };

template<class Memory, class Services>
class ShovelStageHeight {
public:
    ShovelStageHeight(uint64_t base, Memory& memory, Services& services)
        : base_(base), memory_(memory), services_(services), items_(base, memory) {}

    float sample(ShovelStageHeightQuery query) {
        const auto scratch = services_.local_material();
        memory_.template write<uint32_t>(scratch, 0);
        const auto material = query.material ? query.material : scratch;
        float height = services_.base_height(query.point, material, query.stage);

        const auto first = services_.field_item(tile_at(query.point), query.stage, StageHeightLayer::primary);
        if (empty(first)) {
            mark_solid(material);
            return height;
        }

        const auto catalog_slot = memory_.template read<uint64_t>(base_ + 0x4abbc68);
        const ItemCatalog catalog{memory_.template read<uint64_t>(catalog_slot)};
        if (items_.enabled(catalog)
            && items_.entry(catalog, services_.item_id(first, StageHeightItemId::direct_entry)).address) {
            const auto descriptor = services_.describe_item(first);
            const auto id = services_.item_id(first, StageHeightItemId::direct_confirm);
            const auto variant = services_.height_variant(first);
            if (variant == 2 && !(query.flags & 1)) {
                const auto tile = tile_at(query.point);
                const float mid_x = static_cast<float>(tile.x & ~1) * 5.f + 5.f;
                const float mid_z = static_cast<float>(tile.z & ~1) * 5.f + 5.f;
                services_.surface_height(query.point);
                const float dx = mid_x - read_float(query.point);
                const float dz = mid_z - read_float(query.point + 8);
                if (!(std::sqrt(std::fma(dx, dx, dz * dz)) <= 5.f)) return height;
            }
            if (variant == 1 || variant == 2) {
                mark_solid(material);
                return id == 0xea79 ? height : height + services_.descriptor_height(descriptor);
            }
        }

        const auto alternate_slot = memory_.template read<uint64_t>(base_ + 0x4ab43c0);
        if (!alternate_contains(alternate_slot, first, StageHeightItemId::primary_entry)
            || services_.alternate_blocked(first)) {
            mark_solid(material);
            return height;
        }

        if (services_.item_supported(first, StageHeightLayer::primary)) {
            const auto descriptor = copy_primary(first, alternate_slot);
            if (services_.alternate_enabled(descriptor, StageHeightLayer::primary)) {
                const auto position = tile_at(query.point);
                const auto index = memory_.template read<uint16_t>(query.stage);
                const auto stage = current_stage();
                if (terrain_enabled(position, stage, index, query.stage, StageHeightLayer::primary)) {
                    mark_solid(material);
                    const float offset = services_.alternate_height(descriptor, StageHeightLayer::primary, true);
                    height += offset == 0.f ? 5.f : offset;
                }
            } else if (query.include_special_items && services_.special_item(first, StageHeightLayer::primary)) {
                height += services_.alternate_height(descriptor, StageHeightLayer::primary, false);
                mark_solid(material);
            }
        }

        const auto second = services_.field_item(tile_at(query.point), query.stage, StageHeightLayer::secondary);
        if (!empty(second) && alternate_contains(alternate_slot, second, StageHeightItemId::secondary_entry)
            && services_.item_supported(second, StageHeightLayer::secondary)) {
            const auto descriptor = services_.describe_secondary(second);
            if (services_.alternate_enabled(descriptor, StageHeightLayer::secondary)) {
                const auto position = tile_at(query.point);
                const auto index = memory_.template read<uint16_t>(query.stage);
                const auto stage = services_.secondary_stage();
                if (terrain_enabled(position, stage, index, query.stage, StageHeightLayer::secondary)) {
                    mark_solid(material);
                    height += services_.alternate_height(descriptor, StageHeightLayer::secondary, true);
                }
            } else if (query.include_special_items && services_.special_item(second, StageHeightLayer::secondary)) {
                height += services_.alternate_height(descriptor, StageHeightLayer::secondary, false);
            }
        }
        mark_solid(material);
        return height;
    }

private:
    float read_float(uint64_t addr) const {
        return std::bit_cast<float>(memory_.template read<uint32_t>(addr));
    }

    HalfTile tile_at(uint64_t point) const {
        const auto coordinate = [](float value) { return std::isnan(value) ? 0 : terrain_half_tile(value); };
        return {coordinate(read_float(point)), coordinate(read_float(point + 8))};
    }

    bool empty(FieldItem item) const {
        return memory_.template read<uint16_t>(item.address) == 0xfffe;
    }

    void mark_solid(uint64_t address) {
        memory_.template write<uint32_t>(address, 7);
    }

    ItemCatalog alternate_catalog(uint64_t owner) const {
        return {memory_.template read<uint64_t>(owner + 0x57e58)};
    }

    bool alternate_contains(uint64_t slot, FieldItem item, StageHeightItemId use) const {
        const auto owner = memory_.template read<uint64_t>(slot);
        const auto id = services_.item_id(item, use);
        return memory_.template read<uint64_t>(slot) && items_.entry(alternate_catalog(owner), id).address;
    }

    ItemDescriptor copy_primary(FieldItem item, uint64_t alternate_slot) {
        const auto owner = memory_.template read<uint64_t>(alternate_slot);
        const auto descriptor = services_.copy_item(item);
        const auto copied = services_.item_id({descriptor.address + 8}, StageHeightItemId::copied);
        if (memory_.template read<uint64_t>(alternate_slot) && items_.entry(alternate_catalog(owner), copied).address) {
            const auto slot = memory_.template read<uint64_t>(base_ + 0x4ac0428);
            const ItemCatalog metadata{memory_.template read<uint64_t>(slot)};
            const auto id = services_.item_id(item, StageHeightItemId::metadata);
            if (const auto entry = items_.entry(metadata, id); entry.address)
                memory_.template write<uint64_t>(descriptor.address + 0x10, entry.address);
        }
        return descriptor;
    }

    uint64_t current_stage() const {
        const auto scene = memory_.template read<uint64_t>(base_ + 0x59ed670);
        if (scene && memory_.template read<uint64_t>(scene + 0x330)) {
            const auto owner = memory_.template read<uint64_t>(base_ + 0x59ed668);
            const auto name = memory_.template read<uint32_t>(scene + 0x338);
            if (services_.stage_hash(owner, name) == 0x758f7eaf)
                return memory_.template read<uint64_t>(base_ + 0x4ac3a80);
        }
        return base_ + 0x54c04a0;
    }

    bool terrain_enabled(HalfTile tile, uint64_t stage, uint16_t index, uint64_t context, StageHeightLayer layer) const {
        const bool same_stage = index == shovel_stage_ordinal(base_, stage, memory_);
        if (same_stage && !memory_.template read<uint8_t>(context + 2)) return services_.terrain_here(tile, layer);
        const auto mapped = services_.mapped_stage(index, layer);
        if (mapped) return services_.terrain_elsewhere(mapped, tile, layer);
        return same_stage && services_.terrain_here(tile, layer);
    }

    uint64_t base_;
    Memory& memory_;
    Services& services_;
    ShovelItemQueries<Memory> items_;
};

} // namespace nx::acnh
