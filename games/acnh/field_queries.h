#pragma once

#include "field_column.h"
#include "field_record.h"
#include "tile_record_flag.h"

#include <cmath>
#include <cstdint>

namespace nx::acnh {

struct HalfTile {
    int32_t x;
    int32_t z;
};

struct HalfTileRef {
    uint64_t address;
};

struct FieldLayer {
    uint32_t index;
};

struct FieldChannel {
    uint32_t index;
};

struct FieldBounds {
    uint64_t address;
    explicit operator bool() const { return address != 0; }
};

struct FieldGrid {
    uint64_t address;
    explicit operator bool() const { return address != 0; }
};

struct FieldLayerRef {
    uint64_t address;
};

struct FieldCellQuery {
    FieldLayerRef layer;
    FieldChannel channel;
    HalfTileRef position;
};

struct FieldCellProvider {
    uint64_t address;
};

struct FieldItem {
    uint64_t address;
};

struct SurveyScratch {
    uint64_t address;
};

struct TerrainTable {
    uint64_t address;
};

enum class TerrainProperty { diggable, alternate };

struct HalfTileRecord {
    uint64_t address;
    explicit operator bool() const { return address != 0; }
};

inline uint32_t half_tile_coordinate(int32_t coordinate) {
    return static_cast<uint32_t>(static_cast<int32_t>(std::floor(static_cast<float>(coordinate) * 0.5f)));
}

template<class Memory>
class FieldQueries {
public:
    FieldQueries(uint64_t base, Memory& memory) : base_(base), memory_(memory) {}

    FieldBounds bounds_for(FieldLayer layer, FieldChannel channel) const {
        const uint64_t owner_pointer = memory_.template read<uint64_t>(base_ + 0x4ab27a8);
        const uint64_t owner = memory_.template read<uint64_t>(owner_pointer);
        const uint64_t node = layer_node(owner, layer);
        if (!node) return {};
        const uint32_t slot = channel.index < 8 ? channel.index : 0;
        if (!memory_.template read<uint64_t>(node + 0x60 + 8ull * slot)) return {};
        return {node + 0x38};
    }

    bool contains(FieldBounds bounds, HalfTileRef tile) const {
        const int32_t min_x = memory_.template read<int32_t>(bounds.address + 0x10);
        const int32_t x = memory_.template read<int32_t>(tile.address + 8);
        if (x < min_x || x > memory_.template read<int32_t>(bounds.address + 0x20)) return false;

        const int32_t min_z = memory_.template read<int32_t>(bounds.address + 0x14);
        const int32_t z = memory_.template read<int32_t>(tile.address + 12);
        return z >= min_z && z <= memory_.template read<int32_t>(bounds.address + 0x24);
    }


    FieldGrid grid_for(FieldLayer layer) const {
        const uint64_t owner = memory_.template read<uint64_t>(base_ + 0x509d040);
        const uint64_t node = layer_node(owner, layer);
        return {node ? memory_.template read<uint64_t>(node + 0xb0) : 0};
    }

    HalfTileRecord record_at(FieldGrid grid, HalfTile tile) const {
        const uint64_t record = tile_record(base_ + 0x50a50f0, grid.address,
                                            half_tile_coordinate(tile.x), half_tile_coordinate(tile.z),
                                            memory_);
        for (uint64_t slot = record + 0x28; slot < record + 0x68; slot += 16) {
            if (memory_.template read<int16_t>(slot + 12) == tile.x &&
                memory_.template read<int16_t>(slot + 14) == tile.z) {
                return {slot};
            }
        }
        return {};
    }

    bool record_flag(HalfTileRecord record) const {
        return record && (memory_.template read<uint8_t>(record.address + 8) & 1) != 0;
    }

    bool terrain_has(TerrainTable table, uint32_t index, TerrainProperty property) const {
        const uint64_t row = field_record(table.address + 8, index, memory_);
        if (!row) return false;
        const bool diggable = property == TerrainProperty::diggable;
        const uint32_t slot = diggable ? 4 : 9;
        const uint32_t hash = diggable ? 0x472724ed : 0x0254bd05;
        const uint64_t field = field_column(row, slot, hash, base_ + 0x4bfab80, memory_);
        return field && memory_.template read<uint8_t>(field) != 0;
    }

    uint32_t auxiliary_item_index(uint32_t kind) const {
        const uint64_t table = base_ + 0x522a758;
        for (uint32_t index = 0; index < 4; ++index) {
            if (memory_.template read<uint32_t>(table + 4 * index) == kind && index != 0)
                return index;
        }
        return 0;
    }

    uint32_t item_id(FieldItem item) const {
        return item_id(item, [this](uint32_t kind) { return auxiliary_item_index(kind); });
    }

