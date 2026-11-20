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

struct FieldLayer {
    uint32_t index;
};

struct FieldGrid {
    uint64_t address;
    explicit operator bool() const { return address != 0; }
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

    FieldGrid grid_for(FieldLayer layer) const {
        const uint64_t owner = memory_.template read<uint64_t>(base_ + 0x509d040);
        const uint64_t selector = layer.index == 0 ? 0x1f8
            : 0x30 + 8ull * (layer.index < 57 ? layer.index : 0);
        const uint64_t node = memory_.template read<uint64_t>(owner + selector);
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
    uint64_t base_;
    Memory& memory_;
};

}  // namespace nx::acnh
