#include "../games/acnh/field_provider.h"
#include "../games/acnh/shovel_item_metadata.h"
#include "../games/acnh/shovel_survey.h"
#include "../games/acnh/shovel_terrain.h"
#include "fixture_memory.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using namespace nx::acnh;

constexpr uint64_t base = 0x4000000000;
constexpr uint64_t scratch = 0x20000, layer = 0x21000, position = 0x21100, root = 0x21200;
constexpr uint64_t output = 0x21300, descriptor = 0x21400, provider = 0x22000;
constexpr uint64_t provider_data = 0x23000, records = 0x30000;
constexpr uint64_t catalog_slot = 0x24000, owner = 0x25000, catalog = 0x26000, rows = 0x27000;
constexpr uint64_t header = 0x28000, detail = header + 0x14;
constexpr uint64_t metadata_categories = 0x28100, metadata_db = 0x28200;
constexpr uint64_t metadata_cache_flag = 0x28400, metadata_cache = 0x28410;
constexpr uint64_t empty = 0xfffe;
constexpr uint64_t placed_item = 0x162a;
constexpr int32_t first_tile = 4, center_tile = 5;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Memory : FixtureMemory {
    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
    }
};

struct Services {
    Memory& memory;
    FieldQueries<Memory> fields{base, memory};
    unsigned reads = 0, classifications = 0, predicate_calls = 0;
    uint32_t stage = 0;
    bool selection_suitable = false;

    uint32_t stage_hash(uint32_t) { throw std::runtime_error("unexpected dynamic stage lookup"); }
    bool stage_rule(uint32_t) { return false; }
    void set_stage(uint32_t key, bool) { stage = key; }

    bool contains(FieldCellQuery query) {
        const auto bounds = fields.bounds_for({0}, query.channel);
        return bounds && fields.contains(bounds, query.position);
    }
    void initialize(FieldItem item) {
        require(memory.read<uint64_t>(item.address) == empty, "field cell was not initialized");
    }

    ProviderTable table(ProviderReader reader, ProviderTableUse) {
        return provider_table(reader, memory, [](uint64_t target) { return ProviderTable{target}; });
    }
    uint32_t width(ProviderTable, ProviderDimensionUse) { return provider_width(); }
    uint32_t height(ProviderTable, ProviderDimensionUse) { return provider_height(); }
    bool ready(ProviderReader) { return provider_ready(); }
    uint64_t offset(ProviderTable table, ProviderCell cell, bool) {
        return provider_cell_offset(table, cell, memory);
    }
    ProviderAnchor resolve_anchor(FieldCellProvider, ProviderCell cell) { return {{provider_data}, cell}; }
    void cleanup_anchor() {}
    bool release_required(ProviderContext) { throw std::runtime_error("unexpected guest context"); }
    void release(ProviderContext) { throw std::runtime_error("unexpected guest release"); }

    uint64_t read(FieldCellProvider source, HalfTileRef point, HalfTileRef anchor) {
        read_provider_cell(base, {source, point, anchor, {output}}, memory, *this);
        return memory.read<uint64_t>(output);
    }
    SurveyCell read(HalfTile point, bool anchor) {
        ++reads;
        memory.write<int32_t>(position + 8, point.x);
        memory.write<int32_t>(position + 12, point.z);
        memory.write<uint64_t>(root + 8, 0);
        fields.read_cell({{layer}, {0}, {position}}, {output}, {anchor ? root : 0}, *this);
        return {memory.read<uint64_t>(output),
                {memory.read<int32_t>(root + 8), memory.read<int32_t>(root + 12)}};
    }