    template<class ResolveAuxiliary>
    uint32_t item_id(FieldItem item, ResolveAuxiliary resolve_auxiliary) const {
        const uint16_t id = memory_.template read<uint16_t>(item.address);
        if (id == 0x16a1 || id == 0x3100) return id;

        const uint8_t tag = memory_.template read<uint8_t>(item.address + 3);
        const uint32_t selector = (tag >> 2) & 15;
        switch (tag & 3) {
            case 0:
                return id;
            case 1:
                return memory_.template read<uint32_t>(base_ + 0x4077938 + 4 * selector);
            case 2:
                return 0x1180;
            case 3:
                if (selector == 15)
                    return memory_.template read<uint32_t>(base_ + 0x4077938 + 4 * 16);
                const uint32_t rank = resolve_auxiliary(selector);
                if (rank)
                    return memory_.template read<uint32_t>(base_ + 0x4001d00 + 4 * rank);
                if (selector == 11) return 0x3a19;
                return selector == 10 ? 0x306 : 0x1225;
        }
        return id;
    }

    template<class Services>
    void read_cell(FieldCellQuery query, FieldItem output, HalfTileRef root, Services& services) const {
        if (services.contains(query)) {
            memory_.template write<uint64_t>(output.address, 0xfffe);
            services.initialize(output);
            const FieldLayer layer{memory_.template read<uint32_t>(query.layer.address + 8)};
            const uint64_t owner_pointer = memory_.template read<uint64_t>(base_ + 0x4ab27a8);
            const uint64_t node = layer_node(memory_.template read<uint64_t>(owner_pointer), layer);
            const uint32_t slot = query.channel.index < 8 ? query.channel.index : 0;
            const FieldCellProvider provider{memory_.template read<uint64_t>(node + 0xd0 + 16ull * slot)};
            if (provider.address) {
                const uint64_t item = services.read(provider, query.position, root);
                memory_.template write<uint32_t>(output.address, static_cast<uint32_t>(item));
                memory_.template write<uint32_t>(output.address + 4, static_cast<uint32_t>(item >> 32));
                return;
            }
        }

        const uint64_t empty = base_ + 0x5486008;
        const auto lower = memory_.template read<uint32_t>(empty);
        const auto upper = memory_.template read<uint32_t>(empty + 4);
        memory_.template write<uint32_t>(output.address, lower);
        memory_.template write<uint32_t>(output.address + 4, upper);
    }

    template<class Services>
    void initialize_survey_items(FieldItem first, Services& services) const {
        for (uint64_t offset = 0; offset < 36 * 8; offset += 8) {
            const FieldItem item{first.address + offset};
            memory_.template write<uint64_t>(item.address, 0xfffe);
            services.initialize(item);
        }
    }

    void initialize_survey(SurveyScratch scratch) const {
        const uint64_t out = scratch.address;
        const uint64_t empty = base_ + 0x5486008;
        memory_.template write<uint32_t>(out + 0x18, 0);
        memory_.template write<uint64_t>(out + 0x160, 0);
        const auto item = memory_.template read<uint16_t>(empty);
        const auto kind = memory_.template read<uint8_t>(empty + 2);
        const auto flags = memory_.template read<uint8_t>(empty + 3);
        const auto data = memory_.template read<uint32_t>(empty + 4);
        memory_.template write<uint64_t>(out + 0x170, 0);

        const auto write_prefix = [&](uint64_t offset, uint16_t id, uint8_t type, uint8_t bits) {
            memory_.template write<uint16_t>(out + offset, id);
            memory_.template write<uint8_t>(out + offset + 2, type);
            memory_.template write<uint8_t>(out + offset + 3, bits);
        };
        for (uint64_t offset = 0x30; offset < 0xf0; offset += 8)
            write_prefix(offset, item, kind, flags);

        // The original reloads the empty record during the last prefix.
        memory_.template write<uint16_t>(out + 0xf0, item);
        const auto second_item = memory_.template read<uint16_t>(empty);
        memory_.template write<uint8_t>(out + 0xf2, kind);
        const auto second_kind = memory_.template read<uint8_t>(empty + 2);
        memory_.template write<uint8_t>(out + 0xf3, flags);
        const auto second_flags = memory_.template read<uint8_t>(empty + 3);
        const auto second_data = memory_.template read<uint32_t>(empty + 4);

        for (uint64_t offset = 0x34; offset <= 0xf4; offset += 8)
            memory_.template write<uint32_t>(out + offset, data);
        for (uint64_t offset = 0xf8; offset <= 0x148; offset += 8) {
            write_prefix(offset, second_item, second_kind, second_flags);
            memory_.template write<uint32_t>(out + offset + 4, second_data);
        }
    }

private:
    uint64_t layer_node(uint64_t owner, FieldLayer layer) const {
        const uint64_t selector = layer.index == 0 ? 0x1f8
            : 0x30 + 8ull * (layer.index < 57 ? layer.index : 0);
        return memory_.template read<uint64_t>(owner + selector);
    }

    uint64_t base_;
    Memory& memory_;
};

}  // namespace nx::acnh
