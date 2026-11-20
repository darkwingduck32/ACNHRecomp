#pragma once

#include "shovel_item_queries.h"
#include "shovel_target.h"
#include "shovel_terrain.h"

#include <cmath>

namespace nx::acnh {

struct ShovelPointQuery {
    uint64_t point;
    ShovelTargetRequest request;
};

enum class ShovelPointStage { height_sample, raised, level };
enum class ShovelPointItemId {
    raised_alternate, raised_copy, raised_metadata, raised_catalog, raised_confirm,
    level_catalog, level_confirm, level_alternate, level_copy, level_metadata,
};
enum class ShovelPointCategory { raised, raised_low, raised_other, level_first, level_second, level_last };

template<class Memory, class Services>
class ShovelPointClassifier {
public:
    ShovelPointClassifier(uint64_t base, Memory& memory, Services& services)
        : base_(base), memory_(memory), services_(services), items_(base, memory) {}

    uint32_t classify(ShovelPointQuery query) {
        const auto address = query.request.address;
        for (uint64_t offset = 0; offset < 12; offset += 4)
            memory_.template write<uint32_t>(address + 0x2c + offset,
                memory_.template read<uint32_t>(query.point + offset));
        const auto sample_stage = stage(ShovelPointStage::height_sample);
        const float terrain = services_.terrain_height(query.point, stage_index(sample_stage));
        const float surface = services_.surface_height(query.point);
        const float limit = read_float(address + 0x24);
        if (services_.point_blocked(query.point)) return result(address, 14);

        const float origin = read_float(address + 4);
        if (terrain - origin > limit) return classify_raised(query, surface, limit);
        if (!(terrain > origin - 1.25f)) return 14;

        if (memory_.template read<uint8_t>(address + 0x40)) {
            ShovelTargetPoint alternate;
            if (!services_.can_step(point_at(address), point_at(query.point), alternate)) {
                set_point(address + 0x2c, alternate);
                const uint32_t retry = services_.try_alternate(point_at(address), alternate, limit);
                if (retry && retry != 14) return result(address, retry);
            }
        }
        return classify_level(query, surface);
    }

private:
    float read_float(uint64_t address) const {
        return std::bit_cast<float>(memory_.template read<uint32_t>(address));
    }

    ShovelTargetPoint point_at(uint64_t address) const {
        return {read_float(address), read_float(address + 4), read_float(address + 8)};
    }

    void set_point(uint64_t address, ShovelTargetPoint point) {
        memory_.template write<uint32_t>(address, std::bit_cast<uint32_t>(point.x));
        memory_.template write<uint32_t>(address + 4, std::bit_cast<uint32_t>(point.y));
        memory_.template write<uint32_t>(address + 8, std::bit_cast<uint32_t>(point.z));
    }

    uint32_t result(uint64_t request, uint32_t code) {
        memory_.template write<uint32_t>(request + 0x38, code);
        return code;
    }

    HalfTile tile_at(uint64_t address) const {
        const auto coordinate = [](float value) { return std::isnan(value) ? 0 : terrain_half_tile(value); };
        return {coordinate(read_float(address)), coordinate(read_float(address + 8))};
    }

    uint64_t stage(ShovelPointStage phase) {
        const auto slot = memory_.template read<uint64_t>(base_ + 0x4ab2720);
        const auto scene = memory_.template read<uint64_t>(slot);
        if (scene && memory_.template read<uint64_t>(scene + 0x330)) {
            const auto holder = memory_.template read<uint64_t>(base_ + 0x4ab2640);
            const auto owner = memory_.template read<uint64_t>(holder);
            const auto name_index = memory_.template read<uint32_t>(scene + 0x338);
            if (services_.stage_hash(owner, name_index, phase) == 0x758f7eaf)
                return memory_.template read<uint64_t>(base_ + 0x4ac3a80);
        }
        return base_ + 0x54c04a0;
    }

    uint16_t stage_index(uint64_t stage_address) const {
        const uint64_t head = base_ + 0x54c1b58;
        auto node = memory_.template read<uint64_t>(head + 8);
        if (node == head) return 0;
        const auto key = memory_.template read<uint32_t>(stage_address + 0x38);
        uint16_t index = 0;
        for (; node != head; node = memory_.template read<uint64_t>(node + 8), ++index) {
            if (memory_.template read<uint32_t>(node - 8) == key) return index == 0xffff ? 0 : index;
        }
        return 0;
    }

    FieldItem lookup(HalfTile tile, ShovelPointStage phase) {
        const auto current = stage(phase);
        return services_.field_record(tile, memory_.template read<uint32_t>(current + 0x38), phase);
    }

    ItemCatalog catalog() const {
        const auto slot = memory_.template read<uint64_t>(base_ + 0x4abbc68);
        return {memory_.template read<uint64_t>(slot)};
    }

    ItemCatalog alternate_catalog(uint64_t owner) const {
        return {memory_.template read<uint64_t>(owner + 0x57e58)};
    }

    struct AlternateDescriptor {
        ItemDescriptor descriptor;
        uint8_t flags;
    };