    uint32_t item_id(FieldItem item) { return fields.item_id(item); }
    uint32_t item_id(FieldItem item, ShovelItemIdUse) { return item_id(item); }
    uint32_t item_id(uint64_t item, SurveyItemUse) {
        memory.write<uint64_t>(descriptor + 8, item);
        return item_id({descriptor + 8});
    }
    bool metadata_occupied(uint64_t item, uint64_t row) {
        ++classifications;
        require(row == detail, "catalog did not attach the metadata row");
        memory.write<uint64_t>(descriptor + 8, item);
        memory.write<uint64_t>(descriptor + 0x10, row);
        return shovel_metadata_kind(base, {descriptor}, memory, *this) == 0;
    }
    uint32_t alternate_kind(ItemDescriptor value) {
        const auto kind = shovel_alternate_kind(base, value, memory, *this);
        require(kind == 0x13, "unexpected alternate metadata kind");
        return kind;
    }
    bool has_special_kind(ItemDescriptor) { throw std::runtime_error("unexpected special metadata kind"); }
    bool metadata_enabled(ItemDescriptor value, size_t path) {
        require(path == 0 || path == 2, "unexpected metadata predicate path");
        ++predicate_calls;
        return shovel_metadata_enabled(base, value, memory);
    }
    CatalogEntry extended_row(ItemDescriptor) { throw std::runtime_error("unexpected extended row"); }
    bool single_tile_extent(uint64_t) { throw std::runtime_error("unexpected extent fallback"); }

    ItemDescriptor copy_alternate(FieldItem item) {
        memory.write<uint64_t>(descriptor + 8, memory.read<uint64_t>(item.address));
        memory.write<uint64_t>(descriptor + 0x10, 0);
        return {descriptor};
    }
    bool recognized(FieldItem item) { return ShovelItemQueries{base, memory}.recognized(item, *this); }
    bool blocked_by_alternate(FieldItem item) {
        return ShovelItemQueries{base, memory}.blocked_by_alternate(item, *this);
    }
    bool metadata_enabled(ItemDescriptor value) {
        require(memory.read<uint64_t>(value.address + 0x10) == detail,
                "selected item classifier lost the resolved metadata row");
        return true;
    }
    bool metadata_suitable(ItemDescriptor) { return selection_suitable; }
    FieldItem copy_special(FieldItem, uint16_t) { throw std::runtime_error("unexpected special item"); }
    FieldItem copy_fallback(FieldItem) { throw std::runtime_error("unexpected fallback item"); }
    ItemDescriptor describe(FieldItem) { throw std::runtime_error("unexpected category descriptor"); }
    bool has_category(ItemDescriptor) { throw std::runtime_error("unexpected category check"); }
    bool category_flag(CatalogEntry, uint32_t) { throw std::runtime_error("unexpected category flag"); }
    uint32_t category(FieldItem, CatalogEntry, size_t) { throw std::runtime_error("unexpected category lookup"); }

    bool grid_allows(TerrainGrid, TerrainRect area) {
        return area.min.x == 10 && area.min.z == 10 && area.max.x == 11 && area.max.z == 11;
    }
    bool quadrant_materials(TerrainOwner, HalfTile point, std::array<uint32_t, 4>& materials) {
        require(point.x >= 10 && point.x <= 11 && point.z >= 10 && point.z <= 11,
                "terrain lookup left the selected tile");
        materials.fill(0);
        return true;
    }
    bool material_allows(TerrainTable table, uint32_t material, size_t) {
        return fields.terrain_has(table, material, TerrainProperty::diggable);
    }
    bool layer_blocks(HalfTile point) {
        return fields.record_flag(fields.record_at(fields.grid_for({0}), point));
    }
};

void check_neighborhood(uint16_t metadata_kind) {
    Memory memory;
    memory.write<uint64_t>(base + 0x5486008, empty);
    memory.write<uint64_t>(base + 0x4ab2720, 0x24100);
    memory.write<uint64_t>(0x24100, 0);
    memory.write<uint32_t>(base + 0x54c04d8, 0x678);
    memory.write<uint64_t>(base + 0x4ab27a8, 0x24200);
    memory.write<uint64_t>(0x24200, 0x24300);
    memory.write<uint64_t>(0x24300 + 0x1f8, 0x24400);
    memory.write<uint64_t>(0x24400 + 0x60, 1);
    memory.write<int32_t>(0x24400 + 0x48, 8);
    memory.write<int32_t>(0x24400 + 0x4c, 8);
    memory.write<int32_t>(0x24400 + 0x58, 13);
    memory.write<int32_t>(0x24400 + 0x5c, 13);
    memory.write<uint64_t>(0x24400 + 0xd0, provider);
    memory.write<uint32_t>(layer + 8, 0);
    memory.write<uint64_t>(provider + 0x10, 0);
    memory.write<uint64_t>(provider + 0x30, provider_data);
    memory.write<uint64_t>(provider + 0x1b8, 0);
    memory.write<uint64_t>(provider_data + 8, records);
    memory.write<uint64_t>(provider_data + 0x10, 0);
    memory.write<uint64_t>(provider_data + 0x28, 0);
    for (uint32_t x = 8; x <= 13; ++x)
        for (uint32_t z = 8; z <= 13; ++z)
            memory.write<uint64_t>(records + 8ull * (x * 0xc0 + z), empty);
    memory.write<uint64_t>(records + 8ull * (10 * 0xc0 + 10), placed_item);
    memory.write<uint64_t>(records + 8ull * (10 * 0xc0 + 11), 0x01000000'0000fffd);

    memory.write<uint64_t>(base + 0x4ab43c0, catalog_slot);
    memory.write<uint64_t>(catalog_slot, owner);
    memory.write<uint64_t>(owner + 0x57e58, catalog);
    memory.write<uint64_t>(base + 0x4ac0428, 0x24500);
    memory.write<uint64_t>(0x24500, catalog);
    memory.write<uint16_t>(catalog + 0x1f8, placed_item);
    memory.write<uint32_t>(catalog + 0x1d8, 1);
    memory.write<uint64_t>(catalog + 0x1e0, rows);
    memory.write<uint64_t>(rows, detail);
    memory.write<uint64_t>(base + 0x4abbc68, 0x24600);
    memory.write<uint64_t>(0x24600, 0);
    memory.write<uint32_t>(header, 1);
    memory.write<uint32_t>(header + 4, 32);
    memory.write<uint16_t>(header + 8, 1);
    memory.write<uint8_t>(header + 10, 0);
    memory.write<uint32_t>(header + 0xc, 0x90bc0855);
    memory.write<uint32_t>(header + 0x10, 4);
    memory.write<uint32_t>(detail, detail - header);
    memory.write<uint8_t>(detail + 4, 1);
    memory.write<uint64_t>(base + 0x4ac00c8, metadata_categories);
    memory.write<uint32_t>(metadata_categories, 1);
    memory.write<uint64_t>(metadata_categories + 8, 0x28110);
    memory.write<uint16_t>(0x28110, 1);
    memory.write<uint64_t>(base + 0x522a778, metadata_db);
    memory.write<uint32_t>(metadata_db + 0x1b0, 1);
    memory.write<uint64_t>(metadata_db + 0x1b8, rows);
    memory.write<int32_t>(metadata_db + 0x1c0, 2);
    memory.write<uint64_t>(base + 0x4abfeb0, metadata_cache_flag);
    memory.write<uint64_t>(base + 0x4abfeb8, metadata_cache);
    memory.write<uint8_t>(metadata_cache_flag, 0);
    memory.write<uint32_t>(base + 0x5226ec0, 1);
    memory.write<uint64_t>(base + 0x5226ec8, 0x24700);
    memory.write<uint16_t>(0x24700, 0);
    memory.write<uint32_t>(base + 0x5226e60, 1);
    memory.write<uint64_t>(base + 0x5226e68, 0x24708);
    memory.write<uint16_t>(0x24708, metadata_kind);

    Services services{memory};
    services.fields.initialize_survey({scratch});
    memory.write<int32_t>(scratch + 0x28, first_tile);
    memory.write<int32_t>(scratch + 0x2c, first_tile);
    memory.write<int32_t>(scratch + 0x160, first_tile);
    memory.write<int32_t>(scratch + 0x164, first_tile);
    memory.write<int32_t>(scratch + 0x170, center_tile + 1);
    memory.write<int32_t>(scratch + 0x174, center_tile + 1);
    gather_shovel_neighborhood(base, {scratch}, memory, services);

    require(services.stage == 0x678 && services.reads == 36, "gather did not visit the complete neighborhood");
    require(services.classifications == 1 && services.predicate_calls == 2, "deeper item classification did not run");
    require(memory.read<uint8_t>(metadata_cache_flag) == 1 && memory.read<uint32_t>(metadata_cache + 0x2c) == 4,
            "metadata predicate did not resolve its cached column");
    for (unsigned index = 0; index < 36; ++index) {
        const uint64_t expected = index == 16 || index == 19 ? placed_item : empty;
        require(memory.read<uint64_t>(scratch + 0x30 + 8ull * index) == expected,
                "neighborhood record differs from the expected field item");
    }
    require(memory.read<uint32_t>(scratch + 0x18) == (metadata_kind == 0 ? 0x10u : 0u),
            "the selected item's metadata did not control the center occupancy bit");

    const FieldItem selected{scratch + 0x30 + 8 * 16};
    require(services.fields.item_id(selected) == placed_item, "selected item ID differs from the anchor");
    const ShovelItemQueries items{base, memory};
    require(!items.suitable(selected, services), "selected item should fail the metadata suitability check");
    services.selection_suitable = true;
    require(items.suitable(selected, services), "selected item should pass the metadata suitability check");

    constexpr uint64_t terrain_owner = 0x29000, terrain_grid = 0x2a000, terrain_table = 0x2b000;
    constexpr uint64_t terrain_header = 0x2c000, terrain_row = terrain_header + 0x14;
    memory.write<uint64_t>(base + 0x4ab27c8, 0x24800);
    memory.write<uint64_t>(0x24800, terrain_owner);
    memory.write<uint64_t>(terrain_owner + 0xa8, terrain_grid);
    memory.write<uint32_t>(terrain_grid + 0x38, 12);
    memory.write<uint32_t>(terrain_grid + 0x3c, 12);
    memory.write<uint32_t>(terrain_grid + 0x68, 0);
    memory.write<uint64_t>(terrain_grid + 0x70, 0);
    constexpr uint64_t layer_grid = 0x2d000, layer_records = 0x2e000;
    memory.write<uint64_t>(base + 0x509d040, 0x24300);
    memory.write<uint64_t>(0x24400 + 0xb0, layer_grid);
    memory.write<uint64_t>(layer_grid + 0x18, 0x100000001);
    memory.write<uint32_t>(layer_grid + 0x28, center_tile);
    memory.write<uint32_t>(layer_grid + 0x2c, center_tile);
    memory.write<uint32_t>(layer_grid + 0x30, 1);
    memory.write<uint64_t>(layer_grid + 0x38, layer_records);
    for (unsigned i = 0; i < 4; ++i) {
        const uint64_t record = layer_records + 0x28 + 16 * i;
        memory.write<uint8_t>(record + 8, 0);
        memory.write<int16_t>(record + 12, 10 + (i & 1));
        memory.write<int16_t>(record + 14, 10 + (i >> 1));
    }
    memory.write<uint64_t>(base + 0x4ab46c8, 0x24900);
    memory.write<uint64_t>(0x24900, terrain_table);
    memory.write<int32_t>(terrain_table + 8 + 0x1c0, 1);
    memory.write<uint32_t>(terrain_table + 8 + 0x1b0, 1);
    memory.write<uint64_t>(terrain_table + 8 + 0x1b8, 0x24a00);
    memory.write<uint64_t>(0x24a00, terrain_row);
    memory.write<uint16_t>(terrain_header + 8, 1);
    memory.write<uint8_t>(terrain_header + 10, 0);
    memory.write<uint32_t>(terrain_header + 0xc, 0x472724ed);
    memory.write<uint32_t>(terrain_header + 0x10, 4);
    memory.write<uint32_t>(terrain_row, terrain_row - terrain_header);
    memory.write<uint8_t>(base + 0x4bfab80, 0);
    memory.write<uint8_t>(terrain_row + 4, 1);
    require(can_dig_terrain(base, {center_tile, center_tile}, memory, services), "diggable terrain was rejected");
    memory.write<uint8_t>(layer_records + 0x28 + 16 * 3 + 8, 1);
    require(!can_dig_terrain(base, {center_tile, center_tile}, memory, services), "layer-blocked terrain was accepted");
    memory.write<uint8_t>(layer_records + 0x28 + 16 * 3 + 8, 0);
    memory.write<uint8_t>(terrain_row + 4, 0);
    require(!can_dig_terrain(base, {center_tile, center_tile}, memory, services), "blocked terrain was accepted");
}

}  // namespace

int main() {
    try {
        check_neighborhood(0);
        check_neighborhood(3);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