    AlternateDescriptor alternate_descriptor(FieldItem item, ShovelPointStage phase) {
        const bool raised = phase == ShovelPointStage::raised;
        const auto slot = memory_.template read<uint64_t>(base_ + 0x4ab43c0);
        const auto before = memory_.template read<uint64_t>(slot);
        const auto id = services_.item_id(item, raised ? ShovelPointItemId::raised_alternate : ShovelPointItemId::level_alternate);
        const auto current = memory_.template read<uint64_t>(slot);
        if (!current || !items_.entry(alternate_catalog(before), id).address) return {};

        const auto descriptor = services_.copy_item(item);
        const auto flags = memory_.template read<uint8_t>(item.address + 2);
        const auto copied = services_.item_id({descriptor.address + 8},
            raised ? ShovelPointItemId::raised_copy : ShovelPointItemId::level_copy);
        if (memory_.template read<uint64_t>(slot) && items_.entry(alternate_catalog(current), copied).address) {
            const auto metadata_slot = memory_.template read<uint64_t>(base_ + 0x4ac0428);
            const ItemCatalog metadata{memory_.template read<uint64_t>(metadata_slot)};
            const auto key = services_.item_id(item,
                raised ? ShovelPointItemId::raised_metadata : ShovelPointItemId::level_metadata);
            if (const auto entry = items_.entry(metadata, key); entry.address)
                memory_.template write<uint64_t>(descriptor.address + 0x10, entry.address);
        }
        return {descriptor, flags};
    }

    uint32_t category(ItemDescriptor descriptor, ShovelPointCategory use) {
        const CatalogEntry metadata{memory_.template read<uint64_t>(descriptor.address + 0x10)};
        return services_.category(FieldItem{descriptor.address + 8}, metadata, use);
    }

    uint32_t classify_raised(ShovelPointQuery query, float surface, float limit) {
        const auto address = query.request.address;
        const auto tile = tile_at(query.point);
        const auto item = lookup(tile, ShovelPointStage::raised);
        const auto owner_slot = memory_.template read<uint64_t>(base_ + 0x4ab27c8);
        const auto owner = memory_.template read<uint64_t>(owner_slot);
        if (owner && memory_.template read<uint64_t>(owner + 0xa8)
            && services_.terrain_excluded(TerrainOwner{owner}))
            return result(address, 11);

        if (surface - read_float(address + 4) > limit) {
            if (services_.blocked_step(point_at(query.point))) return result(address, 14);
            const auto moved = services_.adjust_step(query);
            const auto current_tile = tile_at(query.point);
            if (moved > 0 || (!services_.terrain_barrier(current_tile) && surface - read_float(address + 4) > 10.f))
                return result(address, 13);
            return result(address, 12);
        }

        if (const auto alternate = alternate_descriptor(item, ShovelPointStage::raised); alternate.descriptor.address) {
            const bool usable = services_.item_supported(item, ShovelPointStage::raised)
                && services_.alternate_enabled(alternate.descriptor, ShovelPointStage::raised);
            return result(address, usable ? 1 : 9);
        }
        const auto main_catalog = catalog();
        if (!items_.enabled(main_catalog)
            || !items_.entry(main_catalog, services_.item_id(item, ShovelPointItemId::raised_catalog)).address
            || services_.item_id(item, ShovelPointItemId::raised_confirm) == 0xea79)
            return 0;

        const auto descriptor = services_.describe_item(item, ShovelPointStage::raised);
        if (category(descriptor, ShovelPointCategory::raised) == 0x2e) return result(address, 5);
        if (services_.category_override(descriptor)) return result(address, 3);
        if (category(descriptor, ShovelPointCategory::raised_low) - 0xbu < 7) return result(address, 4);
        return result(address, category(descriptor, ShovelPointCategory::raised_other) == 0x2d ? 6 : 9);
    }

    uint32_t classify_level(ShovelPointQuery query, float surface) {
        const auto address = query.request.address;
        const auto item = lookup(tile_at(query.point), ShovelPointStage::level);
        const auto main_catalog = catalog();
        if (items_.enabled(main_catalog)
            && items_.entry(main_catalog, services_.item_id(item, ShovelPointItemId::level_catalog)).address
            && services_.item_id(item, ShovelPointItemId::level_confirm) != 0xea79) {
            const auto descriptor = services_.describe_item(item, ShovelPointStage::level);
            if (category(descriptor, ShovelPointCategory::level_first) - 0x12u < 6) return result(address, 7);
            if (category(descriptor, ShovelPointCategory::level_second) - 0x23u < 9) return result(address, 8);
            if (category(descriptor, ShovelPointCategory::level_last) - 0xbu >= 7) return result(address, 9);
            if (read_float(address + 4) - surface > 5.f) return 14;
            return result(address, 4);
        }

        const auto alternate = alternate_descriptor(item, ShovelPointStage::level);
        if (!alternate.descriptor.address) return 0;
        services_.metadata_enabled(alternate.descriptor);
        if (alternate.flags & 4) return result(address, 10);
        const bool custom = services_.item_supported(item, ShovelPointStage::level)
            && !services_.alternate_enabled(alternate.descriptor, ShovelPointStage::level);
        return result(address, custom ? 2 : 9);
    }

    uint64_t base_;
    Memory& memory_;
    Services& services_;
    ShovelItemQueries<Memory> items_;
};

} // namespace nx::acnh
