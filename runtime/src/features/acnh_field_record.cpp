#include "../../../games/acnh/field_column.h"
#include "../../../games/acnh/field_column_25.h"
#include "../../../games/acnh/field_flag.h"
#include "../../../games/acnh/field_record.h"
#include "../../../games/acnh/field_queries.h"
#include "../../../games/acnh/field_provider.h"
#include "../../../games/acnh/field_provider_anchor.h"
#include "../../../games/acnh/field_provider_storage.h"
#include "../../../games/acnh/field_provider_context.h"
#include "../../../games/acnh/field_provider_context_flag.h"
#include "../../../games/acnh/field_provider_context_handle.h"
#include "../../../games/acnh/field_item_init.h"
#include "../../../games/acnh/shovel_target.h"
#include "../../../games/acnh/shovel_point_classifier.h"
#include "../../../games/acnh/shovel_obstacle.h"
#include "../../../games/acnh/shovel_stage_height.h"
#include "../../../games/acnh/shovel_stage_context.h"
#include "../../../games/acnh/shovel_step.h"
#include "../../../games/acnh/shovel_intermediate.h"
#include "../../../games/acnh/shovel_marker.h"
#include "../../../games/acnh/shovel_collision.h"
#include "../../../games/acnh/shovel_mixed_cast.h"
#include "../../../games/acnh/shovel_alternate_terrain.h"
#include "../../../games/acnh/shovel_provider_record.h"
#include "../../../games/acnh/shovel_height.h"
#include "../../../games/acnh/shovel_grid_height.h"
#include "../../../games/acnh/shovel_terrain.h"
#include "../../../games/acnh/shovel_terrain_helpers.h"
#include "../../../games/acnh/shovel_item_queries.h"
#include "../../../games/acnh/shovel_item_metadata.h"
#include "../../../games/acnh/shovel_stage.h"
#include "../../../games/acnh/record_category.h"
#include "../../../games/acnh/tile_record_flag.h"

#include "features/acnh_provider_member.h"
#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <nx/runtime.h>

namespace nx::features {
namespace {

constexpr uint64_t row_entry = 0x8807cc;
constexpr uint64_t column_entry = 0x8823a0;
constexpr uint64_t field_flag_entry = 0x15e0e30;
constexpr uint64_t column_cache_offset = 0x4bfab80;
using ColumnCache = std::array<uint8_t, 60>;
Fn original_field_record = nullptr;
uint64_t main_base = 0;
bool use_cpp = false;
bool trace_field_record = false;
Fn original_field_column = nullptr;
bool use_cpp_column = false;
bool trace_field_column = false;
Fn original_field_flag = nullptr;
bool use_cpp_field_flag = false;
bool trace_field_flag = false;
std::atomic<unsigned> field_flag_traces{0};

struct GuestMemory {
    std::string* trace = nullptr;

    template<class T>
    T read(uint64_t address) const {
        const T value = ld<T>(address);
        if (trace) {
            char field[64];
            std::snprintf(field, sizeof(field), "%llx:%zu:%llx,",
                          static_cast<unsigned long long>(address), sizeof(T),
                          static_cast<unsigned long long>(static_cast<std::make_unsigned_t<T>>(value)));
            trace->append(field);
        }
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        st<T>(address, value);
    }
};

void field_record_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    bool field_probe = false;
    switch (caller) {
        case 0x15e0350: case 0x15e0398: case 0x15e03ec: case 0x15e0548: case 0x15e0590:
            field_probe = trace_field_record;
            break;
    }
    // The field probe's frame record contains the return address in the tree action.
    const uint64_t parent = field_probe ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const bool trace = parent == 0x23e0394 || parent == 0x23e03b0 || parent == 0x23e0524;
    if (!use_cpp && !trace) {
        original_field_record(cpu);
        return;
    }

    const uint64_t db = cpu.x[0];
    const uint32_t index = static_cast<uint32_t>(cpu.x[1]);
    std::string reads;
    const uint64_t result = acnh::field_record(db, index, GuestMemory{trace ? &reads : nullptr});
    if (use_cpp) cpu.x[0] = result;
    else original_field_record(cpu);

    if (trace) {
        log("acnh field row %s caller=%llx parent=%llx db=%llx index=%x out=%llx reads=%s",
            use_cpp ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(parent), static_cast<unsigned long long>(db),
            index, static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
    }
}

Fn original_indexed_record = nullptr;
bool use_cpp_indexed_record = false;
bool trace_indexed_record = false;
std::atomic<unsigned> indexed_record_samples{0};

void indexed_record_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const bool trace = trace_indexed_record && caller == 0x221f0c8 &&
        indexed_record_samples.fetch_add(1, std::memory_order_relaxed) < 24;
    if (!use_cpp_indexed_record && !trace) {
        original_indexed_record(cpu);
        return;
    }

    const uint64_t db = cpu.x[0];
    const uint32_t index = static_cast<uint32_t>(cpu.x[1]);
    std::string reads;
    const uint64_t result = acnh::field_record(db, index, GuestMemory{trace ? &reads : nullptr});
    if (use_cpp_indexed_record) cpu.x[0] = result;
    else original_indexed_record(cpu);
    if (trace)
        log("acnh indexed record %s caller=%llx parent=0 db=%llx index=%x out=%llx reads=%s",
            use_cpp_indexed_record ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(db), index, static_cast<unsigned long long>(cpu.x[0]),
            reads.c_str());
}

Fn original_half_tile_layer_flag = nullptr;
bool use_cpp_shovel_alternate_terrain = false;
bool trace_shovel_alternate_terrain = false;

bool is_shovel_alternate_terrain_frame(uint64_t frame) {
    return frame && ld<uint64_t>(frame + 8) == main_base + 0x1288bd4;
}

bool is_shovel_alternate_level_frame(uint64_t frame) {
    return frame && ld<uint64_t>(frame + 8) == main_base + 0x87bf8c
        && is_shovel_alternate_terrain_frame(ld<uint64_t>(frame));
}

bool use_cpp_half_tile_layer_flag = false;
bool trace_half_tile_layer_flag = false;
std::atomic<unsigned> layer_flag_samples[3]{};

void half_tile_layer_flag_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const bool terrain_caller = caller == 0x877380;
    const uint64_t parent = trace_half_tile_layer_flag && terrain_caller
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const size_t sample = parent == 0x21f778c ? 2 : terrain_caller ? 1 : 0;
    const bool trace = trace_half_tile_layer_flag && (terrain_caller || caller == 0x87c04c) &&
        layer_flag_samples[sample].fetch_add(1, std::memory_order_relaxed) < 12;
    const bool alternate = caller == 0x87c04c && is_shovel_alternate_terrain_frame(cpu.x[29]);
    const bool native = use_cpp_half_tile_layer_flag && (!alternate || use_cpp_shovel_alternate_terrain);
    const bool trace_alternate = alternate && trace_shovel_alternate_terrain;
    if (!native && !trace && !trace_alternate) {
        original_half_tile_layer_flag(cpu);
        return;
    }

    const uint64_t query = cpu.x[0];
    const acnh::FieldLayer layer{static_cast<uint32_t>(cpu.x[1])};
    std::string reads;
    const GuestMemory memory{trace ? &reads : nullptr};
    if (native || trace) {
        const acnh::FieldQueries fields{main_base, memory};
        const auto grid = fields.grid_for(layer);
        bool found = false;
        if (grid) {
            const acnh::HalfTile tile{memory.read<int32_t>(query + 8), memory.read<int32_t>(query + 12)};
            found = fields.record_flag(fields.record_at(grid, tile));
        }
        if (native) cpu.x[0] = found;
    }
    if (!native) original_half_tile_layer_flag(cpu);
    if (trace_alternate)
        log("acnh shovel alternate terrain %s entry=1285fe0 call=87c048 pos=%d,%d layer=%x out=%x",
            native ? "cpp" : "original", ld<int32_t>(query + 8), ld<int32_t>(query + 12), layer.index, unsigned(cpu.x[0]));
    if (trace)
        log("acnh half tile layer %s caller=%llx base=%llx query=%llx layer=%x out=%llx reads=%s parent=%llx",
            native ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(query), layer.index,
            static_cast<unsigned long long>(cpu.x[0]), reads.c_str(), static_cast<unsigned long long>(parent));
}

struct ColumnSnapshot {
    uint64_t base;
    ColumnCache cache;
    GuestMemory guest;

    template<class T>
    T read(uint64_t address) const {
        const uint64_t offset = address - base;
        if (offset > cache.size() - sizeof(T)) return guest.read<T>(address);
        T value;
        std::memcpy(&value, cache.data() + offset, sizeof(T));
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        std::memcpy(cache.data() + (address - base), &value, sizeof(T));
    }
};

ColumnCache read_cache(uint64_t address) {
    ColumnCache cache;
    std::memcpy(cache.data(), reinterpret_cast<const void*>(address), cache.size());
    return cache;
}

std::string cache_hex(std::span<const uint8_t> cache) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(2 * cache.size());
    for (uint8_t byte : cache) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0xf]);
    }
    return result;
}

void field_column_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    bool field_probe = false;
    switch (caller) {
        case 0x15e0374: case 0x15e03ac: case 0x15e040c: case 0x15e056c: case 0x15e05a4:
            field_probe = trace_field_column;
            break;
    }
    const uint64_t parent = field_probe ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const uint64_t row = cpu.x[0];
    const uint32_t column = static_cast<uint32_t>(cpu.x[1]);
    const uint32_t hash = static_cast<uint32_t>(cpu.x[2]);
    const uint64_t cache = main_base + column_cache_offset;
    const bool trace = parent == 0x23e0394 || parent == 0x23e03b0 || parent == 0x23e0524 ||
        (trace_field_column && !ld<uint8_t>(cache) && ld<uint32_t>(row));
    if (trace) {
        const auto before = read_cache(cache);
        std::string reads;
        ColumnSnapshot snapshot{cache, before, GuestMemory{&reads}};
        acnh::field_column(row, column, hash, cache, snapshot);
        if (use_cpp_column) {
            GuestMemory memory;
            cpu.x[0] = acnh::field_column(row, column, hash, cache, memory);
        } else {
            original_field_column(cpu);
        }
        log("acnh field column %s caller=%llx parent=%llx row=%llx column=%x hash=%x cache=%llx "
            "out=%llx before=%s after=%s reads=%s",
            use_cpp_column ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(parent), static_cast<unsigned long long>(row), column, hash,
            static_cast<unsigned long long>(cache), static_cast<unsigned long long>(cpu.x[0]),
            cache_hex(before).c_str(), cache_hex(read_cache(cache)).c_str(), reads.c_str());
    } else if (use_cpp_column) {
        GuestMemory memory;
        cpu.x[0] = acnh::field_column(row, column, hash, cache, memory);
    } else {
        original_field_column(cpu);
    }
}

void field_flag_adapter(CPU& cpu) {
    const bool trace = trace_field_flag && field_flag_traces.fetch_add(1, std::memory_order_relaxed) < 128;
    if (!use_cpp_field_flag && !trace) {
        original_field_flag(cpu);
        return;
    }

    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t position = cpu.x[0];
    const float radius = cpu.v[0].f32[0];
    const uint32_t flags = static_cast<uint32_t>(cpu.x[1]);
    std::string reads;
    const bool found = acnh::field_flag(main_base, position, radius, flags, GuestMemory{trace ? &reads : nullptr});
    if (use_cpp_field_flag) cpu.x[0] = found;
    else original_field_flag(cpu);

    if (trace) {
        log("acnh field flag %s caller=%llx base=%llx position=%llx s0=%08x flags=%x w0=%llx reads=%s",
            use_cpp_field_flag ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(position),
            std::bit_cast<uint32_t>(radius), flags, static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
    }
}

template<size_t Index>
void record_category_adapter(CPU& cpu);

struct CategoryRoute {
    uint64_t entry;
    uint64_t caller;
    Fn adapter;
    Fn original = nullptr;
    std::atomic<unsigned> samples{0};
};

std::array<CategoryRoute, 5> category_routes{{
    {0x23255e4, 0x2431044, record_category_adapter<0>},
    {0x2325688, 0x2430fc0, record_category_adapter<1>},
    {0x232572c, 0x2430fe4, record_category_adapter<2>},
    {0x23257d0, 0x2431004, record_category_adapter<3>},
    {0x2325874, 0x2431024, record_category_adapter<4>},
}};
bool use_cpp_categories = false;
bool trace_categories = false;

template<size_t Index>
void record_category_adapter(CPU& cpu) {
    auto& route = category_routes[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    const bool trace = trace_categories && caller == route.caller &&
        route.samples.fetch_add(1, std::memory_order_relaxed) < 12;
    if (!use_cpp_categories && !trace) {
        route.original(cpu);
        return;
    }

    const uint64_t row = cpu.x[0];
    std::string reads;
    constexpr uint16_t category = static_cast<uint16_t>(Index + 3);
    const bool accepted = acnh::record_category(main_base, row, category, GuestMemory{trace ? &reads : nullptr});
    if (use_cpp_categories) cpu.x[0] = accepted;
    else route.original(cpu);

    if (trace) {
        log("acnh record category %s entry=%llx caller=%llx base=%llx row=%llx w0=%llx reads=%s",
            use_cpp_categories ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
            static_cast<unsigned long long>(caller), static_cast<unsigned long long>(main_base),
            static_cast<unsigned long long>(row), static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
    }
}

Fn original_tile_record_flag = nullptr;
bool use_cpp_tile_record_flag = false;
bool trace_tile_record_flag = false;
std::atomic<unsigned> tile_record_traces{0};

void tile_record_flag_adapter(CPU& cpu) {
    const uint64_t query = cpu.x[0];
    const uint64_t caller = cpu.x[30] - main_base;
    const bool trace = trace_tile_record_flag && caller == 0x14c4a74 &&
        tile_record_traces.fetch_add(1, std::memory_order_relaxed) < 12;
    if (use_cpp_tile_record_flag || trace) {
        std::string reads;
        const bool found = acnh::tile_record_flag(main_base, query, GuestMemory{trace ? &reads : nullptr});
        if (use_cpp_tile_record_flag) cpu.x[0] = found;
        else original_tile_record_flag(cpu);
        if (trace) {
            log("acnh tile record flag %s caller=%llx base=%llx query=%llx w0=%llx reads=%s",
                use_cpp_tile_record_flag ? "cpp" : "original", static_cast<unsigned long long>(caller),
                static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(query),
                static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
        }
    } else {
        original_tile_record_flag(cpu);
    }
}

using ColumnSlots25 = std::array<uint8_t, 100>;

struct ColumnSnapshot25 {
    uint64_t ready_address;
    uint64_t slot_address;
    uint8_t ready;
    ColumnSlots25 slots;
    GuestMemory guest;

    template<class T>
    T read(uint64_t address) const {
        if (address == ready_address && sizeof(T) == 1) return static_cast<T>(ready);
        const uint64_t offset = address - slot_address;
        if (offset > slots.size() - sizeof(T)) return guest.read<T>(address);
        T value;
        std::memcpy(&value, slots.data() + offset, sizeof(T));
        return value;
    }

    template<class T>
    void write(uint64_t address, T value) {
        if (address == ready_address && sizeof(T) == 1) {
            ready = static_cast<uint8_t>(value);
        } else {
            std::memcpy(slots.data() + (address - slot_address), &value, sizeof(T));
        }
    }
};

template<size_t Index>
void cached_field_adapter(CPU& cpu);

struct CachedFieldRoute {
    uint64_t entry;
    uint64_t caller;
    Fn adapter;
    Fn original = nullptr;
    bool use_cpp = false;
    std::atomic<unsigned> samples{0};
};

std::array<CachedFieldRoute, 2> cached_field_routes{{
    {0x1146470, 0x34ae588, cached_field_adapter<0>},
    {0x113d890, 0x267ec88, cached_field_adapter<1>},
}};
bool trace_cached_field = false;

template<size_t Index>
void cached_field_adapter(CPU& cpu) {
    auto& route = cached_field_routes[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t row = cpu.x[0];
    const uint32_t index = static_cast<uint32_t>(cpu.x[1]);
    const uint32_t hash = static_cast<uint32_t>(cpu.x[2]);
    const auto evaluate = [&](auto& memory) -> uint64_t {
        if constexpr (Index == 0) return acnh::field_column_25(main_base, row, index, hash, memory);
        else return acnh::cached_field_bit(main_base, row, static_cast<int32_t>(index), memory);
    };
    const auto apply = [&] {
        if (route.use_cpp) {
            GuestMemory memory;
            cpu.x[0] = evaluate(memory);
        } else {
            route.original(cpu);
        }
    };

    if (!trace_cached_field || caller != route.caller || !row || !ld<uint32_t>(row) ||
        route.samples.fetch_add(1, std::memory_order_relaxed) >= 12) {
        apply();
        return;
    }

    const uint64_t ready = ld<uint64_t>(main_base + 0x4abbc70);
    const uint64_t slots = ld<uint64_t>(main_base + 0x4abbc78);
    const auto read_slots = [slots] {
        ColumnSlots25 snapshot;
        std::memcpy(snapshot.data(), reinterpret_cast<const void*>(slots), snapshot.size());
        return snapshot;
    };
    const auto ready_before = ld<uint8_t>(ready);
    const auto slots_before = read_slots();
    std::string reads;
    ColumnSnapshot25 snapshot{ready, slots, ready_before, slots_before, GuestMemory{&reads}};
    evaluate(snapshot);
    apply();

    log("acnh extended field %s entry=%llx caller=%llx base=%llx row=%llx index=%x hash=%x "
        "ready=%llx slots=%llx initial=%x final=%x before=%s after=%s out=%llx reads=%s",
        route.use_cpp ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
        static_cast<unsigned long long>(caller), static_cast<unsigned long long>(main_base),
        static_cast<unsigned long long>(row), index, hash, static_cast<unsigned long long>(ready),
        static_cast<unsigned long long>(slots), unsigned(ready_before), unsigned(ld<uint8_t>(ready)),
        cache_hex(slots_before).c_str(), cache_hex(read_slots()).c_str(),
        static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
}

template<size_t Index>
void terrain_property_adapter(CPU& cpu);

struct TerrainRoute {
    uint64_t entry;
    acnh::TerrainProperty property;
    uint64_t first_caller;
    uint64_t last_caller;
    Fn adapter;
    Fn original = nullptr;
    bool use_cpp = false;
    std::atomic<unsigned> samples[2]{};
};

std::array<TerrainRoute, 2> terrain_routes{{
    {0x880870, acnh::TerrainProperty::diggable, 0x877340, 0x877370, terrain_property_adapter<0>},
    {0x880a10, acnh::TerrainProperty::alternate, 0x8775d4, 0x877614, terrain_property_adapter<1>},
}};
bool trace_shovel_dependencies = false;

void trace_shovel_dependency(uint64_t entry, uint64_t resume) {
    if (trace_shovel_dependencies)
        log("acnh shovel dependency entry=%llx call=%llx",
            static_cast<unsigned long long>(entry), static_cast<unsigned long long>(resume - 4));
}

bool trace_terrain = false;

template<size_t Index>
void terrain_property_adapter(CPU& cpu) {
    auto& route = terrain_routes[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    const acnh::TerrainTable table{cpu.x[0]};
    const uint32_t index = static_cast<uint32_t>(cpu.x[1]);
    const auto apply = [&] {
        if (route.use_cpp) {
            GuestMemory memory;
            cpu.x[0] = acnh::FieldQueries{main_base, memory}.terrain_has(table, index, route.property);
        } else {
            route.original(cpu);
        }
    };

    if (!trace_terrain || caller < route.first_caller || caller > route.last_caller) {
        apply();
        return;
    }

    const uint64_t parent = ld<uint64_t>(cpu.x[29] + 8) - main_base;
    const bool shovel_parent = parent == 0x21f778c || parent == 0x21f7910 || parent == 0x21f7bbc;
    if (route.samples[shovel_parent].fetch_add(1, std::memory_order_relaxed) >= 24) {
        apply();
        return;
    }
    const uint64_t cache = main_base + column_cache_offset;
    const auto before = read_cache(cache);
    std::string reads;
    ColumnSnapshot snapshot{cache, before, GuestMemory{&reads}};
    acnh::FieldQueries{main_base, snapshot}.terrain_has(table, index, route.property);
    apply();
    log("acnh shovel terrain %s entry=%llx caller=%llx parent=%llx base=%llx table=%llx index=%x "
        "out=%llx before=%s after=%s reads=%s",
        route.use_cpp ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
        static_cast<unsigned long long>(caller), static_cast<unsigned long long>(parent),
        static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(table.address), index,
        static_cast<unsigned long long>(cpu.x[0]), cache_hex(before).c_str(),
        cache_hex(read_cache(cache)).c_str(), reads.c_str());
}

constexpr std::array<uint64_t, 2> shovel_terrain_pair_entries{0x880630, 0x881090};
std::array<Fn, 2> original_shovel_terrain_pair{};
bool use_cpp_shovel_terrain_pair = false;
bool trace_shovel_terrain_pair = false;

template<size_t Index>
void shovel_terrain_pair_adapter(CPU& cpu) {
    constexpr std::array<std::array<uint64_t, 4>, 2> resumes{{
        {0x87c0c0, 0x87c100, 0x87c140, 0x87c180},
        {0x87c0f0, 0x87c130, 0x87c170, 0x87c1b0},
    }};
    const auto resume = cpu.x[30] - main_base;
    if (std::find(resumes[Index].begin(), resumes[Index].end(), resume) == resumes[Index].end()) {
        original_shovel_terrain_pair[Index](cpu);
        return;
    }
    constexpr auto property = Index == 0 ? acnh::TerrainProperty::alternate_rejection
                                         : acnh::TerrainProperty::alternate_requirement;
    const acnh::TerrainTable table{cpu.x[0]};
    const auto index = uint32_t(cpu.x[1]);
    const auto cache = main_base + column_cache_offset;
    std::string reads;
    ColumnCache before{};
    if (trace_shovel_terrain_pair) {
        before = read_cache(cache);
        ColumnSnapshot snapshot{cache, before, GuestMemory{&reads}};
        acnh::FieldQueries{main_base, snapshot}.terrain_has(table, index, property);
    }
    if (use_cpp_shovel_terrain_pair) {
        GuestMemory memory;
        cpu.x[0] = acnh::FieldQueries{main_base, memory}.terrain_has(table, index, property);
    } else {
        original_shovel_terrain_pair[Index](cpu);
    }
    if (trace_shovel_terrain_pair)
        log("acnh shovel terrain pair %s entry=%llx call=%llx index=%x out=%x before=%s after=%s base=%llx table=%llx reads=%s",
            use_cpp_shovel_terrain_pair ? "cpp" : "original",
            static_cast<unsigned long long>(shovel_terrain_pair_entries[Index]),
            static_cast<unsigned long long>(resume - 4), index, unsigned(cpu.x[0]),
            cache_hex(before).c_str(), cache_hex(read_cache(cache)).c_str(),
            static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(table.address), reads.c_str());
}


constexpr uint64_t shovel_terrain_caller = 0x21f778c;
Fn original_shovel_terrain = nullptr;
Fn original_terrain_sample = nullptr;
bool use_cpp_shovel_terrain = false;
bool use_cpp_terrain_sample = false;
bool trace_terrain_dispatch = false;
std::atomic<unsigned> terrain_dispatch_samples{0};
std::array<std::atomic<unsigned>, 2> terrain_sample_counts{};

struct TerrainRuntimeServices {
    CPU cpu;
    uint64_t stack;

    explicit TerrainRuntimeServices(const CPU& caller) : cpu(caller), stack(caller.x[31] - 0xa0) {
        cpu.x[31] = stack;
        cpu.x[29] = stack + 0x60;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    bool grid_allows(acnh::TerrainGrid grid, acnh::TerrainRect area) const {
        const uint64_t rect = stack + 0x28;
        const uint64_t point_type = ld<uint64_t>(main_base + 0x4ab2010) + 0x10;
        st<uint64_t>(rect, ld<uint64_t>(main_base + 0x4abc1a0) + 0x10);
        st<uint64_t>(rect + 8, point_type);
        st<int32_t>(rect + 0x10, area.min.x);
        st<int32_t>(rect + 0x14, area.min.z);
        st<uint64_t>(rect + 0x18, point_type);
        st<int32_t>(rect + 0x20, area.max.x);
        st<int32_t>(rect + 0x24, area.max.z);
        CPU nested = cpu;
        nested.x[0] = grid.address;
        nested.x[1] = rect;
        nested.x[2] = 0;
        nested.x[30] = main_base + 0x877258;
        call(nested, main_base + 0x871f80);
        return (nested.x[0] & 1) != 0;
    }

    uint64_t point(acnh::HalfTile tile) const {
        const uint64_t point = stack + 0x18;
        st<uint64_t>(point, ld<uint64_t>(main_base + 0x4ab2010) + 0x10);
        st<int32_t>(point + 8, tile.x);
        st<int32_t>(point + 12, tile.z);
        return point;
    }

    bool quadrant_materials(acnh::TerrainOwner owner, acnh::HalfTile tile,
                            std::array<uint32_t, 4>& materials) const {
        const uint64_t end = stack + 0x60;
        st<uint64_t>(end - 0x10, 0);
        st<uint64_t>(end - 8, 0);
        CPU nested = cpu;
        nested.x[0] = owner.address + 0x28;
        nested.x[1] = point(tile);
        for (size_t i = 0; i < materials.size(); ++i)
            nested.x[i + 2] = end - 4 * (i + 1);
        nested.x[30] = main_base + 0x877330;
        call(nested, main_base + 0x848e68);
        for (size_t i = 0; i < materials.size(); ++i)
            materials[i] = ld<uint32_t>(end - 4 * (i + 1));
        return (nested.x[0] & 1) != 0;
    }

    bool material_allows(acnh::TerrainTable table, uint32_t material, size_t quadrant) const {
        constexpr std::array resumes{0x877340, 0x877350, 0x877360, 0x877370};
        CPU nested = cpu;
        nested.x[0] = table.address;
        nested.x[1] = material;
        nested.x[30] = main_base + resumes[quadrant];
        call(nested, main_base + 0x880870);
        return (nested.x[0] & 1) != 0;
    }

    bool layer_blocks(acnh::HalfTile tile) const {
        CPU nested = cpu;
        nested.x[0] = point(tile);
        nested.x[1] = 0;
        nested.x[30] = main_base + 0x877380;
        call(nested, main_base + 0x1285fe0);
        return (nested.x[0] & 1) != 0;
    }
};

void shovel_terrain_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + shovel_terrain_caller) {
        original_shovel_terrain(cpu);
        return;
    }

    const acnh::HalfTile tile{ld<int32_t>(cpu.x[0] + 8), ld<int32_t>(cpu.x[0] + 12)};
    if (use_cpp_shovel_terrain) {
        GuestMemory memory;
        TerrainRuntimeServices services{cpu};
        cpu.x[0] = acnh::can_dig_terrain(main_base, tile, memory, services);
    } else {
        original_shovel_terrain(cpu);
    }
    if (trace_terrain_dispatch && terrain_dispatch_samples.fetch_add(1, std::memory_order_relaxed) < 8)
        log("acnh shovel terrain dispatch %s pos=%d,%d out=%u",
            use_cpp_shovel_terrain ? "cpp" : "original", tile.x, tile.z, unsigned(cpu.x[0] & 1));
}

bool try_shovel_marker_terrain_sample(CPU& cpu);

void terrain_sample_adapter(CPU& cpu) {
    if (try_shovel_marker_terrain_sample(cpu)) return;
    const uint64_t caller = cpu.x[30] - main_base;
    if (caller != 0x872114 && caller != 0x848ed4) {
        original_terrain_sample(cpu);
        return;
    }
    const uint64_t frame = cpu.x[29];
    const uint64_t helper_caller = caller == 0x872114 ? 0x877258 : 0x877330;
    const bool alternate = caller == 0x872114 && is_shovel_alternate_level_frame(frame);
    if (!alternate && (ld<uint64_t>(frame + 8) != main_base + helper_caller ||
        ld<uint64_t>(ld<uint64_t>(frame) + 8) != main_base + shovel_terrain_caller)) {
        original_terrain_sample(cpu);
        return;
    }

    const acnh::HalfTile tile{ld<int32_t>(cpu.x[1] + 8), ld<int32_t>(cpu.x[1] + 12)};
    const acnh::TerrainSample output{cpu.x[2]};
    const bool native = use_cpp_terrain_sample && (!alternate || use_cpp_shovel_alternate_terrain);
    if (native) {
        GuestMemory memory;
        acnh::sample_terrain({cpu.x[0]}, tile, output, memory);
    } else {
        original_terrain_sample(cpu);
    }
    if (alternate && trace_shovel_alternate_terrain) {
        std::array<uint8_t, 0x34> record;
        for (size_t i = 0; i < record.size(); ++i) record[i] = ld<uint8_t>(output.address + i);
        log("acnh shovel alternate terrain %s entry=862530 call=872110 pos=%d,%d out=%s",
            native ? "cpp" : "original", tile.x, tile.z, cache_hex(record).c_str());
    } else if (!alternate && trace_terrain_dispatch &&
        terrain_sample_counts[caller == 0x872114].fetch_add(1, std::memory_order_relaxed) < 16) {
        std::array<uint8_t, 0x34> record;
        for (size_t i = 0; i < record.size(); ++i) record[i] = ld<uint8_t>(output.address + i);
        log("acnh shovel terrain sample %s caller=%llx pos=%d,%d out=%s",
            use_cpp_terrain_sample ? "cpp" : "original", static_cast<unsigned long long>(caller),
            tile.x, tile.z, cache_hex(record).c_str());
    }
}

Fn original_terrain_level = nullptr;
Fn original_terrain_materials = nullptr;
bool use_cpp_terrain_level = false;
bool use_cpp_terrain_materials = false;
bool trace_terrain_helpers = false;
std::atomic<unsigned> terrain_level_samples{0};
std::atomic<unsigned> terrain_material_samples{0};

Fn original_terrain_field_material = nullptr;
bool use_cpp_terrain_primary = false;
bool use_cpp_terrain_secondary = false;
bool use_cpp_terrain_field_material = false;
bool trace_terrain_fallbacks = false;
bool trace_sampler_terrain = false;
bool use_cpp_sampler_terrain = false;

enum class ShovelHeightScope {
    unrelated, target, classifier, intermediate, stage_base = 5, obstacle, cast,
};

bool use_cpp_shovel_cast_height = false;
bool trace_shovel_cast_height = false;
bool is_cast_sampler_frame(uint64_t frame, bool ground);
bool is_shovel_step_cast_frame(uint64_t frame);

bool is_shovel_height_child_frame(uint64_t frame, uint64_t expected_resume);

constexpr std::array terrain_primary_resumes{0x848fe8u, 0x849044u, 0x84909cu, 0x8490f4u};
constexpr std::array terrain_secondary_resumes{0x849004u, 0x849060u, 0x8490b8u, 0x849110u};

void terrain_field_material_adapter(CPU& cpu) {
    const auto parent = cpu.x[30] == main_base + 0x87aae8
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const bool from_quadrants = std::find(terrain_secondary_resumes.begin(),
                                         terrain_secondary_resumes.end(), parent) != terrain_secondary_resumes.end();
    const auto quadrant_frame = from_quadrants ? ld<uint64_t>(cpu.x[29]) : 0;
    const bool shovel_quadrants = quadrant_frame && ld<uint64_t>(quadrant_frame + 8) == main_base + 0x877330
        && ld<uint64_t>(ld<uint64_t>(quadrant_frame) + 8) == main_base + shovel_terrain_caller;
    const bool shovel_sampler = parent == 0x83ab2c && is_shovel_height_child_frame(ld<uint64_t>(cpu.x[29]), 0x15e02f8);
    const bool cast_sampler = parent == 0x83ab2c && is_cast_sampler_frame(ld<uint64_t>(cpu.x[29]), false);
    if (!shovel_quadrants && !shovel_sampler && !cast_sampler) {
        original_terrain_field_material(cpu);
        return;
    }
    const acnh::HalfTile point{ld<int32_t>(cpu.x[0] + 8), ld<int32_t>(cpu.x[0] + 12)};
    const bool native = use_cpp_terrain_field_material && (!shovel_sampler || use_cpp_sampler_terrain)
        && (!cast_sampler || (use_cpp_sampler_terrain && use_cpp_shovel_cast_height));
    if (native) {
        GuestMemory memory;
        cpu.x[0] = acnh::terrain_has_field_material(main_base, point, memory);
    } else {
        trace_shovel_dependency(0x1285040, 0x87aae8);
        original_terrain_field_material(cpu);
    }
    if (cast_sampler) {
        if (trace_shovel_cast_height)
            log("acnh shovel height chain %s entry=1285040 call=87aae4 scope=%u out=%x",
                native ? "cpp" : "original", unsigned(ShovelHeightScope::cast), unsigned(cpu.x[0]));
    } else if (shovel_sampler ? trace_sampler_terrain : trace_terrain_fallbacks)
        log("acnh shovel %sterrain fallback %s entry=1285040 call=87aae4 pos=%d,%d out=%x",
            shovel_sampler ? "sampler " : "", native ? "cpp" : "original",
            point.x, point.z, static_cast<unsigned>(cpu.x[0]));
}

struct TerrainFallbackServices {
    CPU cpu;
    uint64_t vector;

    TerrainFallbackServices(const CPU& caller, uint64_t address, bool secondary)
        : cpu(caller), vector(address) {
        cpu.x[31] -= secondary ? 0x40 : 0x30;
        cpu.x[29] = cpu.x[31] + 0x10;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    bool has_field_material(acnh::HalfTile point) const {
        const auto query = cpu.x[31];
        st<uint64_t>(query, ld<uint64_t>(main_base + 0x4ab2010) + 0x10);
        st<int32_t>(query + 8, point.x);
        st<int32_t>(query + 12, point.z);
        CPU nested = cpu;
        nested.x[0] = query;
        nested.x[30] = main_base + 0x87aae8;
        call(nested, main_base + 0x1285040);
        return (nested.x[0] & 1) != 0;
    }

    uint32_t stage_hash(uint64_t object, uint32_t index, acnh::TerrainFallback fallback) const {
        const bool secondary = fallback == acnh::TerrainFallback::secondary;
        const auto target = ld<uint64_t>(ld<uint64_t>(object) + 0x18);
        CPU nested = cpu;
        nested.x[0] = object;
        nested.x[1] = index;
        const auto resume = secondary ? 0x87ab3c : 0x87a9d8;
        nested.x[30] = main_base + resume;
        trace_shovel_dependency(target - main_base, resume);
        call(nested, target);
        nested.x[0] = ld<uint64_t>(nested.x[0]);
        nested.x[1] = 0;
        nested.x[30] = main_base + (secondary ? 0x87ab48 : 0x87a9e4);
        call(nested, main_base + 0x5bd550);
        return static_cast<uint32_t>(nested.x[0]);
    }

    uint32_t resolve_kind(acnh::TerrainOwner owner, acnh::TerrainWorldPoint& point,
                          acnh::TerrainFallback fallback) const {
        const bool secondary = fallback == acnh::TerrainFallback::secondary;
        const uint64_t target = secondary ? 0x83b18c : 0x83b9a4;
        const uint64_t resume = secondary ? 0x87abcc : 0x87aa68;
        CPU nested = cpu;
        nested.x[0] = owner.address + 0x28;
        nested.x[1] = vector;
        nested.x[30] = main_base + resume;
        trace_shovel_dependency(target, resume);
        call(nested, main_base + target);
        point = {ld<float>(vector), ld<float>(vector + 8)};
        return static_cast<uint32_t>(nested.x[0]);
    }
};

uint32_t route_terrain_fallback(CPU& cpu, acnh::TerrainFallback fallback, bool native) {
    const auto point_address = cpu.x[0];
    if (native) {
        const bool secondary = fallback == acnh::TerrainFallback::secondary;
        acnh::TerrainWorldPoint point{ld<float>(point_address), ld<float>(point_address + 8)};
        GuestMemory memory;
        TerrainFallbackServices services{cpu, point_address, secondary};
        return acnh::terrain_fallback_kind(main_base, point, fallback, memory, services);
    }
    const uint64_t entry = fallback == acnh::TerrainFallback::secondary ? 0x87aa70 : 0x87a980;
    trace_shovel_dependency(entry, cpu.x[30] - main_base);
    call(cpu, main_base + entry);
    return static_cast<uint32_t>(cpu.x[0]);
}

Fn original_shovel_target = nullptr;
Fn original_shovel_terrain_setup = nullptr;
Fn original_shovel_layer_one = nullptr;
bool use_cpp_shovel_target = false;
bool use_cpp_shovel_terrain_setup = false;
bool use_cpp_shovel_layer_one = false;
bool trace_shovel_target = false;
constexpr uint64_t shovel_target_resume = 0x21f70d8;
constexpr uint64_t shovel_terrain_setup_resume = 0x21f7500;

constexpr std::array shovel_height_resumes{0x15f392cu, 0x15f3e74u, 0x15f3e8cu};
constexpr std::array<uint64_t, 5> shovel_height_entries{0x15e026c, 0x83b024, 0x83a950, 0x241e98c, 0x60cba4};
constexpr std::array shovel_height_switches{
    "NX_ACNH_SHOVEL_HEIGHT", "NX_ACNH_SHOVEL_GROUND_HEIGHT", "NX_ACNH_SHOVEL_OVERLAY_HEIGHT",
    "NX_ACNH_SHOVEL_HEIGHT_PARAMETERS", "NX_ACNH_SHOVEL_HEIGHT_FLOAT"};
std::array<Fn, 5> original_shovel_height{};
std::array<bool, 5> use_cpp_shovel_height{};
bool trace_shovel_height = false;

bool is_shovel_height_call(uint64_t resume) {
    return std::find(shovel_height_resumes.begin(), shovel_height_resumes.end(),
                     resume - main_base) != shovel_height_resumes.end();
}

bool is_shovel_target_frame(uint64_t frame) {
    return frame && ld<uint64_t>(frame + 8) == main_base + shovel_target_resume;
}

constexpr std::array shovel_approach_resumes{0x15f39c8u, 0x15f3a24u, 0x15f3a78u};
constexpr std::array shovel_nearby_resumes{0x15f3b84u, 0x15f3bd8u, 0x15f3c2cu, 0x15f3c80u, 0x15f3cd4u,
                                          0x15f3d28u, 0x15f3d88u, 0x15f3ddcu, 0x15f3e3cu};
constexpr uint64_t shovel_radius_resume = 0x15f3b40;


bool is_shovel_point_probe(uint64_t resume) {
    const auto contains = [resume](const auto& resumes) {
        return std::find(resumes.begin(), resumes.end(), resume) != resumes.end();
    };
    return contains(shovel_approach_resumes) || contains(shovel_nearby_resumes) || resume == shovel_radius_resume;
}

bool is_shovel_point_classifier_frame(uint64_t frame) {
    return frame && is_shovel_point_probe(ld<uint64_t>(frame + 8) - main_base)
        && is_shovel_target_frame(ld<uint64_t>(frame));
}

bool use_cpp_shovel_height_chain = false;
bool trace_shovel_height_chain = false;

ShovelHeightScope shovel_surface_scope(uint64_t resume, uint64_t frame);
ShovelHeightScope shovel_sampler_scope(bool ground, uint64_t resume, uint64_t frame);

ShovelHeightScope shovel_surface_frame(uint64_t frame) {
    return frame ? shovel_surface_scope(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame))
                 : ShovelHeightScope::unrelated;
}

bool use_cpp_height_scope(ShovelHeightScope scope) {
    return scope == ShovelHeightScope::cast ? use_cpp_shovel_cast_height : use_cpp_shovel_height_chain;
}

bool trace_height_scope(ShovelHeightScope scope) {
    return scope == ShovelHeightScope::cast ? trace_shovel_cast_height : trace_shovel_height_chain;
}

bool is_new_height_scope(ShovelHeightScope scope) {
    return scope != ShovelHeightScope::unrelated && scope != ShovelHeightScope::target;
}

bool use_cpp_shovel_point_height = false;
bool trace_shovel_point_height = false;

bool is_shovel_height_child_frame(uint64_t frame, uint64_t expected_resume) {
    if (!frame || ld<uint64_t>(frame + 8) != main_base + expected_resume) return false;
    const auto height_frame = ld<uint64_t>(frame);
    return height_frame && is_shovel_height_call(ld<uint64_t>(height_frame + 8))
        && is_shovel_target_frame(ld<uint64_t>(height_frame));
}

Fn original_shovel_grid_height = nullptr;
bool use_cpp_shovel_grid_height = false;
bool trace_shovel_grid_height = false;

ShovelHeightScope shovel_grid_scope(const CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    const bool ground = resume == 0x83b0cc;
    const auto frame = cpu.x[29];
    if ((!ground && resume != 0x83aa78 && resume != 0x83aad8) || !frame)
        return ShovelHeightScope::unrelated;
    return shovel_sampler_scope(ground, ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame));
}

void shovel_grid_height_adapter(CPU& cpu) {
    const auto scope = shovel_grid_scope(cpu);
    if (scope == ShovelHeightScope::unrelated) {
        original_shovel_grid_height(cpu);
        return;
    }
    const auto resume = cpu.x[30] - main_base;
    const acnh::GridHeightQuery query{{cpu.x[0]}, cpu.x[1], cpu.x[2], cpu.x[4], (cpu.x[3] & 1) != 0};
    const bool extended = is_new_height_scope(scope);
    const bool native = use_cpp_shovel_grid_height && (!extended || use_cpp_height_scope(scope));
    const bool trace = extended ? trace_height_scope(scope) : trace_shovel_grid_height;
    if (!trace) {
        if (native) {
            GuestMemory memory;
            cpu.v[0].f32[0] = acnh::query_grid_height(query, memory);
        } else {
            original_shovel_grid_height(cpu);
        }
        return;
    }

    const auto before = ld<uint32_t>(query.material);
    const auto override_before = ld<uint8_t>(query.override_present);
    CPU reference = cpu;
    original_shovel_grid_height(reference);
    const auto expected_height = std::bit_cast<uint32_t>(reference.v[0].f32[0]);
    const auto expected_kind = ld<uint32_t>(query.material);
    const auto expected_override = ld<uint8_t>(query.override_present);
    const char* comparison = "stock";
    if (native) {
        st<uint32_t>(query.material, before);
        st<uint8_t>(query.override_present, override_before);
        GuestMemory memory;
        cpu.v[0].f32[0] = acnh::query_grid_height(query, memory);
        comparison = std::bit_cast<uint32_t>(cpu.v[0].f32[0]) == expected_height
                  && ld<uint32_t>(query.material) == expected_kind
                  && ld<uint8_t>(query.override_present) == expected_override ? "match" : "mismatch";
    } else {
        cpu = reference;
    }
    if (extended) {
        log("acnh shovel height chain %s entry=862700 same_input=%s call=%llx scope=%u ground=%u kind=%x override=%u out=%x",
            native ? "cpp" : "original", comparison, static_cast<unsigned long long>(resume - 4), unsigned(scope),
            unsigned(query.ground_only), ld<uint32_t>(query.material), unsigned(ld<uint8_t>(query.override_present)),
            std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
    } else {
        log("acnh shovel height %s entry=862700 same_input=%s point=%x,%x,%x call=%llx ground=%u before=%x kind=%x override=%u out=%x",
            native ? "cpp" : "original", comparison,
            ld<uint32_t>(query.point), ld<uint32_t>(query.point + 4), ld<uint32_t>(query.point + 8),
            static_cast<unsigned long long>(resume - 4), unsigned(query.ground_only), before,
            ld<uint32_t>(query.material), unsigned(ld<uint8_t>(query.override_present)),
            std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
    }
}

struct HeightGuestFrame {
    CPU cpu;

    HeightGuestFrame(const CPU& caller, uint64_t frame_size, uint64_t frame_offset) : cpu(caller) {
        cpu.x[31] -= frame_size;
        cpu.x[29] = cpu.x[31] + frame_offset;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    void invoke(CPU& nested, uint64_t target, uint64_t resume) const {
        nested.x[30] = main_base + resume;
        call(nested, target);
    }
    CPU invoke_child(uint64_t entry, uint64_t resume, std::initializer_list<uint64_t> args, float input = 0.f) const {
        CPU nested = cpu;
        std::copy(args.begin(), args.end(), nested.x);
        nested.v[0].f32[0] = input;
        invoke(nested, entry, resume);
        return nested;
    }
};

struct ShovelHeightServices : HeightGuestFrame {
    explicit ShovelHeightServices(const CPU& caller) : HeightGuestFrame(caller, 0x70, 0x30) {}

    acnh::HeightSample sample(uint64_t owner, uint64_t point, uint64_t entry, uint64_t resume,
                             uint64_t material) const {
        st<uint32_t>(material, 0);
        CPU nested = cpu;
        nested.x[0] = owner;
        nested.x[1] = point;
        nested.x[2] = material;
        invoke(nested, main_base + entry, resume);
        return {nested.v[0].f32[0], ld<uint32_t>(material)};
    }

    acnh::HeightSample sample_base(uint64_t owner, uint64_t point) const {
        return sample(owner, point, 0x83b024, 0x15e02e0, cpu.x[31] + 8);
    }
    acnh::HeightSample sample_overlay(uint64_t owner, uint64_t point) const {
        return sample(owner, point, 0x83a950, 0x15e02f8, cpu.x[31] + 4);
    }

    float float_value(uint64_t object, uint64_t resume) const {
        CPU nested = cpu;
        nested.x[0] = object;
        invoke(nested, ld<uint64_t>(ld<uint64_t>(object) + 0xf0), resume);
        return ld<float>(nested.x[0]);
    }

    uint64_t parameters(uint64_t resume) const {
        CPU nested = cpu;
        nested.x[0] = 1;
        invoke(nested, main_base + 0x241e98c, resume);
        return nested.x[0];
    }

    float height_limit() const { return float_value(parameters(0x15e0304) + 0x330, 0x15e0318); }
    float actor_height(uint64_t object) const { return float_value(object, 0x15e0674); }
    float world_height() const { return float_value(parameters(0x15e0680) + 0x1560, 0x15e0698); }

    bool material_property(uint32_t kind, acnh::HeightPropertyUse) const {
        GuestMemory memory;
        return acnh::shovel_height_material_property(main_base, kind, memory);
    }
};

struct ShovelSamplerServices : HeightGuestFrame {
    bool ground;

    ShovelSamplerServices(const CPU& caller, bool is_ground)
        : HeightGuestFrame(caller, is_ground ? 0x40 : 0x70, 0x20), ground(is_ground) {}

    acnh::HeightSample ground_grid(uint64_t grid, uint64_t point, uint32_t kind) const {
        CPU nested = cpu;
        const auto material = cpu.x[29] - 4;
        st<uint32_t>(material, kind);
        st<uint8_t>(material - 4, 0);
        nested.x[0] = grid;
        nested.x[1] = point;
        nested.x[2] = material;
        nested.x[3] = 1;
        nested.x[4] = material - 4;
        if (!use_cpp_shovel_grid_height) trace_shovel_dependency(0x862700, 0x83b0cc);
        invoke(nested, main_base + 0x862700, 0x83b0cc);
        return {nested.v[0].f32[0], ld<uint32_t>(material)};
    }

    void lock_overlay(uint64_t lock) const {
        CPU nested = cpu;
        nested.x[0] = lock;
        invoke(nested, main_base + 0x3f03880, 0x83aa04);
    }
    void unlock_overlay(uint64_t lock) const {
        CPU nested = cpu;
        nested.x[0] = lock;
        invoke(nested, main_base + 0x3f038c0, 0x83ab00);
    }

    acnh::OverlayHeightSample overlay_grid(uint64_t object, uint64_t point, uint32_t& kind, bool capture) const {
        CPU nested = cpu;
        const auto material = cpu.x[29] - 4;
        st<uint32_t>(material, kind);
        st<uint8_t>(material - 4, 0);
        nested.x[0] = object;
        nested.x[1] = point;
        nested.x[2] = material;
        nested.x[3] = 0;
        nested.x[4] = material - 4;
        const auto target = ld<uint64_t>(ld<uint64_t>(object) + 0x30);
        const auto resume = capture ? 0x83aa78 : 0x83aad8;
        if (target != main_base + 0x862700 || !use_cpp_shovel_grid_height)
            trace_shovel_dependency(target - main_base, resume);
        invoke(nested, target, resume);
        kind = ld<uint32_t>(material);
        return {nested.v[0].f32[0], ld<uint8_t>(material - 4) != 0};
    }

    uint32_t adjustment(uint64_t point, acnh::TerrainFallback route) const {
        const bool secondary = route == acnh::TerrainFallback::secondary;
        CPU nested = cpu;
        nested.x[0] = point;
        const uint64_t entry = secondary ? 0x87aa70 : 0x87a980;
        const uint64_t resume = ground ? 0x83b0e4 : (secondary ? 0x83ab2c : 0x83ab10);
        nested.x[30] = main_base + resume;
        if (is_cast_sampler_frame(cpu.x[29], ground)) {
            call(nested, main_base + entry);
            return uint32_t(nested.x[0]);
        }
        const bool native = use_cpp_sampler_terrain && (secondary ? use_cpp_terrain_secondary : use_cpp_terrain_primary);
        const auto before_x = trace_sampler_terrain ? ld<uint32_t>(point) : 0;
        const auto before_z = trace_sampler_terrain ? ld<uint32_t>(point + 8) : 0;
        const auto kind = route_terrain_fallback(nested, route, native);
        if (trace_sampler_terrain) {
            const auto after_x = ld<uint32_t>(point);
            const auto after_z = ld<uint32_t>(point + 8);
            log("acnh shovel sampler terrain fallback %s entry=%llx point=%x,%x after=%x,%x call=%llx moved=%u out=%x",
                native ? "cpp" : "original", static_cast<unsigned long long>(entry),
                before_x, before_z, after_x, after_z, static_cast<unsigned long long>(resume - 4),
                unsigned(before_x != after_x || before_z != after_z), kind);
        }
        return kind;
    }
};

Fn original_shovel_height_parameter_type = nullptr;
bool use_cpp_shovel_height_parameter_type = false;
bool trace_shovel_height_parameter_type = false;

bool is_shovel_height_parameter_resume(uint64_t resume) {
    return resume == 0x15e0304 || resume == 0x15e0680;
}

struct ShovelHeightParameterServices : HeightGuestFrame {
    explicit ShovelHeightParameterServices(const CPU& caller) : HeightGuestFrame(caller, 0x30, 0x10) {}

    uint8_t guard_byte(uint64_t address) const { return nx::ld_acq<uint8_t>(address); }
    bool acquire_guard(uint64_t address, acnh::HeightParameterGuard guard) const {
        CPU nested = cpu;
        nested.x[0] = address;
        invoke(nested, main_base + 0x3f030d0,
               guard == acnh::HeightParameterGuard::type ? 0x241ea88 : 0x241ea24);
        return static_cast<uint32_t>(nested.x[0]) != 0;
    }

    void release_guard(uint64_t address, acnh::HeightParameterGuard guard) const {
        CPU nested = cpu;
        nested.x[0] = address;
        invoke(nested, main_base + 0x3f030e0,
               guard == acnh::HeightParameterGuard::type ? 0x241eab0 : 0x241ea78);
    }

    bool parameter_type_matches(uint64_t object, uint64_t type) const {
        CPU nested = cpu;
        nested.x[0] = object;
        nested.x[1] = type;
        const auto target = ld<uint64_t>(ld<uint64_t>(object) + 0x10);
        if (target != main_base + 0x22688e4 || !use_cpp_shovel_height_parameter_type)
            trace_shovel_dependency(target - main_base, 0x241e9e8);
        invoke(nested, target, 0x241e9e8);
        return (nested.x[0] & 1) != 0;
    }

    void construct_parameters(uint64_t object) const {
        CPU nested = cpu;
        nested.x[0] = object;
        nested.x[1] = cpu.x[31];
        st<uint64_t>(nested.x[1], ld<uint64_t>(main_base + 0x4ab2020) + 0x10);
        st<uint64_t>(nested.x[1] + 8, main_base + 0x3f6f9b4);
        invoke(nested, main_base + 0x225e000, 0x241ea54);
    }

    void register_parameter_destructor(uint64_t object) const {
        CPU nested = cpu;
        nested.x[0] = ld<uint64_t>(main_base + 0x4ac3100);
        nested.x[1] = object;
        nested.x[2] = main_base + 0x4736160;
        invoke(nested, main_base + 0x1d0, 0x241ea6c);
    }
};

struct ShovelHeightParameterTypeServices : HeightGuestFrame {
    const CPU& caller;

    explicit ShovelHeightParameterTypeServices(const CPU& context)
        : HeightGuestFrame(context, 0x20, 0x10), caller(context) {}

    uint8_t guard_byte(uint64_t address) const { return nx::ld_acq<uint8_t>(address); }

    bool acquire_guard(uint64_t address, acnh::HeightParameterType kind) const {
        CPU nested = cpu;
        nested.x[0] = address;
        const auto resume = kind == acnh::HeightParameterType::primary ? 0x2268960 : 0x22689ac;
        invoke(nested, main_base + 0x3f030d0, resume);
        return static_cast<uint32_t>(nested.x[0]) != 0;
    }

    void release_guard(uint64_t address, acnh::HeightParameterType kind) const {
        CPU nested = cpu;
        nested.x[0] = address;
        const auto resume = kind == acnh::HeightParameterType::primary ? 0x226898c : 0x22689d8;
        invoke(nested, main_base + 0x3f030e0, resume);
    }

    uint32_t matches_base_type(uint64_t requested) const {
        CPU nested = caller;
        nested.x[0] = requested;
        trace_shovel_dependency(0x6521f0, 0x2268940);
        call(nested, main_base + 0x6521f0);
        return static_cast<uint32_t>(nested.x[0]);
    }
};

void shovel_height_parameter_type_adapter(CPU& cpu) {
    const auto parent = cpu.x[30] == main_base + 0x241e9e8 && cpu.x[29]
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const auto scope = is_shovel_height_parameter_resume(parent) ? shovel_surface_frame(ld<uint64_t>(cpu.x[29]))
                                                                 : ShovelHeightScope::unrelated;
    if (scope == ShovelHeightScope::unrelated) {
        original_shovel_height_parameter_type(cpu);
        return;
    }
    const auto requested = cpu.x[1];
    unsigned type = 0;
    const bool extended = is_new_height_scope(scope);
    const bool trace = extended ? trace_height_scope(scope) : trace_shovel_height_parameter_type;
    const bool native = use_cpp_shovel_height_parameter_type && (!extended || use_cpp_height_scope(scope));
    if (trace) {
        if (requested == ld<uint64_t>(main_base + 0x4ac3108)) type = 1;
        else if (requested == ld<uint64_t>(main_base + 0x4ab35f0)) type = 2;
    }
    if (native) {
        GuestMemory memory;
        ShovelHeightParameterTypeServices services{cpu};
        cpu.x[0] = acnh::height_parameter_type_matches(main_base, requested, memory, services);
    } else {
        original_shovel_height_parameter_type(cpu);
    }
    if (trace) {
        const auto site = static_cast<unsigned long long>(parent - 4);
        if (extended) {
            log("acnh shovel height chain %s entry=22688e4 call=241e9e4 scope=%u from=%llx type=%u out=%x",
                native ? "cpp" : "original", unsigned(scope), site, type, unsigned(cpu.x[0]));
        } else {
            log("acnh shovel height type %s entry=22688e4 call=241e9e4 from=%llx type=%u out=%x",
                native ? "cpp" : "original", site, type, unsigned(cpu.x[0]));
        }
    }
}

template<size_t Index>
void shovel_height_adapter(CPU& cpu) {
    const uint64_t resume = cpu.x[30] - main_base;
    auto scope = ShovelHeightScope::unrelated;
    if constexpr (Index == 0) scope = shovel_surface_scope(resume, cpu.x[29]);
    if constexpr (Index == 1 || Index == 2) scope = shovel_sampler_scope(Index == 1, resume, cpu.x[29]);
    if constexpr (Index == 3) {
        if (is_shovel_height_parameter_resume(resume)) scope = shovel_surface_frame(cpu.x[29]);
    }
    if constexpr (Index == 4) {
        if (resume == 0x15e0318 || resume == 0x15e0674 || resume == 0x15e0698) scope = shovel_surface_frame(cpu.x[29]);
    }
    if (scope == ShovelHeightScope::unrelated) {
        original_shovel_height[Index](cpu);
        return;
    }

    const auto input = cpu.x[0];
    const auto point = Index == 0 ? cpu.x[0] : cpu.x[1];
    const auto material = Index == 0 ? cpu.x[1] : cpu.x[2];
    const auto flags = static_cast<uint32_t>(cpu.x[2]);
    const auto bias = cpu.v[0].f32[0];
    const bool from_classifier = Index == 0 && scope == ShovelHeightScope::classifier;
    const bool extended = is_new_height_scope(scope) && !from_classifier;
    const bool native = use_cpp_shovel_height[Index] && (!from_classifier || use_cpp_shovel_point_height)
        && (!extended || use_cpp_height_scope(scope));
    if (!native) {
        original_shovel_height[Index](cpu);
    } else if constexpr (Index == 0) {
        GuestMemory memory;
        ShovelHeightServices services{cpu};
        cpu.v[0].f32[0] = acnh::shovel_height(main_base, {point, material, flags, bias}, memory, services);
    } else if constexpr (Index == 1 || Index == 2) {
        GuestMemory memory;
        ShovelSamplerServices services{cpu, Index == 1};
        cpu.v[0].f32[0] = Index == 1 ? acnh::shovel_ground_height(main_base, input, point, material, memory, services)
                                     : acnh::shovel_overlay_height(main_base, input, point, material, memory, services);
    } else if constexpr (Index == 3) {
        GuestMemory memory;
        ShovelHeightParameterServices services{cpu};
        cpu.x[0] = acnh::height_parameters(main_base, static_cast<uint32_t>(input), memory, services);
    } else {
        cpu.x[0] = acnh::height_float_address(input);
    }

    const char* route = native ? "cpp" : "original";
    if (from_classifier) {
        if (trace_shovel_point_height)
            log("acnh shovel point height %s entry=15e026c call=15f4010 flags=%x bias=%x kind=%x out=%x",
                route, flags, std::bit_cast<uint32_t>(bias), material ? ld<uint32_t>(material) : 0,
                std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
        return;
    }
    if (extended ? !trace_height_scope(scope) : !trace_shovel_height) return;
    const auto entry = static_cast<unsigned long long>(shovel_height_entries[Index]);
    const auto call_site = static_cast<unsigned long long>(resume - 4);
    if (extended) {
        if constexpr (Index < 3) {
            log("acnh shovel height chain %s entry=%llx call=%llx scope=%u kind=%x out=%x",
                route, entry, call_site, unsigned(scope), material ? ld<uint32_t>(material) : 0,
                std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
        } else if constexpr (Index == 3) {
            const auto index = static_cast<uint32_t>(input);
            const auto table = ld<uint64_t>(ld<uint64_t>(main_base + 0x4ab2f58));
            const auto object = table && index <= 0xeb ? ld<uint64_t>(table + 16ull * index) : 0;
            const unsigned kind = cpu.x[0] == main_base + 0x53f1368 ? 0u : (cpu.x[0] == object ? 1u : 2u);
            log("acnh shovel height chain %s entry=%llx call=%llx scope=%u index=%x out=%x",
                route, entry, call_site, unsigned(scope), index, kind);
        } else {
            log("acnh shovel height chain %s entry=%llx call=%llx scope=%u value=%x out=%llx",
                route, entry, call_site, unsigned(scope), ld<uint32_t>(cpu.x[0]),
                static_cast<unsigned long long>(cpu.x[0] - input));
        }
        return;
    }
    if constexpr (Index < 3) {
        log("acnh shovel height %s entry=%llx call=%llx point=%x,%x,%x kind=%x out=%x",
            route, entry, call_site, ld<uint32_t>(point), ld<uint32_t>(point + 4), ld<uint32_t>(point + 8),
            material ? ld<uint32_t>(material) : 0, std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
    } else if constexpr (Index == 3) {
        const auto index = static_cast<uint32_t>(input);
        const auto table = ld<uint64_t>(ld<uint64_t>(main_base + 0x4ab2f58));
        const auto object = table && index <= 0xeb ? ld<uint64_t>(table + 16ull * index) : 0;
        const unsigned kind = cpu.x[0] == main_base + 0x53f1368 ? 0u : (cpu.x[0] == object ? 1u : 2u);
        log("acnh shovel height %s entry=%llx call=%llx index=%x out=%x", route, entry, call_site, index, kind);
    } else {
        log("acnh shovel height %s entry=%llx call=%llx value=%x out=%llx", route, entry, call_site,
            ld<uint32_t>(cpu.x[0]), static_cast<unsigned long long>(cpu.x[0] - input));
    }
}


Fn original_shovel_obstacle = nullptr;
bool use_cpp_shovel_obstacle = false;

struct ShovelObstacleServices : HeightGuestFrame {
    explicit ShovelObstacleServices(const CPU& caller) : HeightGuestFrame(caller, 0x50, 0x20) {
        st<uint32_t>(cpu.x[31] + 0xc, 0);
    }

    uint32_t material(uint64_t owner, uint64_t point, unsigned index) const {
        constexpr std::array resumes{0x15ea86cu, 0x15ea8d0u, 0x15ea954u, 0x15ea9c8u, 0x15eaa40u,
                                     0x15eaab4u, 0x15eab2cu, 0x15eaba8u, 0x15eac24u};
        const auto out = cpu.x[31] + 0xc;
        CPU nested = cpu;
        nested.x[0] = owner;
        nested.x[1] = point;
        nested.x[2] = out;
        invoke(nested, main_base + 0x83a950, resumes[index]);
        return ld<uint32_t>(out);
    }

    uint32_t material(uint64_t owner, acnh::ShovelTargetPoint point, unsigned index) const {
        const auto address = cpu.x[31];
        st<float>(address, point.x);
        st<float>(address + 4, point.y);
        st<float>(address + 8, point.z);
        return material(owner, address, index);
    }
};

void shovel_obstacle_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x15f4028 || !is_shovel_point_classifier_frame(cpu.x[29])) {
        original_shovel_obstacle(cpu);
        return;
    }
    const acnh::ShovelObstacleQuery query{cpu.x[0], cpu.v[0].f32[0]};
    const bool native = use_cpp_shovel_point_height && use_cpp_shovel_obstacle;
    if (native) {
        GuestMemory memory;
        ShovelObstacleServices services{cpu};
        cpu.x[0] = acnh::shovel_point_obstructed(main_base, query, memory, services);
    } else {
        original_shovel_obstacle(cpu);
    }
    if (trace_shovel_point_height)
        log("acnh shovel point height %s entry=15ea82c call=15f4024 radius=%x out=%x",
            native ? "cpp" : "original", std::bit_cast<uint32_t>(query.radius), unsigned(cpu.x[0]));
}

constexpr std::array<uint64_t, 3> shovel_stage_context_entries{0x15e80f0, 0x15e7980, 0x195190c};
std::array<Fn, 3> original_shovel_stage_context{};
bool use_cpp_shovel_stage_context = false;
bool trace_shovel_stage_context = false;

bool is_shovel_stage_height_frame(uint64_t frame) {
    return frame && ld<uint64_t>(frame + 8) == main_base + 0x15f3ffc
        && is_shovel_point_classifier_frame(ld<uint64_t>(frame));
}

bool is_shovel_stage_item_resume(uint64_t resume) {
    return resume == 0x15e0798 || resume == 0x15e0c54;
}

bool is_shovel_stage_record_call(uint64_t resume, uint64_t frame) {
    if (resume == 0x15f42f4 || resume == 0x15f4630)
        return is_shovel_point_classifier_frame(frame);
    return resume == 0x15e7bf4 && frame
        && is_shovel_stage_item_resume(ld<uint64_t>(frame + 8) - main_base)
        && is_shovel_stage_height_frame(ld<uint64_t>(frame));
}

Fn original_shovel_provider_record = nullptr;
bool use_cpp_shovel_provider_record = false;
bool trace_shovel_record_leaves = false;

void shovel_provider_record_adapter(CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    if (!is_shovel_stage_record_call(resume, cpu.x[29])) {
        original_shovel_provider_record(cpu);
        return;
    }
    const acnh::ShovelProviderView provider{cpu.x[0]};
    const acnh::HalfTileRef point{cpu.x[1]}, root{cpu.x[2]};
    if (use_cpp_shovel_provider_record) {
        GuestMemory memory;
        cpu.x[0] = acnh::shovel_provider_record(main_base, provider, point, root, memory).address;
    } else {
        original_shovel_provider_record(cpu);
    }
    if (trace_shovel_record_leaves)
        log("acnh shovel record leaf %s entry=11eb21c call=19519ec from=%llx root=%d,%d out=%llx",
            use_cpp_shovel_provider_record ? "cpp" : "original", static_cast<unsigned long long>(resume - 4),
            root.address ? ld<int32_t>(root.address + 8) : 0, root.address ? ld<int32_t>(root.address + 12) : 0,
            cpu.x[0] ? ld<unsigned long long>(cpu.x[0]) : 0);
}

struct ShovelStageContextServices : HeightGuestFrame {
    const CPU& caller;

    ShovelStageContextServices(const CPU& context, size_t kind)
        : HeightGuestFrame(context, kind == 0 ? 0x50 : kind == 1 ? 0x70 : 0x40, kind == 1 ? 0x20 : 0x10),
          caller(context) {}

    uint32_t stage_hash(uint64_t owner, uint32_t index, acnh::ShovelStageSelection phase) const {
        constexpr std::array resumes{0x15e815cu, 0x15e79f4u, 0x15e7adcu};
        const auto resume = resumes[static_cast<size_t>(phase)];
        const auto getter = ld<uint64_t>(ld<uint64_t>(owner) + 0x18);
        const auto name = invoke_child(getter, resume, {owner, index}).x[0];
        return uint32_t(invoke_child(main_base + 0x5bd550, resume + 0xc, {ld<uint64_t>(name), 0}).x[0]);
    }

    CPU tail(uint64_t entry, uint64_t arg0, uint64_t arg1, uint64_t arg2) const {
        CPU nested = caller;
        nested.x[0] = arg0;
        nested.x[1] = arg1;
        nested.x[2] = arg2;
        call(nested, main_base + entry);
        return nested;
    }

    float sample_current(acnh::TerrainOwner owner, acnh::ShovelStageBaseHeightQuery query) const {
        return tail(0x83a950, owner.address + 0x28, query.point, query.material).v[0].f32[0];
    }

    float sample_special(acnh::TerrainOwner owner, acnh::ShovelStageBaseHeightQuery query) const {
        return invoke_child(main_base + 0x84ba6c, 0x15e82f0, {owner.address + 0x28, query.point, query.material}).v[0].f32[0];
    }

    float sample_other(acnh::ShovelStageBaseHeightQuery query) const {
        return invoke_child(main_base + 0x15e87e0, 0x15e8320, {query.point, query.material, query.context.address}).v[0].f32[0];
    }

    acnh::FieldItem lookup(acnh::ShovelStageFieldQuery query, acnh::ShovelStageFieldKey stage) const {
        const auto descriptor = cpu.x[31];
        const uint64_t vtable = stage.table ? ld<uint64_t>(main_base + 0x4abfe68) + 0x10 : main_base + 0x48ffa38;
        st<uint64_t>(descriptor, vtable);
        st<uint32_t>(descriptor + 8, stage.table);
        st<uint32_t>(descriptor + 12, stage.key.hash);
        const auto flags = invoke_child(main_base + 0x16baf44, 0x15e7bd8, {stage.key.hash}).x[0];
        st<uint8_t>(descriptor + 16, uint8_t(flags & 1));
        return {invoke_child(main_base + 0x195190c, 0x15e7bf4,
                             {descriptor, query.layer, query.point.address, query.root.address}).x[0]};
    }

    bool accept_stage(uint64_t stage, uint32_t layer, acnh::HalfTileRef point) const {
        return invoke_child(main_base + 0x194f798, 0x1951938, {stage, layer, point.address}).x[0] & 1;
    }

    acnh::FieldItem cell_at(uint64_t provider, acnh::HalfTileRef point, acnh::HalfTileRef root) const {
        return {tail(0x11eb21c, provider, point.address, root.address).x[0]};
    }
};

template<size_t Index>
void shovel_stage_context_adapter(CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    bool scoped;
    if constexpr (Index == 0) scoped = resume == 0x15e0724 && is_shovel_stage_height_frame(cpu.x[29]);
    if constexpr (Index == 1) scoped = is_shovel_stage_item_resume(resume) && is_shovel_stage_height_frame(cpu.x[29]);
    if constexpr (Index == 2) scoped = is_shovel_stage_record_call(resume, cpu.x[29]);
    if (!scoped) {
        original_shovel_stage_context[Index](cpu);
        return;
    }

    GuestMemory memory;
    const char* route = use_cpp_shovel_stage_context ? "cpp" : "original";
    const auto site = static_cast<unsigned long long>(resume - 4);
    if constexpr (Index == 0) {
        const acnh::ShovelStageBaseHeightQuery query{cpu.x[0], cpu.x[1], {cpu.x[2]}};
        if (use_cpp_shovel_stage_context) {
            ShovelStageContextServices services{cpu, Index};
            cpu.v[0].f32[0] = acnh::ShovelStageContextQueries{main_base, memory}.base_height(query, services);
        } else {
            original_shovel_stage_context[Index](cpu);
        }
        if (trace_shovel_stage_context)
            log("acnh shovel stage context %s entry=15e80f0 call=%llx stage=%x flag=%x material=%x out=%x",
                route, site, unsigned(ld<uint16_t>(query.context.address)), unsigned(ld<uint8_t>(query.context.address + 2)),
                query.material ? ld<uint32_t>(query.material) : 0, std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
    } else if constexpr (Index == 1) {
        const acnh::ShovelStageFieldQuery query{{cpu.x[0]}, {cpu.x[1]}, uint32_t(cpu.x[2]), {cpu.x[3]}};
        if (use_cpp_shovel_stage_context) {
            ShovelStageContextServices services{cpu, Index};
            cpu.x[0] = acnh::ShovelStageContextQueries{main_base, memory}.field_item(query, services).address;
        } else {
            original_shovel_stage_context[Index](cpu);
        }
        if (trace_shovel_stage_context)
            log("acnh shovel stage context %s entry=15e7980 call=%llx stage=%x layer=%x root=%d,%d out=%llx",
                route, site, unsigned(ld<uint16_t>(query.context.address)), query.layer,
                query.root.address ? ld<int32_t>(query.root.address + 8) : 0,
                query.root.address ? ld<int32_t>(query.root.address + 12) : 0,
                ld<unsigned long long>(cpu.x[0]));
    } else {
        const acnh::ShovelStageCellQuery query{cpu.x[0], uint32_t(cpu.x[1]), {cpu.x[2]}, {cpu.x[3]}};
        if (use_cpp_shovel_stage_context) {
            ShovelStageContextServices services{cpu, Index};
            cpu.x[0] = acnh::shovel_stage_cell(main_base, query, memory, services).address;
        } else {
            original_shovel_stage_context[Index](cpu);
        }
        if (trace_shovel_stage_context)
            log("acnh shovel stage context %s entry=195190c call=%llx table=%x stage=%x layer=%x root=%d,%d out=%llx",
                route, site, ld<uint32_t>(query.stage + 8), ld<uint32_t>(query.stage + 12), query.layer,
                query.root.address ? ld<int32_t>(query.root.address + 8) : 0,
                query.root.address ? ld<int32_t>(query.root.address + 12) : 0,
                ld<unsigned long long>(cpu.x[0]));
    }
}


Fn original_shovel_stage_height = nullptr;
bool use_cpp_shovel_stage_height = false;
bool trace_shovel_stage_height = false;

struct ShovelStageHeightServices : HeightGuestFrame {
    explicit ShovelStageHeightServices(const CPU& caller) : HeightGuestFrame(caller, 0xb0, 0x50) {}

    uint64_t stack(size_t offset) const { return cpu.x[31] + offset; }
    uint64_t local_material() const { return stack(0x68); }

    void put_tile(uint64_t address, acnh::HalfTile tile) const {
        st<uint64_t>(address, ld<uint64_t>(main_base + 0x4ab2010) + 0x10);
        st<int32_t>(address + 8, tile.x);
        st<int32_t>(address + 12, tile.z);
    }

    float base_height(uint64_t point, uint64_t material, uint64_t stage) const {
        return invoke_child(main_base + 0x15e80f0, 0x15e0724, {point, material, stage}, cpu.v[0].f32[0]).v[0].f32[0];
    }

    acnh::FieldItem field_item(acnh::HalfTile tile, uint64_t stage, acnh::StageHeightLayer layer) const {
        const bool first = layer == acnh::StageHeightLayer::primary;
        put_tile(stack(8), tile);
        put_tile(stack(0x20), {});
        return {invoke_child(main_base + 0x15e7980, first ? 0x15e0798 : 0x15e0c54,
                             {stack(8), stack(0x20), !first, stage}).x[0]};
    }

    uint32_t item_id(acnh::FieldItem item, acnh::StageHeightItemId use) const {
        constexpr std::array resumes{0x15e07f0u, 0x15e0828u, 0x15e0948u, 0x15e09dcu, 0x15e0a28u, 0x15e0c6cu};
        return uint32_t(invoke_child(main_base + 0x267d6c4, resumes[static_cast<size_t>(use)], {item.address}).x[0]);
    }

    acnh::ItemDescriptor describe_item(acnh::FieldItem item) const {
        invoke_child(main_base + 0x114c030, 0x15e0820, {stack(8), item.address});
        return {stack(8)};
    }

    uint32_t height_variant(acnh::FieldItem item) const {
        return uint32_t(invoke_child(main_base + 0x267ebd0, 0x15e0834, {item.address}).x[0]);
    }

    float surface_height(uint64_t point) const {
        return invoke_child(main_base + 0x15e026c, 0x15e08c4, {point, 0, 0}).v[0].f32[0];
    }

    float descriptor_height(acnh::ItemDescriptor descriptor) const {
        const auto metadata = ld<uint64_t>(descriptor.address + 0x10);
        if (!metadata) return 0.f;
        const auto owner = ld<uint64_t>(ld<uint64_t>(main_base + 0x4abbca8));
        const auto value = invoke_child(main_base + 0x1146470, 0x15e0924, {metadata, 0, 0x0c315945, owner}).x[0];
        return value ? ld<float>(value) : 0.f;
    }

    bool alternate_blocked(acnh::FieldItem item) const {
        return invoke_child(main_base + 0x267e024, 0x15e0988, {item.address}).x[0] & 1;
    }

    bool item_supported(acnh::FieldItem item, acnh::StageHeightLayer layer) const {
        const auto resume = layer == acnh::StageHeightLayer::primary ? 0x15e0994 : 0x15e0cac;
        return invoke_child(main_base + 0x267e294, resume, {item.address}).x[0] & 1;
    }

    acnh::ItemDescriptor copy_item(acnh::FieldItem item) const {
        st<uint64_t>(stack(8), main_base + 0x4936b50);
        st<uint64_t>(stack(0x10), ld<uint64_t>(item.address));
        st<uint64_t>(stack(0x18), 0);
        return {stack(8)};
    }

    bool alternate_enabled(acnh::ItemDescriptor descriptor, acnh::StageHeightLayer layer) const {
        const auto resume = layer == acnh::StageHeightLayer::primary ? 0x15e0a58 : 0x15e0cc4;
        return invoke_child(main_base + 0x19e6020, resume, {descriptor.address}).x[0] & 1;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index) const {
        const auto getter = ld<uint64_t>(ld<uint64_t>(owner) + 0x18);
        const auto name = invoke_child(getter, 0x15e0ae8, {owner, index}).x[0];
        return uint32_t(invoke_child(main_base + 0x5bd550, 0x15e0af4, {ld<uint64_t>(name), 0}).x[0]);
    }

    bool special_item(acnh::FieldItem item, acnh::StageHeightLayer layer) const {
        const auto resume = layer == acnh::StageHeightLayer::primary ? 0x15e0b1c : 0x15e0d78;
        return invoke_child(main_base + 0x15e8ee0, resume, {item.address}).x[0] & 1;
    }

    float alternate_height(acnh::ItemDescriptor descriptor, acnh::StageHeightLayer layer, bool stage_matched) const {
        const auto resume = layer == acnh::StageHeightLayer::secondary ? 0x15e0de0 : stage_matched ? 0x15e0be4 : 0x15e0b28;
        return invoke_child(main_base + 0x19e8c70, resume, {descriptor.address}).v[0].f32[0];
    }

    bool terrain_here(acnh::HalfTile tile, acnh::StageHeightLayer layer) const {
        put_tile(stack(0x20), tile);
        const auto resume = layer == acnh::StageHeightLayer::primary ? 0x15e0bac : 0x15e0da8;
        return invoke_child(main_base + 0x87b830, resume, {stack(0x20)}).x[0] & 1;
    }

    uint32_t mapped_stage(uint16_t index, acnh::StageHeightLayer layer) const {
        const auto resume = layer == acnh::StageHeightLayer::primary ? 0x15e0bbc : 0x15e0db8;
        return uint32_t(invoke_child(main_base + 0x12837d0, resume, {index}).x[0]);
    }

    bool terrain_elsewhere(uint32_t mapped, acnh::HalfTile tile, acnh::StageHeightLayer layer) const {
        put_tile(stack(0x20), tile);
        const auto resume = layer == acnh::StageHeightLayer::primary ? 0x15e0bd0 : 0x15e0dcc;
        return invoke_child(main_base + 0x87bc40, resume, {mapped, stack(0x20)}).x[0] & 1;
    }

    acnh::ItemDescriptor describe_secondary(acnh::FieldItem item) const {
        invoke_child(main_base + 0x19d5d04, 0x15e0cbc, {stack(8), item.address});
        return {stack(8)};
    }

    uint64_t secondary_stage() const {
        return invoke_child(main_base + 0x28156e0, 0x15e0d1c, {}).x[0];
    }
};

void shovel_stage_height_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x15f3ffc || !is_shovel_point_classifier_frame(cpu.x[29])) {
        original_shovel_stage_height(cpu);
        return;
    }
    const acnh::ShovelStageHeightQuery query{cpu.x[0], cpu.x[1], cpu.x[3], uint32_t(cpu.x[2]), (cpu.x[4] & 1) != 0};
    if (use_cpp_shovel_stage_height) {
        GuestMemory memory;
        ShovelStageHeightServices services{cpu};
        cpu.v[0].f32[0] = acnh::ShovelStageHeight{main_base, memory, services}.sample(query);
    } else {
        original_shovel_stage_height(cpu);
    }
    if (trace_shovel_stage_height)
        log("acnh shovel stage height %s entry=15e06d0 call=15f3ff8 flags=%x stage=%x extra=%u material=%x out=%x",
            use_cpp_shovel_stage_height ? "cpp" : "original", query.flags, unsigned(ld<uint16_t>(query.stage)),
            unsigned(query.include_special_items), query.material ? ld<uint32_t>(query.material) : 0,
            std::bit_cast<uint32_t>(cpu.v[0].f32[0]));
}


Fn original_shovel_step = nullptr;
bool use_cpp_shovel_step = false;
bool trace_shovel_step = false;

struct ShovelStepServices : HeightGuestFrame {
    explicit ShovelStepServices(const CPU& caller) : HeightGuestFrame(caller, 0xd0, 0xa8) {}

    uint64_t stack(size_t offset) const { return cpu.x[31] + offset; }

    void put_point(uint64_t out, acnh::ShovelTargetPoint point) const {
        st<float>(out, point.x);
        st<float>(out + 4, point.y);
        st<float>(out + 8, point.z);
    }

    uint32_t heading(float dx, float dz) const {
        CPU nested = cpu;
        nested.v[0].f32[0] = dx;
        nested.v[1].f32[0] = dz;
        invoke(nested, main_base + 0x5f0400, 0x15f0d18);
        return uint32_t(nested.x[0]);
    }

    void prepare(acnh::ShovelStepMove move) const {
        put_point(stack(0x50), move.origin);
        put_point(stack(0x5c), move.end);
        st<uint64_t>(stack(0x68), move.options);
        st<uint16_t>(stack(0x6c), 1);
    }

    float collision_radius() const {
        const auto config = invoke_child(main_base + 0x241e98c, 0x15f0ec0, {1}).x[0] + 0x2a0;
        const auto getter = ld<uint64_t>(ld<uint64_t>(config) + 0xf0);
        return ld<float>(invoke_child(getter, 0x15f0ed4, {config}).x[0]);
    }

    bool cast(uint64_t args, uint64_t resume) const {
        return uint32_t(invoke_child(main_base + 0x15ed290, resume, {args, 0, 0, 0}).x[0]) != 0;
    }

    bool try_intermediate_probes(acnh::ShovelTargetPoint point, float radius) const {
        const auto first = stack(0x34);
        put_point(first, point);
        st<float>(first + 0xc, radius);
        st<float>(first + 0x10, 1.f);
        st<uint32_t>(first + 0x14, 0x10);
        st<uint32_t>(first + 0x18, 1);
        const bool outer = cast(first, 0x15f0f14);

        const auto second = stack(0x18);
        st<uint64_t>(second, ld<uint64_t>(first));
        st<uint64_t>(second + 8, ld<uint64_t>(first + 8));
        st<uint32_t>(second + 0x10, ld<uint32_t>(first + 0x10));
        st<uint32_t>(second + 0x14, 4);
        st<uint32_t>(second + 0x18, ld<uint32_t>(first + 0x18));
        const bool inner = cast(second, 0x15f0f4c);
        return outer || !inner;
    }

    bool accept_intermediate(acnh::ShovelTargetPoint point, float radius, unsigned index) const {
        constexpr std::array casts{0x15f0fc8u, 0x15f1028u, 0x15f108cu};
        constexpr std::array validate{0x15f0fe4u, 0x15f1044u, 0x15f10a8u};
        const auto obstruction = stack(0x34);
        put_point(obstruction, point);
        st<float>(obstruction + 0xc, radius);
        st<uint32_t>(obstruction + 0x14, 0x67);
        const auto check = stack(4);
        put_point(check, point);
        st<float>(check + 0xc, radius);
        st<uint16_t>(check + 0x10, 0x0101);
        st<uint8_t>(check + 0x12, 0);

        if (cast(obstruction, casts[index])) return false;
        return invoke_child(main_base + 0x15e9c80, validate[index],
            {check, ld<uint8_t>(check + 0x10), ld<uint8_t>(check + 0x11), ld<uint8_t>(check + 0x12)},
            ld<float>(check + 0xc)).x[0] & 1;
    }

    bool fallback(uint64_t output) const {
        return invoke_child(main_base + 0x15f10f0, 0x15f10c0, {stack(0x50), output}).x[0] & 1;
    }
};

void shovel_step_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x15f4190 || !is_shovel_point_classifier_frame(cpu.x[29])) {
        original_shovel_step(cpu);
        return;
    }
    const acnh::ShovelStepQuery query{cpu.x[0], cpu.x[1]};
    const auto options = trace_shovel_step ? ld<uint32_t>(query.input + 0x18) : 0;
    if (use_cpp_shovel_step) {
        GuestMemory memory;
        ShovelStepServices services{cpu};
        cpu.x[0] = acnh::shovel_check_step(query, memory, services);
    } else {
        original_shovel_step(cpu);
    }
    if (trace_shovel_step)
        log("acnh shovel step %s entry=15f0c64 call=15f418c options=%x y=%x out=%x",
            use_cpp_shovel_step ? "cpp" : "original", options,
            query.output ? ld<uint32_t>(query.output + 4) : 0, unsigned(cpu.x[0]));
}


struct ShovelIntermediateServices : HeightGuestFrame {
    explicit ShovelIntermediateServices(const CPU& caller) : HeightGuestFrame(caller, 0xc0, 0x60) {}

    uint64_t stack(size_t offset) const { return cpu.x[31] + offset; }

    void put_point(uint64_t address, acnh::ShovelTargetPoint point) const {
        st<float>(address, point.x);
        st<float>(address + 4, point.y);
        st<float>(address + 8, point.z);
    }

    void prepare_offset(acnh::ShovelTargetPoint point) const { put_point(stack(8), point); }

    bool blocked_at_center(uint64_t point, float radius) const {
        return (invoke_child(main_base + 0x15ea0c8, 0x15e9cd0, {point}, radius).x[0] & 1) != 0;
    }

    float surface_height(uint64_t point, acnh::ShovelIntermediateHeight kind) const {
        constexpr std::array resumes{0x15e9cf0u, 0x15e9d6cu, 0x15e9fa0u};
        return invoke_child(main_base + 0x15e026c, resumes[static_cast<size_t>(kind)], {point, 0, 0}).v[0].f32[0];
    }

    float offset_surface_height() const {
        return surface_height(stack(8), acnh::ShovelIntermediateHeight::offset);
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, bool center) const {
        const auto getter = ld<uint64_t>(ld<uint64_t>(owner) + 0x18);
        const uint64_t resume = center ? 0x15e9d30 : 0x15e9fe4;
        const auto name = invoke_child(getter, resume, {owner, index}).x[0];
        return uint32_t(invoke_child(main_base + 0x5bd550, resume + 0xc, {ld<uint64_t>(name), 0}).x[0]);
    }

    float stage_height_at(uint64_t point, uint16_t ordinal, uint64_t resume) const {
        const auto context = stack(0x14);
        st<uint16_t>(context, ordinal);
        st<uint16_t>(context + 2, 0);
        st<uint8_t>(context + 4, 1);
        return invoke_child(main_base + 0x15e06d0, resume, {point, 0, 0, context, 0}).v[0].f32[0];
    }

    float stage_height(uint64_t point, uint16_t ordinal) const {
        return stage_height_at(point, ordinal, 0x15e9dfc);
    }

    float offset_stage_height(uint16_t ordinal) const {
        return stage_height_at(stack(8), ordinal, 0x15ea08c);
    }

    bool cast(acnh::ShovelTargetPoint point, float radius, acnh::ShovelIntermediateCast kind) const {
        constexpr std::array resumes{0x15e9e6cu, 0x15e9f84u, 0x15e9ee0u};
        const auto args = stack(0x14);
        put_point(args, point);
        st<float>(args + 0xc, radius);
        st<float>(args + 0x10, 1.f);
        st<uint32_t>(args + 0x14, kind == acnh::ShovelIntermediateCast::skip_offset ? 1 : 0x10);
        st<uint32_t>(args + 0x18, 0);
        return uint32_t(invoke_child(main_base + 0x15ed290, resumes[static_cast<size_t>(kind)], {args, 0, 0, 0}).x[0]) != 0;
    }
};

Fn original_shovel_intermediate = nullptr;
bool use_cpp_shovel_intermediate = false;
bool trace_shovel_intermediate = false;

bool is_shovel_intermediate_call(uint64_t resume, uint64_t frame) {
    return (resume == 0x15f0fe4 || resume == 0x15f1044 || resume == 0x15f10a8)
        && frame && ld<uint64_t>(frame + 8) == main_base + 0x15f4190
        && is_shovel_point_classifier_frame(ld<uint64_t>(frame));
}

void shovel_intermediate_adapter(CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    if (!is_shovel_intermediate_call(resume, cpu.x[29])) {
        original_shovel_intermediate(cpu);
        return;
    }

    const auto flags = unsigned((cpu.x[1] & 1) | ((cpu.x[2] & 1) << 1) | ((cpu.x[3] & 1) << 2));
    const acnh::ShovelIntermediateQuery query{cpu.x[0], cpu.v[0].f32[0],
        (flags & 1) == 0, (flags & 2) != 0, (flags & 4) != 0};
    if (use_cpp_shovel_intermediate) {
        ShovelIntermediateServices services{cpu};
        cpu.x[0] = acnh::shovel_intermediate_point(main_base, query, GuestMemory{}, services);
    } else {
        original_shovel_intermediate(cpu);
    }
    if (trace_shovel_intermediate)
        log("acnh shovel intermediate %s entry=15e9c80 call=%llx flags=%x radius=%x out=%x",
            use_cpp_shovel_intermediate ? "cpp" : "original", static_cast<unsigned long long>(resume - 4),
            flags, std::bit_cast<uint32_t>(query.radius), unsigned(cpu.x[0]));
}


ShovelHeightScope shovel_surface_scope(uint64_t resume, uint64_t frame) {
    if (is_shovel_height_call(main_base + resume) && is_shovel_target_frame(frame)) return ShovelHeightScope::target;
    if (resume == 0x15f4014 && is_shovel_point_classifier_frame(frame)) return ShovelHeightScope::classifier;
    if ((resume == 0x15e9d6c || resume == 0x15e9fa0) && frame
        && is_shovel_intermediate_call(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame)))
        return ShovelHeightScope::intermediate;
    return ShovelHeightScope::unrelated;
}

ShovelHeightScope shovel_sampler_scope(bool ground, uint64_t resume, uint64_t frame) {
    if (resume == (ground ? 0x15e02e0u : 0x15e02f8u)) return shovel_surface_frame(frame);
    constexpr std::array cast_overlays{0x15ed374u, 0x15ed810u, 0x15ed874u, 0x15eed8cu, 0x15ef1e4u, 0x15ef244u};
    const bool from_cast = ground ? resume == 0x15ed360 || resume == 0x15eed78
        : std::find(cast_overlays.begin(), cast_overlays.end(), resume) != cast_overlays.end();
    if (from_cast && is_shovel_step_cast_frame(frame) && ld<uint64_t>(frame + 8) == main_base + 0x15f0fc8)
        return ShovelHeightScope::cast;
    if (ground || !frame) return ShovelHeightScope::unrelated;
    if (resume == 0x15e0724 && is_shovel_stage_height_frame(frame)) return ShovelHeightScope::stage_base;
    constexpr std::array resumes{0x15ea86cu, 0x15ea8d0u, 0x15ea954u, 0x15ea9c8u, 0x15eaa40u,
                                 0x15eaab4u, 0x15eab2cu, 0x15eaba8u, 0x15eac24u};
    if (std::find(resumes.begin(), resumes.end(), resume) != resumes.end()
        && ld<uint64_t>(frame + 8) == main_base + 0x15f4028 && is_shovel_point_classifier_frame(ld<uint64_t>(frame)))
        return ShovelHeightScope::obstacle;
    return ShovelHeightScope::unrelated;
}

bool is_cast_sampler_frame(uint64_t frame, bool ground) {
    return frame && shovel_sampler_scope(ground, ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame))
        == ShovelHeightScope::cast;
}

constexpr std::array<uint64_t, 4> shovel_marker_entries{0x15ea0c8, 0x876320, 0x849ac8, 0x8493f8};
constexpr std::array<uint64_t, 5> shovel_marker_direct_resumes{0x15ea1b4, 0x15ea294, 0x15ea39c, 0x15ea494, 0x15ea590};
constexpr std::array<uint64_t, 4> shovel_marker_global_resumes{0x15ea628, 0x15ea6c0, 0x15ea760, 0x15ea828};
constexpr std::array<uint64_t, 4> shovel_marker_primary_resumes{0x8494c0, 0x8494f8, 0x84952c, 0x849560};
std::array<Fn, 4> original_shovel_marker{};
Fn original_shovel_primary_terrain_fallback = nullptr;
Fn original_shovel_cast_secondary = nullptr;
bool use_cpp_shovel_marker = false;
bool trace_shovel_marker = false;

bool is_shovel_intermediate_frame(uint64_t frame) {
    return frame && is_shovel_intermediate_call(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame));
}

bool is_shovel_marker_extent_frame(uint64_t frame) {
    return frame && ld<uint64_t>(frame + 8) == main_base + 0x15e9cd0
        && is_shovel_intermediate_frame(ld<uint64_t>(frame));
}

bool is_shovel_marker_global_call(uint64_t resume, uint64_t frame) {
    if (resume == 0x15f47e8) return is_shovel_point_classifier_frame(frame);
    return std::find(shovel_marker_global_resumes.begin(), shovel_marker_global_resumes.end(), resume)
            != shovel_marker_global_resumes.end()
        && is_shovel_marker_extent_frame(frame);
}

bool is_shovel_marker_direct_call(uint64_t resume, uint64_t frame) {
    if (resume == 0x15f4370) return is_shovel_point_classifier_frame(frame);
    if (resume == 0x87639c)
        return frame && is_shovel_marker_global_call(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame));
    return std::find(shovel_marker_direct_resumes.begin(), shovel_marker_direct_resumes.end(), resume)
            != shovel_marker_direct_resumes.end()
        && is_shovel_marker_extent_frame(frame);
}

bool is_shovel_marker_mask_frame(uint64_t frame) {
    return frame && is_shovel_marker_direct_call(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame));
}

bool is_shovel_marker_corners_frame(uint64_t frame) {
    return frame && ld<uint64_t>(frame + 8) == main_base + 0x849afc
        && is_shovel_marker_mask_frame(ld<uint64_t>(frame));
}

struct ShovelMarkerServices : HeightGuestFrame {
    ShovelMarkerServices(const CPU& caller, size_t index)
        : HeightGuestFrame(caller, frame_sizes[index], frame_offsets[index]) {}

    static constexpr std::array<uint64_t, 4> frame_sizes{0x80, 0x20, 0x30, 0x90};
    static constexpr std::array<uint64_t, 4> frame_offsets{0x40, 0x10, 0x10, 0x50};

    uint64_t stack(size_t offset) const { return cpu.x[31] + offset; }

    uint64_t put_tile(acnh::HalfTile tile, uint64_t type, uint64_t address) const {
        st<uint64_t>(address, type);
        st<int32_t>(address + 8, tile.x);
        st<int32_t>(address + 12, tile.z);
        return address;
    }

    uint64_t prepare_mask(uint64_t address) const {
        st<uint64_t>(address, 1 << 8);
        st<uint32_t>(address + 8, 0);
        return address;
    }

    bool test_direct(acnh::TerrainOwner owner, acnh::HalfTile tile, uint64_t type, unsigned probe) const {
        const auto descriptor = put_tile(tile, type, stack(probe == 0 ? 0x10 : 0));
        const auto mask = prepare_mask(stack(probe == 0 ? 0 : 0x20));
        return invoke_child(main_base + 0x849ac8, shovel_marker_direct_resumes[probe],
                            {owner.address, descriptor, mask}).x[0] & 1;
    }

    bool test_global(acnh::HalfTile tile, uint64_t type, unsigned probe) const {
        return invoke_child(main_base + 0x876320, shovel_marker_global_resumes[probe],
                            {put_tile(tile, type, stack(0))}).x[0] & 1;
    }

    bool test(acnh::TerrainOwner owner, uint64_t descriptor) const {
        return invoke_child(main_base + 0x849ac8, 0x87639c,
                            {owner.address, descriptor, prepare_mask(stack(0))}).x[0] & 1;
    }

    acnh::TerrainQuadrants prepare_corners() const {
        const acnh::TerrainQuadrants corners{{stack(0x28), stack(0xc), stack(8), stack(4)}};
        for (const auto address : corners.output) st<uint32_t>(address, 0);
        return corners;
    }

    bool sample_corners(acnh::TerrainOwner owner, uint64_t descriptor, acnh::TerrainQuadrants corners) const {
        const auto& out = corners.output;
        return invoke_child(main_base + 0x8493f8, 0x849afc,
                            {owner.address, descriptor, out[0], out[1], out[2], out[3]}).x[0] & 1;
    }

    std::array<uint8_t, 4> sample_kinds(acnh::TerrainGrid grid, uint64_t descriptor) const {
        invoke_child(main_base + 0x862530, 0x84945c, {grid.address, descriptor, stack(0xc)});
        return {ld<uint8_t>(stack(0x3c)), ld<uint8_t>(stack(0x3d)),
                ld<uint8_t>(stack(0x3e)), ld<uint8_t>(stack(0x3f))};
    }

    uint32_t primary_kind(acnh::TerrainWorldPoint& point, unsigned corner) const {
        const auto vector = stack(0xc);
        st<float>(vector, point.x);
        st<float>(vector + 8, point.z);
        const auto result = invoke_child(main_base + 0x87a980, shovel_marker_primary_resumes[corner], {vector});
        point = {ld<float>(vector), ld<float>(vector + 8)};
        return uint32_t(result.x[0]);
    }
};

template<size_t Index>
void shovel_marker_adapter(CPU& cpu) {
    const auto frame = cpu.x[29];
    const auto resume = cpu.x[30] - main_base;
    bool scoped;
    if constexpr (Index == 0) scoped = resume == 0x15e9cd0 && is_shovel_intermediate_frame(frame);
    if constexpr (Index == 1) scoped = is_shovel_marker_global_call(resume, frame);
    if constexpr (Index == 2) scoped = is_shovel_marker_direct_call(resume, frame);
    if constexpr (Index == 3) scoped = resume == 0x849afc && is_shovel_marker_mask_frame(frame);
    if (!scoped) {
        original_shovel_marker[Index](cpu);
        return;
    }
    const auto& x = cpu.x;
    const auto a0 = x[0], a1 = x[1], a2 = x[2];
    const acnh::TerrainQuadrants corners{{x[2], x[3], x[4], x[5]}};
    const auto radius = cpu.v[0].f32[0];
    if (use_cpp_shovel_marker) {
        GuestMemory memory;
        ShovelMarkerServices services{cpu, Index};
        if constexpr (Index == 0) cpu.x[0] = acnh::shovel_marker_extent(main_base, {a0, radius}, memory, services);
        if constexpr (Index == 1) cpu.x[0] = acnh::shovel_marker_global(main_base, a0, memory, services);
        if constexpr (Index == 2) cpu.x[0] = acnh::shovel_marker_match(a0, a1, a2, memory, services);
        if constexpr (Index == 3) cpu.x[0] = acnh::shovel_marker_corners(a0, a1, corners, memory, services);
    } else {
        original_shovel_marker[Index](cpu);
    }

    if (!trace_shovel_marker) return;
    const char* route = use_cpp_shovel_marker ? "cpp" : "original";
    const auto entry = static_cast<unsigned long long>(shovel_marker_entries[Index]);
    const auto site = static_cast<unsigned long long>(resume - 4);
    const auto result = unsigned(cpu.x[0]);
    if constexpr (Index == 3) {
        const auto& out = corners.output;
        log("acnh shovel marker %s entry=%llx call=%llx kinds=%x,%x,%x,%x out=%x",
            route, entry, site, ld<uint32_t>(out[0]), ld<uint32_t>(out[1]), ld<uint32_t>(out[2]), ld<uint32_t>(out[3]), result);
    } else {
        log("acnh shovel marker %s entry=%llx call=%llx out=%x", route, entry, site, result);
    }
}

bool try_shovel_marker_terrain_sample(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x84945c || !is_shovel_marker_corners_frame(cpu.x[29])) return false;
    const auto output = cpu.x[2];
    const bool native = use_cpp_shovel_marker && use_cpp_terrain_sample;
    if (native) {
        GuestMemory memory;
        acnh::sample_terrain({cpu.x[0]}, {ld<int32_t>(cpu.x[1] + 8), ld<int32_t>(cpu.x[1] + 12)}, {output}, memory);
    } else {
        original_terrain_sample(cpu);
    }
    if (trace_shovel_marker) {
        std::array<uint8_t, 0x34> record;
        for (size_t i = 0; i < record.size(); ++i) record[i] = ld<uint8_t>(output + i);
        log("acnh shovel marker %s entry=862530 call=849458 out=%s",
            native ? "cpp" : "original", cache_hex(record).c_str());
    }
    return true;
}

void shovel_cast_terrain_fallback(CPU& cpu, acnh::TerrainFallback fallback, Fn original) {
    const auto point = cpu.x[0];
    const auto before_x = ld<uint32_t>(point);
    const auto before_z = ld<uint32_t>(point + 8);
    const auto resume = cpu.x[30] - main_base;
    const bool secondary = fallback == acnh::TerrainFallback::secondary;
    const bool native = use_cpp_shovel_cast_height && use_cpp_sampler_terrain
        && (secondary ? use_cpp_terrain_secondary : use_cpp_terrain_primary);
    if (native) {
        cpu.x[0] = route_terrain_fallback(cpu, fallback, true);
    } else {
        original(cpu);
    }
    if (trace_shovel_cast_height)
        log("acnh shovel height chain %s entry=%x call=%llx scope=%u moved=%u out=%x",
            native ? "cpp" : "original", secondary ? 0x87aa70 : 0x87a980,
            static_cast<unsigned long long>(resume - 4), unsigned(ShovelHeightScope::cast),
            unsigned(before_x != ld<uint32_t>(point) || before_z != ld<uint32_t>(point + 8)),
            unsigned(cpu.x[0]));
}

void shovel_cast_secondary_adapter(CPU& cpu) {
    if (cpu.x[30] == main_base + 0x83ab2c && is_cast_sampler_frame(cpu.x[29], false)) {
        shovel_cast_terrain_fallback(cpu, acnh::TerrainFallback::secondary, original_shovel_cast_secondary);
    } else {
        original_shovel_cast_secondary(cpu);
    }
}

void shovel_primary_terrain_fallback_adapter(CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    if ((resume == 0x83b0e4 || resume == 0x83ab10) && is_cast_sampler_frame(cpu.x[29], resume == 0x83b0e4)) {
        shovel_cast_terrain_fallback(cpu, acnh::TerrainFallback::primary, original_shovel_primary_terrain_fallback);
        return;
    }
    if (std::find(shovel_marker_primary_resumes.begin(), shovel_marker_primary_resumes.end(), resume)
            == shovel_marker_primary_resumes.end() || !is_shovel_marker_corners_frame(cpu.x[29])) {
        original_shovel_primary_terrain_fallback(cpu);
        return;
    }

    const auto point = cpu.x[0];
    const bool native = use_cpp_shovel_marker && use_cpp_terrain_primary;
    const auto x = ld<uint32_t>(point), z = ld<uint32_t>(point + 8);
    if (native) {
        cpu.x[0] = route_terrain_fallback(cpu, acnh::TerrainFallback::primary, true);
    } else {
        original_shovel_primary_terrain_fallback(cpu);
    }
    if (trace_shovel_marker)
        log("acnh shovel marker %s entry=87a980 call=%llx moved=%u out=%x",
            native ? "cpp" : "original", static_cast<unsigned long long>(resume - 4),
            unsigned(x != ld<uint32_t>(point) || z != ld<uint32_t>(point + 8)), unsigned(cpu.x[0]));
}


struct ShovelCollisionServices : HeightGuestFrame {
    ShovelCollisionServices(const CPU& caller, bool compare_height)
        : HeightGuestFrame(caller, compare_height ? 0x50 : 0x90, compare_height ? 0x20 : 0x58) {}

    float scalar = 0.f;

    float surface_height(uint64_t point, acnh::ShovelCollisionProbe probe) {
        const auto resume = probe == acnh::ShovelCollisionProbe::height_difference ? 0x15e11d4 : 0x15ec7b8;
        scalar = invoke_child(main_base + 0x15e026c, resume, {point, 0, 0}).v[0].f32[0];
        return scalar;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, acnh::ShovelCollisionProbe probe) {
        const auto resume = probe == acnh::ShovelCollisionProbe::height_difference ? 0x15e1214 : 0x15ec860;
        const auto getter = ld<uint64_t>(ld<uint64_t>(owner) + 0x18);
        const auto name = invoke_child(getter, resume, {owner, index}, scalar);
        const auto hash = invoke_child(main_base + 0x5bd550, resume + 0xc, {ld<uint64_t>(name.x[0]), 0}, name.v[0].f32[0]);
        scalar = hash.v[0].f32[0];
        return uint32_t(hash.x[0]);
    }

    float stage_height(uint64_t point, bool alternate, uint16_t ordinal) const {
        const auto context = cpu.x[31] + 8;
        st<uint16_t>(context, ordinal);
        st<uint16_t>(context + 2, 0);
        st<uint8_t>(context + 4, 1);
        return invoke_child(main_base + 0x15e06d0, 0x15e12c0, {point, 0, alternate, context, 0}, scalar).v[0].f32[0];
    }

    uint32_t stage_item_id(acnh::HalfTile tile, uint32_t key) const {
        const auto root = cpu.x[31] + 8;
        const auto point = cpu.x[31] + 0x18;
        const auto query = cpu.x[31] + 0x28;
        const auto coordinate_type = ld<uint64_t>(main_base + 0x4ab2010) + 0x10;
        st<uint64_t>(root, coordinate_type);
        st<uint64_t>(root + 8, 0);
        st<uint64_t>(point, coordinate_type);
        st<int32_t>(point + 8, tile.x);
        st<int32_t>(point + 12, tile.z);
        st<uint64_t>(query, ld<uint64_t>(main_base + 0x4abfe68) + 0x10);
        st<uint32_t>(query + 8, 0);
        st<uint32_t>(query + 12, key);
        const auto permitted = invoke_child(main_base + 0x16baf44, 0x15ec89c, {key}).x[0] & 1;
        st<uint8_t>(query + 16, uint8_t(permitted));
        const auto record = invoke_child(main_base + 0x195190c, 0x15ec8b8, {query, 0, point, root}).x[0];
        return uint32_t(invoke_child(main_base + 0x267d6c4, 0x15ec8bc, {record}).x[0]);
    }
};

constexpr std::array<uint64_t, 2> shovel_collision_probe_entries{0x15e11a0, 0x15ec764};
std::array<Fn, 2> original_shovel_collision_probes{};
bool use_cpp_shovel_collision_probes = false;
bool trace_shovel_collision_probes = false;

bool is_shovel_step_cast_frame(uint64_t frame) {
    if (!frame) return false;
    const auto site = ld<uint64_t>(frame + 8) - main_base;
    if (site != 0x15f0f14 && site != 0x15f0f4c && site != 0x15f0fc8) return false;
    const auto step = ld<uint64_t>(frame);
    return step && ld<uint64_t>(step + 8) == main_base + 0x15f4190
        && is_shovel_point_classifier_frame(ld<uint64_t>(step));
}

template<size_t Index>
void shovel_collision_probe_adapter(CPU& cpu) {
    constexpr std::array<std::array<uint64_t, 2>, 2> resumes{{
        {0x15ede50, 0x15ef5b0}, {0x15edfd8, 0x15ef894},
    }};
    const auto resume = cpu.x[30] - main_base;
    const auto frame = cpu.x[29];
    if (std::find(resumes[Index].begin(), resumes[Index].end(), resume) == resumes[Index].end()
        || !is_shovel_step_cast_frame(frame)) {
        original_shovel_collision_probes[Index](cpu);
        return;
    }

    const auto call = ld<uint64_t>(frame + 8) - main_base - 4;
    const auto one = bool(cpu.x[1] & 1);
    const auto two = bool(cpu.x[2] & 1);
    if (use_cpp_shovel_collision_probes) {
        GuestMemory memory;
        ShovelCollisionServices services{cpu, Index == 0};
        if constexpr (Index == 0)
            cpu.x[0] = acnh::shovel_collision_height_difference(main_base, {cpu.x[0], one, two}, memory, services);
        else
            cpu.x[0] = acnh::shovel_collision_stage_item(main_base, {cpu.x[0], one}, memory, services);
    } else {
        original_shovel_collision_probes[Index](cpu);
    }
    if (!trace_shovel_collision_probes) return;
    const auto flags = unsigned(one) | (Index == 0 ? unsigned(two) << 1 : 0u);
    log("acnh shovel collision probe %s entry=%llx call=%llx cast=%llx flags=%x out=%x",
        use_cpp_shovel_collision_probes ? "cpp" : "original",
        static_cast<unsigned long long>(shovel_collision_probe_entries[Index]),
        static_cast<unsigned long long>(resume - 4), static_cast<unsigned long long>(call),
        flags, unsigned(cpu.x[0]));
}


struct ShovelStepCastServices : HeightGuestFrame {
    explicit ShovelStepCastServices(const CPU& caller) : HeightGuestFrame(caller, 0x140, 0xe0) {}

    uint64_t stack(size_t offset) const { return cpu.x[31] + offset; }

    void put_point(uint64_t address, acnh::ShovelTargetPoint point) const {
        st<float>(address, point.x);
        st<float>(address + 4, point.y);
        st<float>(address + 8, point.z);
    }


    uint32_t cast_material(acnh::ShovelTargetPoint& point, acnh::ShovelCastMaterial use, bool center) const {
        constexpr std::array center_resumes{0x15ed360u, 0x15ed374u, 0x15ed810u, 0x15ed874u};
        constexpr std::array rim_resumes{0x15eed78u, 0x15eed8cu, 0x15ef1e4u, 0x15ef244u};
        constexpr std::array outputs{0xa8u, 0x98u, 0x50u, 0xa8u};
        const auto index = static_cast<size_t>(use);
        const auto resume = center ? center_resumes[index] : rim_resumes[index];
        const auto sample = stack(center ? 0x78 : 0x44);
        const auto output = stack(outputs[index]);
        const auto owner = ld<uint64_t>(ld<uint64_t>(main_base + 0x4ab27c8)) + 0x28;
        put_point(sample, point);
        st<uint32_t>(output, 0);
        const auto entry = use == acnh::ShovelCastMaterial::base ? 0x83b024 : 0x83a950;
        invoke_child(main_base + entry, resume, {owner, sample, output});
        point = {ld<float>(sample), ld<float>(sample + 4), ld<float>(sample + 8)};
        return ld<uint32_t>(output);
    }

    bool cast_tile_flag(acnh::HalfTile tile, bool center) const {
        const auto query = stack(0x50);
        st<uint64_t>(query, ld<uint64_t>(main_base + 0x4ab2018) + 0x10);
        st<int32_t>(query + 8, tile.x);
        st<int32_t>(query + 12, tile.z);
        return (invoke_child(main_base + 0x1218220, center ? 0x15ed69c : 0x15ef09c, {query}).x[0] & 1) != 0;
    }

    bool material_flag(uint32_t kind, acnh::ShovelCastProperty property, bool center) const {
        const bool base = property == acnh::ShovelCastProperty::base;
        const auto database = ld<uint64_t>(ld<uint64_t>(main_base + 0x4ab46c8)) + 8;
        const auto row_site = base ? (center ? 0x15ed394 : 0x15eedac) : (center ? 0x15ed894 : 0x15ef264);
        const auto row = invoke_child(main_base + 0x8807cc, row_site, {database, kind}).x[0];
        if (!row) return false;
        const auto cache = ld<uint64_t>(ld<uint64_t>(main_base + (base ? 0x4ab5d10 : 0x4ab5d28)));
        const auto col_site = base ? (center ? 0x15ed3b4 : 0x15eedcc) : (center ? 0x15ed8b4 : 0x15ef284);
        const auto column = invoke_child(main_base + 0x8823a0, col_site,
            {row, base ? 13u : 11u, base ? 0x58da05edu : 0xe7e965dbu, cache}).x[0];
        return column && ld<uint8_t>(column) != 0;
    }


    bool probe(acnh::ShovelTargetPoint& point, acnh::ShovelStepCastMask mask, bool center) const {
        const bool height = mask == acnh::ShovelStepCastMask::height;
        const auto address = stack(center ? 0x78 : 0x44);
        put_point(address, point);
        const auto resume = height ? (center ? 0x15ede50 : 0x15ef5b0) : (center ? 0x15edfd8 : 0x15ef894);
        const auto result = invoke_child(main_base + (height ? 0x15e11a0 : 0x15ec764), resume, {address, 1, 0});
        point = {ld<float>(address), ld<float>(address + 4), ld<float>(address + 8)};
        return (result.x[0] & 1) != 0;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, bool center) const {
        const uint64_t resume = center ? 0x15ef9a0 : 0x15ef678;
        const auto getter = ld<uint64_t>(ld<uint64_t>(owner) + 0x18);
        const auto name = invoke_child(getter, resume, {owner, index}).x[0];
        return uint32_t(invoke_child(main_base + 0x5bd550, resume + 0xc, {ld<uint64_t>(name), 0}).x[0]);
    }

    void record_height_contact(acnh::ShovelTargetPoint point, uint32_t key, bool center) const {
        const auto coordinate = [](float value) { return std::isnan(value) ? 0 : acnh::terrain_half_tile(value); };
        const auto descriptor = stack(0x50);
        const auto root = stack(0x98);
        const auto tile = stack(0xa8);
        const auto type = ld<uint64_t>(main_base + 0x4ab2010) + 0x10;
        st<uint64_t>(root, type);
        st<uint64_t>(root + 8, 0);
        st<uint64_t>(tile, type);
        st<int32_t>(tile + 8, coordinate(point.x));
        st<int32_t>(tile + 12, coordinate(point.z));
        st<uint64_t>(descriptor, ld<uint64_t>(main_base + 0x4abfe68) + 0x10);
        st<uint32_t>(descriptor + 8, 0);
        st<uint32_t>(descriptor + 12, key);
        const auto flag = invoke_child(main_base + 0x16baf44, center ? 0x15ef9e4 : 0x15ef6ec, {key}).x[0] & 1;
        st<uint8_t>(descriptor + 16, uint8_t(flag));
        invoke_child(main_base + 0x195190c, center ? 0x15efa00 : 0x15ef708, {descriptor, 0, tile, root});
    }
};

Fn original_shovel_step_cast = nullptr;
bool use_cpp_shovel_simple_cast = false;
bool trace_shovel_simple_cast = false;
bool use_cpp_shovel_mixed_cast = false;
bool trace_shovel_mixed_cast = false;

void shovel_step_cast_adapter(CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    const bool mixed = resume == 0x15f0fc8;
    const auto point = cpu.x[0];
    if ((!mixed && resume != 0x15f0f14 && resume != 0x15f0f4c) || cpu.x[1] || cpu.x[2] || cpu.x[3]
        || !cpu.x[29] || ld<uint64_t>(cpu.x[29] + 8) != main_base + 0x15f4190
        || !is_shovel_point_classifier_frame(ld<uint64_t>(cpu.x[29]))) {
        original_shovel_step_cast(cpu);
        return;
    }
    const auto mask = ld<uint32_t>(point + 0x14);
    const auto mode = ld<uint32_t>(point + 0x18);
    const auto radius = ld<float>(point + 0xc);
    const auto range = radius * ld<float>(point + 0x10);
    const bool valid_mask = mixed ? mask == 0x67 : mask == 4 || mask == 16;
    if (mode != 1 || !valid_mask || radius < 0.f || !std::isfinite(range)) {
        original_shovel_step_cast(cpu);
        return;
    }
    const bool native = mixed ? use_cpp_shovel_mixed_cast : use_cpp_shovel_simple_cast;
    if (native) {
        const acnh::ShovelStepCastQuery query{{ld<float>(point), ld<float>(point + 4), ld<float>(point + 8)},
            range, static_cast<acnh::ShovelStepCastMask>(mask)};
        ShovelStepCastServices services{cpu};
        cpu.x[0] = mixed ? acnh::shovel_mixed_step_cast(main_base, query, GuestMemory{}, services)
                         : acnh::shovel_simple_step_cast(main_base, query, GuestMemory{}, services);
    } else {
        original_shovel_step_cast(cpu);
    }
    const bool trace = mixed ? trace_shovel_mixed_cast : trace_shovel_simple_cast;
    if (trace)
        log("acnh shovel %s cast %s entry=15ed290 call=%llx mask=%x range=%x out=%x",
            mixed ? "mixed" : "simple", native ? "cpp" : "original", static_cast<unsigned long long>(resume - 4),
            mask, std::bit_cast<uint32_t>(range), unsigned(cpu.x[0]));
}

Fn original_shovel_point_classifier = nullptr;
bool use_cpp_shovel_point_classifier = false;
bool trace_shovel_point_classifier = false;

struct ShovelPointServices : HeightGuestFrame {
    explicit ShovelPointServices(const CPU& caller) : HeightGuestFrame(caller, 0xf0, 0xb8) {}

    uint64_t stack(size_t offset) const { return cpu.x[31] + offset; }

    void put_point(uint64_t address, acnh::ShovelTargetPoint point) const {
        st<float>(address, point.x);
        st<float>(address + 4, point.y);
        st<float>(address + 8, point.z);
    }

    void put_tile(uint64_t address, acnh::HalfTile tile) const {
        st<uint64_t>(address, ld<uint64_t>(main_base + 0x4ab2010) + 0x10);
        st<int32_t>(address + 8, tile.x);
        st<int32_t>(address + 12, tile.z);
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, acnh::ShovelPointStage phase) const {
        constexpr std::array resumes{0x15f3f4cu, 0x15f410cu, 0x15f429cu};
        const auto resume = resumes[static_cast<size_t>(phase)];
        const auto getter = ld<uint64_t>(ld<uint64_t>(owner) + 0x18);
        const auto name = invoke_child(getter, resume, {owner, index}).x[0];
        return uint32_t(invoke_child(main_base + 0x5bd550, resume + 0xc, {ld<uint64_t>(name), 0}).x[0]);
    }

    float terrain_height(uint64_t point, uint16_t stage_index) const {
        st<uint16_t>(stack(8), stage_index);
        st<uint16_t>(stack(0xa), 0);
        st<uint8_t>(stack(0xc), 1);
        return invoke_child(main_base + 0x15e06d0, 0x15f3ffc, {point, 0, 1, stack(8), 0}, ld<float>(point + 8)).v[0].f32[0];
    }

    float surface_height(uint64_t point) const {
        return invoke_child(main_base + 0x15e026c, 0x15f4014, {point, 0, 0}).v[0].f32[0];
    }

    bool point_blocked(uint64_t point) const {
        return invoke_child(main_base + 0x15ea82c, 0x15f4028, {point}).x[0] & 1;
    }

    bool can_step(acnh::ShovelTargetPoint origin, acnh::ShovelTargetPoint point,
                  acnh::ShovelTargetPoint& output) const {
        put_point(stack(0x50), origin);
        put_point(stack(0x5c), point);
        st<uint32_t>(stack(0x68), 0x01010001);
        st<uint16_t>(stack(0x6c), 1);
        st<uint8_t>(stack(0x6e), 0);
        if (invoke_child(main_base + 0x15f0c64, 0x15f4190, {stack(0x50), stack(0x70)}).x[0] & 1) return true;
        output = {ld<float>(stack(0x70)), ld<float>(stack(0x74)), ld<float>(stack(0x78))};
        return false;
    }

    uint32_t try_alternate(acnh::ShovelTargetPoint origin, acnh::ShovelTargetPoint alternate, float limit) const {
        put_point(stack(8), origin);
        put_point(stack(0x14), alternate);
        st<uint64_t>(stack(0x20), 0);
        st<uint32_t>(stack(0x28), 0);
        st<float>(stack(0x2c), limit);
        st<uint8_t>(stack(0x30), 0);
        st<uint64_t>(stack(0x34), 0);
        st<uint64_t>(stack(0x3c), 0);
        st<uint32_t>(stack(0x44), 0);
        st<uint8_t>(stack(0x48), 0);
        return uint32_t(invoke_child(main_base + 0x15f36e0, 0x15f41e0, {stack(8)}).x[0]);
    }

    acnh::FieldItem field_record(acnh::HalfTile tile, uint32_t stage, acnh::ShovelPointStage phase) const {
        const bool raised = phase == acnh::ShovelPointStage::raised;
        const auto primary = stack(raised ? 0x50 : 0x90);
        const auto fallback = stack(raised ? 0x90 : 0x80);
        put_tile(primary, tile);
        put_tile(fallback, {});
        if (raised) put_tile(stack(0x80), {});
        st<uint64_t>(stack(8), ld<uint64_t>(main_base + 0x4abfe68) + 0x10);
        st<uint32_t>(stack(0x10), 0);
        st<uint32_t>(stack(0x14), stage);
        const auto allowed = invoke_child(main_base + 0x16baf44, raised ? 0x15f42d8 : 0x15f4614, {stage}).x[0] & 1;
        st<uint8_t>(stack(0x18), uint8_t(allowed));
        return {invoke_child(main_base + 0x195190c, raised ? 0x15f42f4 : 0x15f4630,
                             {stack(8), 0, primary, fallback}).x[0]};
    }

    bool terrain_excluded(acnh::TerrainOwner owner) const {
        st<uint64_t>(stack(0x88), ld<uint64_t>(stack(0x98)));
        st<uint64_t>(stack(8), 1u << 8);
        st<uint32_t>(stack(0x10), 0);
        return invoke_child(main_base + 0x849ac8, 0x15f4370, {owner.address + 0x28, stack(0x80), stack(8)}).x[0] & 1;
    }

    bool blocked_step(acnh::ShovelTargetPoint point) const {
        put_point(stack(8), point);
        st<uint64_t>(stack(0x14), 0x3f80000000000000ull);
        st<uint32_t>(stack(0x1c), 2);
        return uint32_t(invoke_child(main_base + 0x15ed290, 0x15f43c0, {stack(8), 0, 0, 0}).x[0]) != 0;
    }

    int32_t adjust_step(acnh::ShovelPointQuery query) const {
        return int32_t(invoke_child(main_base + 0x15f33f0, 0x15f43d4, {query.request.address, query.point}).x[0]);
    }

    bool terrain_barrier(acnh::HalfTile tile) const {
        put_tile(stack(8), tile);
        return invoke_child(main_base + 0x876320, 0x15f47e8, {stack(8)}).x[0] & 1;
    }

    uint32_t item_id(acnh::FieldItem item, acnh::ShovelPointItemId use) const {
        constexpr std::array resumes{0x15f4444u, 0x15f44bcu, 0x15f4508u, 0x15f4598u, 0x15f45c4u,
                                     0x15f467cu, 0x15f46a8u, 0x15f46c8u, 0x15f4740u, 0x15f478cu};
        return uint32_t(invoke_child(main_base + 0x267d6c4, resumes[static_cast<size_t>(use)], {item.address}).x[0]);
    }

    acnh::ItemDescriptor copy_item(acnh::FieldItem item) const {
        st<uint64_t>(stack(8), ld<uint64_t>(main_base + 0x4ac0510) + 0x10);
        st<uint64_t>(stack(0x10), ld<uint64_t>(item.address));
        st<uint64_t>(stack(0x18), 0);
        return {stack(8)};
    }

    acnh::ItemDescriptor describe_item(acnh::FieldItem item, acnh::ShovelPointStage phase) const {
        invoke_child(main_base + 0x114c030, phase == acnh::ShovelPointStage::raised ? 0x15f45e0 : 0x15f4840,
                     {stack(8), item.address});
        return {stack(8)};
    }

    bool item_supported(acnh::FieldItem item, acnh::ShovelPointStage phase) const {
        return invoke_child(main_base + 0x267e294, phase == acnh::ShovelPointStage::raised ? 0x15f4538 : 0x15f4810,
                            {item.address}).x[0] & 1;
    }

    bool metadata_enabled(acnh::ItemDescriptor descriptor) const {
        return invoke_child(main_base + 0x19e59b0, 0x15f47bc, {descriptor.address}).x[0] & 1;
    }

    bool alternate_enabled(acnh::ItemDescriptor descriptor, acnh::ShovelPointStage phase) const {
        return invoke_child(main_base + 0x19e6020, phase == acnh::ShovelPointStage::raised ? 0x15f4544 : 0x15f481c,
                            {descriptor.address}).x[0] & 1;
    }

    uint32_t category(acnh::FieldItem item, acnh::CatalogEntry metadata, acnh::ShovelPointCategory use) const {
        constexpr std::array resumes{0x15f45f0u, 0x15f48d8u, 0x15f48f8u, 0x15f4850u, 0x15f4870u, 0x15f48a8u};
        return uint32_t(invoke_child(main_base + 0x113db50, resumes[static_cast<size_t>(use)],
                                     {item.address, metadata.address}).x[0]);
    }

    bool category_override(acnh::ItemDescriptor descriptor) const {
        return invoke_child(main_base + 0x114dc64, 0x15f4890, {descriptor.address, 0}).x[0] & 1;
    }
};

void shovel_point_classifier_adapter(CPU& cpu) {
    const auto resume = cpu.x[30] - main_base;
    if (!is_shovel_point_probe(resume) || !is_shovel_target_frame(cpu.x[29])) {
        original_shovel_point_classifier(cpu);
        return;
    }

    const acnh::ShovelPointQuery query{cpu.x[0], {cpu.x[1]}};
    const auto result_address = query.request.address + 0x38;
    const auto before = trace_shovel_point_classifier ? ld<uint32_t>(result_address) : 0;
    if (use_cpp_shovel_point_classifier) {
        GuestMemory memory;
        ShovelPointServices services{cpu};
        cpu.x[0] = acnh::ShovelPointClassifier{main_base, memory, services}.classify(query);
    } else {
        original_shovel_point_classifier(cpu);
    }
    if (trace_shovel_point_classifier) {
        const auto selected = query.request.address + 0x2c;
        const bool kept = ld<uint64_t>(selected) == ld<uint64_t>(query.point)
                       && ld<uint32_t>(selected + 8) == ld<uint32_t>(query.point + 8);
        log("acnh shovel point %s entry=15f3ed0 call=%llx kept=%u before=%x after=%x out=%x",
            use_cpp_shovel_point_classifier ? "cpp" : "original", static_cast<unsigned long long>(resume - 4),
            unsigned(kept), before, ld<uint32_t>(result_address), unsigned(cpu.x[0]));
    }
}


struct ShovelTargetServices {
    CPU cpu;

    explicit ShovelTargetServices(const CPU& caller) : cpu(caller) {
        cpu.x[31] -= 0x70;
        cpu.x[29] = cpu.x[31] + 0x30;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    uint32_t heading(float x, float z) const {
        CPU nested = cpu;
        nested.v[0].f32[0] = x;
        nested.v[1].f32[0] = z;
        nested.x[30] = main_base + 0x15f3748;
        call(nested, main_base + 0x5f0400);
        return static_cast<uint32_t>(nested.x[0]);
    }

    float height(acnh::ShovelTargetRequest request, acnh::ShovelTargetHeight which) const {
        const auto resume = shovel_height_resumes[size_t(which)];
        CPU nested = cpu;
        nested.x[0] = request.address + (which == acnh::ShovelTargetHeight::desired ? 0xc : 0);
        nested.x[1] = nested.x[2] = 0;
        nested.v[0] = {};
        nested.x[30] = main_base + resume;
        if (!use_cpp_shovel_height[0]) trace_shovel_dependency(0x15e026c, resume);
        call(nested, main_base + 0x15e026c);
        return nested.v[0].f32[0];
    }

    uint32_t probe(acnh::ShovelTargetPoint point, acnh::ShovelTargetRequest request,
                   acnh::ShovelTargetProbe method, size_t index) const {
        const auto resume = method == acnh::ShovelTargetProbe::approach ? shovel_approach_resumes[index]
            : method == acnh::ShovelTargetProbe::radius ? shovel_radius_resume : shovel_nearby_resumes[index];
        CPU nested = cpu;
        const auto vector = cpu.x[31] + 4;
        st<float>(vector, point.x);
        st<float>(vector + 4, point.y);
        st<float>(vector + 8, point.z);
        nested.x[0] = vector;
        nested.x[1] = request.address;
        nested.x[30] = main_base + resume;
        if (!use_cpp_shovel_point_classifier) trace_shovel_dependency(0x15f3ed0, resume);
        call(nested, main_base + 0x15f3ed0);
        return static_cast<uint32_t>(nested.x[0]);
    }
};

void shovel_target_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + shovel_target_resume) {
        original_shovel_target(cpu);
        return;
    }
    const acnh::ShovelTargetRequest request{cpu.x[0]};
    std::array<uint32_t, 6> input;
    if (trace_shovel_target)
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = ld<uint32_t>(request.address + 4 * i);
    if (use_cpp_shovel_target) {
        GuestMemory memory;
        ShovelTargetServices services{cpu};
        cpu.x[0] = acnh::select_shovel_target(main_base, request, memory, services);
    } else {
        original_shovel_target(cpu);
    }
    if (trace_shovel_target) {
        const auto data = request.address;
        log("acnh shovel target %s entry=15f36e0 call=21f70d4 in=%x,%x,%x,%x,%x,%x originHeight=%x goal=%x,%x,%x selected=%x,%x,%x code=%x out=%x",
            use_cpp_shovel_target ? "cpp" : "original", input[0], input[1], input[2], input[3], input[4], input[5],
            ld<uint32_t>(data + 4), ld<uint32_t>(data + 0xc), ld<uint32_t>(data + 0x10), ld<uint32_t>(data + 0x14),
            ld<uint32_t>(data + 0x2c), ld<uint32_t>(data + 0x30), ld<uint32_t>(data + 0x34),
            ld<uint32_t>(data + 0x38), static_cast<unsigned>(cpu.x[0]));
    }
}

void shovel_layer_one_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + shovel_terrain_setup_resume) {
        original_shovel_layer_one(cpu);
        return;
    }
    const acnh::HalfTile point{ld<int32_t>(cpu.x[0] + 8), ld<int32_t>(cpu.x[0] + 12)};
    if (use_cpp_shovel_layer_one)
        cpu.x[0] = acnh::terrain_layer_one_has_material(main_base, point, GuestMemory{});
    else
        original_shovel_layer_one(cpu);
    if (trace_shovel_target)
        log("acnh shovel target %s entry=1284ee0 call=87b57c pos=%d,%d out=%x",
            use_cpp_shovel_layer_one ? "cpp" : "original", point.x, point.z, static_cast<unsigned>(cpu.x[0]));
}

struct ShovelTerrainSetupServices {
    const CPU& caller;

    uint32_t stage_hash(uint64_t object, uint32_t index) const {
        CPU nested = caller;
        nested.x[31] -= 0x30;
        nested.x[29] = nested.x[31];
        st<uint64_t>(nested.x[29], caller.x[29]);
        st<uint64_t>(nested.x[29] + 8, caller.x[30]);
        const auto target = ld<uint64_t>(ld<uint64_t>(object) + 0x18);
        nested.x[0] = object;
        nested.x[1] = index;
        nested.x[30] = main_base + 0x87b4c0;
        trace_shovel_dependency(target - main_base, 0x87b4c0);
        call(nested, target);
        nested.x[0] = ld<uint64_t>(nested.x[0]);
        nested.x[1] = 0;
        nested.x[30] = main_base + 0x87b4cc;
        call(nested, main_base + 0x5bd550);
        return static_cast<uint32_t>(nested.x[0]);
    }

    uint32_t test_material(acnh::HalfTileRef point, acnh::TerrainSetupRoute route) const {
        constexpr std::array targets{0x1285040u, 0x170cee0u, 0x1284ee0u};
        const auto target = targets[size_t(route)];
        CPU nested = caller;
        nested.x[0] = point.address;
        call(nested, main_base + target);
        return static_cast<uint32_t>(nested.x[0]);
    }
};

void shovel_terrain_setup_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + shovel_terrain_setup_resume) {
        original_shovel_terrain_setup(cpu);
        return;
    }
    const acnh::HalfTileRef point{cpu.x[0]};
    const auto x = ld<int32_t>(point.address + 8);
    const auto z = ld<int32_t>(point.address + 12);
    if (use_cpp_shovel_terrain_setup) {
        ShovelTerrainSetupServices services{cpu};
        cpu.x[0] = acnh::shovel_terrain_setup(main_base, point, GuestMemory{}, services);
    } else {
        original_shovel_terrain_setup(cpu);
    }
    if (trace_shovel_target)
        log("acnh shovel target %s entry=87b460 call=21f74fc pos=%d,%d out=%x",
            use_cpp_shovel_terrain_setup ? "cpp" : "original", x, z, static_cast<unsigned>(cpu.x[0]));
}

struct TerrainHelperServices {
    CPU cpu;
    uint64_t stack;

    TerrainHelperServices(const CPU& caller, uint64_t frame_size, uint64_t frame_offset)
        : cpu(caller), stack(caller.x[31] - frame_size) {
        cpu.x[31] = stack;
        cpu.x[29] = stack + frame_offset;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    void sample(acnh::TerrainGrid grid, uint64_t point, uint64_t output, uint64_t resume) const {
        CPU nested = cpu;
        nested.x[0] = grid.address;
        nested.x[1] = point;
        nested.x[2] = output;
        nested.x[30] = main_base + resume;
        call(nested, main_base + 0x862530);
    }

    float sample_height(acnh::TerrainGrid grid, acnh::HalfTile tile) const {
        const uint64_t point = stack + 0x40;
        st<uint64_t>(point, ld<uint64_t>(main_base + 0x4ab2010) + 0x10);
        st<int32_t>(point + 8, tile.x);
        st<int32_t>(point + 12, tile.z);
        sample(grid, point, stack + 8, 0x872114);
        return ld<float>(stack + 8);
    }

    std::array<uint8_t, 4> sample_kinds(acnh::TerrainGrid grid, acnh::HalfTile) const {
        sample(grid, cpu.x[1], stack + 0xc, 0x848ed4);
        return {ld<uint8_t>(stack + 0x3c), ld<uint8_t>(stack + 0x3d),
                ld<uint8_t>(stack + 0x3e), ld<uint8_t>(stack + 0x3f)};
    }

    uint32_t adjusted_kind(acnh::TerrainWorldPoint& point, uint64_t target, uint64_t resume) const {
        const uint64_t vector = stack + 0xc;
        st<float>(vector, point.x);
        st<float>(vector + 8, point.z);
        CPU nested = cpu;
        nested.x[0] = vector;
        nested.x[30] = main_base + resume;
        const bool secondary = target == 0x87aa70;
        const bool native = secondary ? use_cpp_terrain_secondary : use_cpp_terrain_primary;
        const auto before = point;
        const auto fallback = secondary ? acnh::TerrainFallback::secondary : acnh::TerrainFallback::primary;
        const auto kind = route_terrain_fallback(nested, fallback, native);
        point = {ld<float>(vector), ld<float>(vector + 8)};
        if (trace_terrain_fallbacks)
            log("acnh shovel terrain fallback %s entry=%llx call=%llx point=%x,%x after=%x,%x out=%x",
                native ? "cpp" : "original", static_cast<unsigned long long>(target),
                static_cast<unsigned long long>(resume - 4),
                std::bit_cast<uint32_t>(before.x), std::bit_cast<uint32_t>(before.z),
                std::bit_cast<uint32_t>(point.x), std::bit_cast<uint32_t>(point.z), kind);
        return kind;
    }

    uint32_t primary_kind(acnh::TerrainWorldPoint& point, size_t corner) const {
        return adjusted_kind(point, 0x87a980, terrain_primary_resumes[corner]);
    }

    uint32_t secondary_kind(acnh::TerrainWorldPoint& point, size_t corner) const {
        return adjusted_kind(point, 0x87aa70, terrain_secondary_resumes[corner]);
    }
};

struct ShovelAlternateTerrainServices : HeightGuestFrame {
    explicit ShovelAlternateTerrainServices(const CPU& caller) : HeightGuestFrame(caller, 0x90, 0x30) {}

    bool level(acnh::TerrainGrid grid, acnh::AlternateTerrainQuery query) const {
        return (invoke_child(main_base + 0x871f80, 0x87bf8c, {grid.address, query.rectangle, 0}).x[0] & 1) != 0;
    }

    bool layer_flag(acnh::HalfTile tile, uint32_t layer) const {
        const auto point = cpu.x[31] + 0x18;
        st<uint64_t>(point, ld<uint64_t>(main_base + 0x4ab2010) + 0x10);
        st<int32_t>(point + 8, tile.x);
        st<int32_t>(point + 12, tile.z);
        return (invoke_child(main_base + 0x1285fe0, 0x87c04c, {point, layer}).x[0] & 1) != 0;
    }

    bool property(uint64_t entry, uint64_t resume, uint32_t kind) const {
        const auto table = ld<uint64_t>(ld<uint64_t>(main_base + 0x4ab46c8));
        return (invoke_child(main_base + entry, resume, {table, kind}).x[0] & 1) != 0;
    }

    bool rejected(uint32_t kind, unsigned corner) const {
        constexpr std::array resumes{0x87c0c0u, 0x87c100u, 0x87c140u, 0x87c180u};
        return property(0x880630, resumes[corner], kind);
    }

    bool requirement(uint32_t kind, unsigned corner) const {
        constexpr std::array resumes{0x87c0f0u, 0x87c130u, 0x87c170u, 0x87c1b0u};
        return property(0x881090, resumes[corner], kind);
    }
};

Fn original_shovel_alternate_terrain = nullptr;

void shovel_alternate_terrain_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x1288bd4) {
        original_shovel_alternate_terrain(cpu);
        return;
    }
    const acnh::AlternateTerrainQuery query{cpu.x[0]};
    if (use_cpp_shovel_alternate_terrain) {
        ShovelAlternateTerrainServices services{cpu};
        cpu.x[0] = acnh::shovel_alternate_terrain(main_base, query, GuestMemory{}, services);
    } else {
        original_shovel_alternate_terrain(cpu);
    }
    if (trace_shovel_alternate_terrain)
        log("acnh shovel alternate terrain %s entry=87bf30 call=1288bd0 rect=%d,%d,%d,%d out=%x",
            use_cpp_shovel_alternate_terrain ? "cpp" : "original",
            ld<int32_t>(query.rectangle + 0x10), ld<int32_t>(query.rectangle + 0x14),
            ld<int32_t>(query.rectangle + 0x20), ld<int32_t>(query.rectangle + 0x24),
            unsigned(cpu.x[0]));
}


void terrain_level_adapter(CPU& cpu) {
    const bool alternate = cpu.x[30] == main_base + 0x87bf8c && is_shovel_alternate_terrain_frame(cpu.x[29]);
    if (!alternate && (cpu.x[30] != main_base + 0x877258 ||
        ld<uint64_t>(cpu.x[29] + 8) != main_base + shovel_terrain_caller)) {
        original_terrain_level(cpu);
        return;
    }

    const acnh::TerrainRect area{{ld<int32_t>(cpu.x[1] + 0x10), ld<int32_t>(cpu.x[1] + 0x14)},
                                {ld<int32_t>(cpu.x[1] + 0x20), ld<int32_t>(cpu.x[1] + 0x24)}};
    const auto mode = cpu.x[2] & 1 ? acnh::TerrainHeightMode::base_only : acnh::TerrainHeightMode::composed;
    const bool native = use_cpp_terrain_level && (!alternate || use_cpp_shovel_alternate_terrain);
    if (native) {
        GuestMemory memory;
        TerrainHelperServices services{cpu, 0xc0, 0x60};
        cpu.x[0] = acnh::terrain_is_level({cpu.x[0]}, area, mode, memory, services);
    } else {
        original_terrain_level(cpu);
    }

    if (alternate && trace_shovel_alternate_terrain) {
        log("acnh shovel alternate terrain %s entry=871f80 call=87bf88 rect=%d,%d,%d,%d out=%x",
            native ? "cpp" : "original", area.min.x, area.min.z, area.max.x, area.max.z, unsigned(cpu.x[0]));
    } else if (!alternate && trace_terrain_helpers && terrain_level_samples.fetch_add(1, std::memory_order_relaxed) < 8)
        log("acnh shovel terrain helper level %s area=%d,%d,%d,%d mode=%u out=%u",
            use_cpp_terrain_level ? "cpp" : "original", area.min.x, area.min.z, area.max.x, area.max.z,
            unsigned(mode), unsigned(cpu.x[0] & 1));
}

void terrain_materials_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x877330 ||
        ld<uint64_t>(cpu.x[29] + 8) != main_base + shovel_terrain_caller) {
        original_terrain_materials(cpu);
        return;
    }

    const acnh::HalfTile tile{ld<int32_t>(cpu.x[1] + 8), ld<int32_t>(cpu.x[1] + 12)};
    const acnh::TerrainQuadrants corners{{cpu.x[2], cpu.x[3], cpu.x[4], cpu.x[5]}};
    if (use_cpp_terrain_materials) {
        const acnh::TerrainGrid grid{tile.x >= 0 && tile.z >= 0 ? ld<uint64_t>(cpu.x[0] + 0x80) : 0};
        GuestMemory memory;
        TerrainHelperServices services{cpu, 0xb0, 0x50};
        cpu.x[0] = acnh::terrain_quadrant_materials(main_base, grid, tile, corners, memory, services);
    } else {
        original_terrain_materials(cpu);
    }

    if (trace_terrain_helpers && terrain_material_samples.fetch_add(1, std::memory_order_relaxed) < 16)
        log("acnh shovel terrain helper materials %s pos=%d,%d kinds=%x,%x,%x,%x out=%u",
            use_cpp_terrain_materials ? "cpp" : "original", tile.x, tile.z,
            ld<uint32_t>(corners.output[0]), ld<uint32_t>(corners.output[1]),
            ld<uint32_t>(corners.output[2]), ld<uint32_t>(corners.output[3]), unsigned(cpu.x[0] & 1));
}

Fn original_field_item_id = nullptr;
Fn original_auxiliary_item = nullptr;
bool use_cpp_field_item_id = false;
bool use_cpp_auxiliary_item = false;
bool trace_field_item = false;
std::atomic<unsigned> field_item_samples[3]{};
std::atomic<unsigned> auxiliary_item_samples{0};

bool shovel_item_caller(uint64_t caller) {
    return caller == 0x21f76b4 ||
        (caller >= 0x127ccbc && caller <= 0x127ce54) ||
        (caller >= 0x127d0a4 && caller <= 0x127d2ec);
}

void resolve_auxiliary_item(CPU& cpu, uint64_t item_caller) {
    const uint64_t caller = cpu.x[30] - main_base;
    const uint32_t kind = static_cast<uint32_t>(cpu.x[0]);
    const bool trace = trace_field_item && caller == 0x267d76c && shovel_item_caller(item_caller) &&
        auxiliary_item_samples.fetch_add(1, std::memory_order_relaxed) < 24;
    if (!use_cpp_auxiliary_item && !trace) {
        original_auxiliary_item(cpu);
        return;
    }

    std::string reads;
    const GuestMemory memory{trace ? &reads : nullptr};
    const uint32_t result = acnh::FieldQueries{main_base, memory}.auxiliary_item_index(kind);
    if (use_cpp_auxiliary_item) cpu.x[0] = result;
    else original_auxiliary_item(cpu);
    if (trace)
        log("acnh shovel item %s entry=1a03610 caller=%llx base=%llx arg=%x out=%llx reads=%s",
            use_cpp_auxiliary_item ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(main_base), kind,
            static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
}

void auxiliary_item_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t item_caller = trace_field_item && caller == 0x267d76c
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    resolve_auxiliary_item(cpu, item_caller);
}

void field_item_id_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const size_t sample = caller == 0x21f76b4 ? 0 : caller < 0x127d0a4 ? 1 : 2;
    const bool trace = trace_field_item && shovel_item_caller(caller) &&
        field_item_samples[sample].fetch_add(1, std::memory_order_relaxed) < 16;
    const acnh::FieldItem item{cpu.x[0]};
    std::string reads;
    if (trace) {
        const GuestMemory snapshot{&reads};
        acnh::FieldQueries{main_base, snapshot}.item_id(item);
    }

    if (use_cpp_field_item_id) {
        const GuestMemory memory;
        cpu.x[0] = acnh::FieldQueries{main_base, memory}.item_id(item, [&](uint32_t kind) {
            CPU query = cpu;
            query.x[0] = kind;
            query.x[30] = main_base + 0x267d76c;
            resolve_auxiliary_item(query, caller);
            return static_cast<uint32_t>(query.x[0]);
        });
    } else {
        original_field_item_id(cpu);
    }

    if (trace)
        log("acnh shovel item %s entry=267d6c4 caller=%llx base=%llx arg=%llx out=%llx reads=%s",
            use_cpp_field_item_id ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(item.address),
            static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
}

Fn original_field_bounds = nullptr;
bool use_cpp_field_bounds = false;
bool trace_field_bounds = false;
std::atomic<unsigned> field_bounds_samples{0};

bool shovel_cell_caller(uint64_t caller) {
    return caller == 0x127cc88 || caller == 0x127cf88 ||
           caller == 0x127cfd4 || caller == 0x127cffc;
}

bool provider_read_frame(uint64_t frame) {
    return ld<uint64_t>(frame + 8) == main_base + 0x1950414 &&
           shovel_cell_caller(ld<uint64_t>(ld<uint64_t>(frame) + 8) - main_base);
}

bool provider_acquire_frame(uint64_t frame) {
    return ld<uint64_t>(frame + 8) == main_base + 0x11e51b8 && provider_read_frame(ld<uint64_t>(frame));
}

void field_bounds_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t parent = trace_field_bounds && caller == 0x1950370
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const bool trace = shovel_cell_caller(parent) &&
        field_bounds_samples.fetch_add(1, std::memory_order_relaxed) < 32;
    const auto frame = cpu.x[29];
    const bool record_trace = trace_shovel_record_leaves && caller == 0x1951938 && frame
        && is_shovel_stage_record_call(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame));
    if (!use_cpp_field_bounds && !trace && !record_trace) {
        original_field_bounds(cpu);
        return;
    }

    const uint64_t layer_address = cpu.x[0];
    const acnh::FieldChannel channel{static_cast<uint32_t>(cpu.x[1])};
    const uint64_t position = cpu.x[2];
    std::string reads;
    const GuestMemory memory{trace ? &reads : nullptr};
    const acnh::FieldQueries fields{main_base, memory};
    const acnh::FieldLayer layer{memory.read<uint32_t>(layer_address + 8)};
    const auto bounds = fields.bounds_for(layer, channel);
    const bool inside = bounds && fields.contains(bounds, {position});
    if (use_cpp_field_bounds) cpu.x[0] = inside;
    else original_field_bounds(cpu);
    if (record_trace)
        log("acnh shovel record leaf %s entry=194f798 call=1951934 table=%x channel=%x pos=%d,%d out=%x",
            use_cpp_field_bounds ? "cpp" : "original", layer.index, channel.index,
            ld<int32_t>(position + 8), ld<int32_t>(position + 12), unsigned(cpu.x[0]));
    if (trace)
        log("acnh shovel bounds %s caller=%llx parent=%llx base=%llx layer=%llx channel=%x position=%llx out=%llx reads=%s",
            use_cpp_field_bounds ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(parent), static_cast<unsigned long long>(main_base),
            static_cast<unsigned long long>(layer_address), channel.index, static_cast<unsigned long long>(position),
            static_cast<unsigned long long>(cpu.x[0]), reads.c_str());
}

struct FieldCellServices {
    CPU cpu;

    explicit FieldCellServices(const CPU& caller) : cpu(caller) {
        cpu.x[31] -= 0x50;
        const uint64_t frame = cpu.x[31] + 0x10;
        st<uint64_t>(frame, caller.x[29]);
        st<uint64_t>(frame + 8, caller.x[30]);
        cpu.x[29] = frame;
    }

    bool contains(acnh::FieldCellQuery query) const {
        CPU nested = cpu;
        nested.x[0] = query.layer.address;
        nested.x[1] = query.channel.index;
        nested.x[2] = query.position.address;
        nested.x[30] = main_base + 0x1950370;
        call(nested, main_base + 0x194f798);
        return (nested.x[0] & 1) != 0;
    }

    void initialize(acnh::FieldItem item) const {
        CPU nested = cpu;
        nested.x[0] = item.address;
        nested.x[30] = main_base + 0x1950384;
        call(nested, main_base + 0x267d4ac);
    }

    uint64_t read(acnh::FieldCellProvider provider, acnh::HalfTileRef point, acnh::HalfTileRef root) const {
        CPU nested = cpu;
        const uint64_t result = cpu.x[31] + 0x28;
        nested.x[0] = provider.address;
        nested.x[1] = point.address;
        nested.x[2] = root.address;
        nested.x[8] = result;
        nested.x[30] = main_base + 0x1950414;
        call(nested, main_base + 0x11e4f58);
        return ld<uint64_t>(result);
    }
};

Fn original_field_cell = nullptr;
bool use_cpp_field_cell = false;
bool trace_field_cell = false;
std::atomic<unsigned> field_cell_samples{0};

void field_cell_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    if (!shovel_cell_caller(caller)) {
        original_field_cell(cpu);
        return;
    }

    const acnh::FieldCellQuery query{{cpu.x[0]}, {static_cast<uint32_t>(cpu.x[1])}, {cpu.x[2]}};
    const acnh::HalfTileRef root{cpu.x[3]};
    const acnh::FieldItem item{cpu.x[8]};
    const bool trace = trace_field_cell && field_cell_samples.fetch_add(1, std::memory_order_relaxed) < 32;
    uint32_t layer = 0;
    int32_t x = 0, z = 0, root_x = 0, root_z = 0;
    if (trace) {
        layer = ld<uint32_t>(query.layer.address + 8);
        x = ld<int32_t>(query.position.address + 8);
        z = ld<int32_t>(query.position.address + 12);
        if (root.address) {
            root_x = ld<int32_t>(root.address + 8);
            root_z = ld<int32_t>(root.address + 12);
        }
    }

    if (use_cpp_field_cell) {
        GuestMemory memory;
        FieldCellServices services{cpu};
        acnh::FieldQueries{main_base, memory}.read_cell(query, item, root, services);
    } else {
        original_field_cell(cpu);
    }

    if (trace)
        log("acnh shovel cell %s caller=%llx layer=%x channel=%x pos=%d,%d root_present=%u root=%d,%d resolved=%d,%d item=%llx",
            use_cpp_field_cell ? "cpp" : "original", static_cast<unsigned long long>(caller),
            layer, query.channel.index, x, z, unsigned(root.address != 0), root_x, root_z,
            root.address ? ld<int32_t>(root.address + 8) : 0,
            root.address ? ld<int32_t>(root.address + 12) : 0,
            ld<unsigned long long>(item.address));
}

template<size_t Index>
void provider_method_adapter(CPU& cpu);

struct ProviderMethodRoute {
    uint64_t entry;
    Fn adapter;
    Fn original = nullptr;
    std::atomic<unsigned> samples{0};
};

std::array<ProviderMethodRoute, 5> provider_methods{{
    {0x11ac1e8, provider_method_adapter<0>},
    {0x11ac290, provider_method_adapter<1>},
    {0x261157c, provider_method_adapter<2>},
    {0x2611584, provider_method_adapter<3>},
    {0x261158c, provider_method_adapter<4>},
}};
bool use_cpp_provider_methods = false;
bool trace_provider_methods = false;

template<size_t Index>
void provider_method_adapter(CPU& cpu) {
    auto& route = provider_methods[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    if (caller < 0x11e4fa8 || caller > 0x11e51d0 ||
        !provider_read_frame(cpu.x[29])) {
        route.original(cpu);
        return;
    }

    const uint64_t object = cpu.x[0];
    const acnh::ProviderCell cell{static_cast<uint32_t>(cpu.x[1]), static_cast<uint32_t>(cpu.x[2])};
    const bool trace = trace_provider_methods && route.samples.fetch_add(1, std::memory_order_relaxed) < 12;
    uint64_t data = 0;
    if (trace) {
        if constexpr (Index == 0) data = ld<uint64_t>(object + 8);
        if constexpr (Index == 4) data = ld<uint64_t>(object + 0x10);
    }

    if (use_cpp_provider_methods) {
        const GuestMemory memory;
        if constexpr (Index == 0) {
            cpu.x[0] = acnh::provider_table({object}, memory, [&](uint64_t target) {
                cpu.x[0] = target;
                call(cpu, target);
                return acnh::ProviderTable{cpu.x[0]};
            }).address;
        } else if constexpr (Index == 1) {
            cpu.x[0] = acnh::provider_ready();
        } else if constexpr (Index == 2) {
            cpu.x[0] = acnh::provider_width();
        } else if constexpr (Index == 3) {
            cpu.x[0] = acnh::provider_height();
        } else {
            cpu.x[0] = acnh::provider_cell_offset({object}, cell, memory);
        }
    } else {
        route.original(cpu);
    }

    if (trace)
        log("acnh shovel provider method %s entry=%llx caller=%llx base=%llx object=%llx x=%x z=%x data=%llx out=%llx",
            use_cpp_provider_methods ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
            static_cast<unsigned long long>(caller), static_cast<unsigned long long>(main_base),
            static_cast<unsigned long long>(object), cell.x, cell.z,
            static_cast<unsigned long long>(data), static_cast<unsigned long long>(cpu.x[0]));
}

struct ProviderReadServices {
    const CPU& caller;
    CPU cpu;
    uint64_t stack;

    explicit ProviderReadServices(const CPU& source)
        : caller(source), cpu(source), stack(source.x[31] - 0x90) {
        cpu.x[31] = stack;
        cpu.x[29] = stack + 0x30;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    uint64_t invoke(uint64_t object, uint64_t slot, uint64_t resume, acnh::ProviderCell cell = {}) const {
        CPU nested = cpu;
        nested.x[0] = object;
        nested.x[1] = cell.x;
        nested.x[2] = cell.z;
        nested.x[30] = main_base + resume;
        const auto target = ld<uint64_t>(ld<uint64_t>(object) + slot);
        nested.x[8] = target;
        call(nested, target);
        return nested.x[0];
    }

    acnh::ProviderTable table(acnh::ProviderReader reader, acnh::ProviderTableUse use) const {
        constexpr std::array resumes{0x11e4fa8, 0x11e4fd0, 0x11e5008, 0x11e50a4, 0x11e5170, 0x11e5194};
        return {invoke(reader.address, 0, resumes[static_cast<size_t>(use)])};
    }

    uint32_t width(acnh::ProviderTable table, acnh::ProviderDimensionUse use) const {
        constexpr std::array resumes{0x11e4fb4, 0x11e50b4, 0x11e517c};
        return static_cast<uint32_t>(invoke(table.address, 0x18, resumes[static_cast<size_t>(use)]));
    }

    uint32_t height(acnh::ProviderTable table, acnh::ProviderDimensionUse use) const {
        constexpr std::array resumes{0x11e4fdc, 0x11e50c8, 0x11e51a0};
        return static_cast<uint32_t>(invoke(table.address, 0x20, resumes[static_cast<size_t>(use)]));
    }

    bool ready(acnh::ProviderReader reader) const {
        return (invoke(reader.address, 0x10, 0x11e4ff4) & 1) != 0;
    }

    bool release_required(acnh::ProviderContext context) const {
        return (invoke(context.address, 0, 0x11e50f4) & 1) != 0;
    }

    uint64_t offset(acnh::ProviderTable table, acnh::ProviderCell cell, bool anchor) const {
        return invoke(table.address, 0x28, anchor ? 0x11e51d0 : 0x11e5118, cell);
    }

    acnh::ProviderAnchor resolve_anchor(acnh::FieldCellProvider provider, acnh::ProviderCell position) const {
        const uint64_t point = stack + 0x20;
        st<uint64_t>(point, main_base + 0x488d600);
        st<uint32_t>(point + 8, position.x);
        st<uint32_t>(point + 12, position.z);
        CPU nested = cpu;
        nested.x[0] = provider.address;
        nested.x[1] = point;
        nested.x[8] = stack;
        nested.x[30] = main_base + 0x11e51b8;
        call(nested, main_base + 0x11e4578);
        return {{ld<uint64_t>(stack + 8)}, {ld<uint32_t>(stack + 0x14), ld<uint32_t>(stack + 0x18)}};
    }

    void cleanup_anchor() const {
        CPU nested = cpu;
        nested.x[0] = stack;
        nested.x[30] = main_base + 0x11e5200;
        call(nested, main_base + 0x2540184);
    }

    void release(acnh::ProviderContext context) const {
        CPU nested = caller;
        nested.x[0] = context.address;
        nested.x[8] = ld<uint64_t>(context.address);
        nested.x[1] = ld<uint64_t>(nested.x[8]);
        call(nested, nested.x[1]);
    }
};

Fn original_provider_cell = nullptr;
bool use_cpp_provider_cell = false;
bool trace_provider_cell = false;
std::atomic<unsigned> provider_cell_samples{0};

void provider_cell_adapter(CPU& cpu) {
    const uint64_t parent = cpu.x[30] == main_base + 0x1950414
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    if (!shovel_cell_caller(parent)) {
        original_provider_cell(cpu);
        return;
    }

    const acnh::ProviderQuery query{{cpu.x[0]}, {cpu.x[1]}, {cpu.x[2]}, {cpu.x[8]}};
    const bool trace = trace_provider_cell && provider_cell_samples.fetch_add(1, std::memory_order_relaxed) < 24;
    int32_t x = 0, z = 0, root_x = 0, root_z = 0;
    uint32_t counter = 0;
    if (trace) {
        x = ld<int32_t>(query.point.address + 8);
        z = ld<int32_t>(query.point.address + 12);
        counter = ld<uint32_t>(query.provider.address + 0x1c4);
        if (query.root.address) {
            root_x = ld<int32_t>(query.root.address + 8);
            root_z = ld<int32_t>(query.root.address + 12);
        }
    }

    if (use_cpp_provider_cell) {
        GuestMemory memory;
        ProviderReadServices services{cpu};
        acnh::read_provider_cell(main_base, query, memory, services);
    } else {
        original_provider_cell(cpu);
    }

    if (trace)
        log("acnh shovel provider cell %s parent=%llx pos=%d,%d root_present=%u root=%d,%d resolved=%d,%d item=%llx counter=%x,%x",
            use_cpp_provider_cell ? "cpp" : "original", static_cast<unsigned long long>(parent),
            x, z, unsigned(query.root.address != 0), root_x, root_z,
            query.root.address ? ld<int32_t>(query.root.address + 8) : 0,
            query.root.address ? ld<int32_t>(query.root.address + 12) : 0,
            ld<unsigned long long>(query.output.address), counter, ld<uint32_t>(query.provider.address + 0x1c4));
}

constexpr std::array<uint64_t, 5> shovel_item_class_callers{
    0x21f77b0, 0x21f77cc, 0x21f77e0, 0x21f7948, 0x21f7bfc,
};

bool shovel_item_class_caller(uint64_t caller) {
    for (const uint64_t site : shovel_item_class_callers)
        if (caller == site) return true;
    return false;
}

template<size_t Index>
struct ShovelItemRuntimeServices {
    CPU cpu;
    uint64_t stack;

    explicit ShovelItemRuntimeServices(const CPU& caller) : cpu(caller) {
        constexpr std::array frame_sizes{0x40, 0x60, 0x50};
        stack = caller.x[31] - frame_sizes[Index];
        cpu.x[31] = stack;
        cpu.x[29] = stack + (Index == 0 ? 0x10 : 0x20);
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
    }

    uint32_t invoke(uint64_t target, uint64_t resume, uint64_t object, uint64_t arg = 0) const {
        CPU nested = cpu;
        nested.x[0] = object;
        nested.x[1] = arg;
        nested.x[30] = main_base + resume;
        call(nested, main_base + target);
        return static_cast<uint32_t>(nested.x[0]);
    }

    uint32_t item_id(acnh::FieldItem item, acnh::ShovelItemIdUse use) const {
        constexpr uint64_t resumes[3][6]{
            {0x267da8c, 0x267dac4, 0, 0x267db30, 0, 0},
            {0, 0x267e050, 0x267e0c4, 0, 0x267e0f0, 0},
            {0x127d1dc, 0x127d0a4, 0x127d104, 0, 0x127d150, 0x127d2ec},
        };
        return invoke(0x267d6c4, resumes[Index][static_cast<size_t>(use)], item.address);
    }

    acnh::ItemDescriptor copy_alternate(acnh::FieldItem item) const {
        const uint64_t descriptor = stack + 8;
        st<uint64_t>(descriptor, main_base + 0x4936b50);
        st<uint64_t>(descriptor + 8, ld<uint64_t>(item.address));
        st<uint64_t>(descriptor + 0x10, 0);
        return {descriptor};
    }

    acnh::FieldItem copy_special(acnh::FieldItem item, uint16_t id) const {
        st<uint64_t>(stack, ld<uint64_t>(main_base + 0x4ac35c0) + 0x10);
        st<uint16_t>(stack + 8, id);
        st<uint16_t>(stack + 0xa, ld<uint16_t>(item.address + 2));
        st<uint32_t>(stack + 0xc, ld<uint32_t>(item.address + 4));
        return {stack + 8};
    }

    acnh::FieldItem copy_fallback(acnh::FieldItem item) const {
        st<uint64_t>(stack + 8, ld<uint64_t>(main_base + 0x4ac35c0) + 0x10);
        st<uint64_t>(stack + 0x10, ld<uint64_t>(item.address));
        return {stack + 0x10};
    }

    bool recognized(acnh::FieldItem item) const {
        return (invoke(0x267da10, 0x127d078, item.address) & 1) != 0;
    }

    bool blocked_by_alternate(acnh::FieldItem item) const {
        return (invoke(0x267e024, 0x127d084, item.address) & 1) != 0;
    }

    bool metadata_enabled(acnh::ItemDescriptor descriptor) const {
        constexpr uint64_t resume = Index == 1 ? 0x267e120 : 0x127d180;
        return (invoke(0x19e59b0, resume, descriptor.address) & 1) != 0;
    }

    bool metadata_suitable(acnh::ItemDescriptor descriptor) const {
        return (invoke(0x19e6020, 0x127d18c, descriptor.address) & 1) != 0;
    }

    acnh::ItemDescriptor describe(acnh::FieldItem item) const {
        const uint64_t descriptor = stack + 8;
        invoke(0x114c030, 0x127d20c, descriptor, item.address);
        return {descriptor};
    }

    bool has_category(acnh::ItemDescriptor descriptor) const {
        return (invoke(0x114dcb0, 0x127d214, descriptor.address) & 1) != 0;
    }

    bool category_flag(acnh::CatalogEntry row, uint32_t bit) const {
        return (invoke(0x113d890, 0x127d224, row.address, bit) & 1) != 0;
    }

    uint32_t category(acnh::FieldItem item, acnh::CatalogEntry row, size_t call_index) const {
        constexpr std::array resumes{0x127d23c, 0x127d254, 0x127d268, 0x127d280, 0x127d298, 0x127d2ac, 0x127d2c4};
        return invoke(0x113db50, resumes[call_index], item.address, row.address);
    }
};

template<size_t Index>
void shovel_item_query_adapter(CPU& cpu);

struct ShovelItemRoute {
    uint64_t entry;
    Fn adapter;
    Fn original = nullptr;
    std::atomic<unsigned> samples{0};
};

std::array<ShovelItemRoute, 3> shovel_item_queries{{
    {0x267da10, shovel_item_query_adapter<0>},
    {0x267e024, shovel_item_query_adapter<1>},
    {0x127d05c, shovel_item_query_adapter<2>},
}};
bool use_cpp_shovel_item_queries = false;
bool trace_shovel_item_queries = false;

template<size_t Index>
void shovel_item_query_adapter(CPU& cpu) {
    auto& route = shovel_item_queries[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    bool on_shovel_path;
    if constexpr (Index == 2) {
        on_shovel_path = shovel_item_class_caller(caller);
    } else {
        constexpr uint64_t call_site = Index == 0 ? 0x127d078 : 0x127d084;
        on_shovel_path = caller == call_site && shovel_item_class_caller(ld<uint64_t>(cpu.x[29] + 8) - main_base);
    }
    if (!on_shovel_path) {
        route.original(cpu);
        return;
    }

    const acnh::FieldItem item{cpu.x[0]};
    const bool trace = trace_shovel_item_queries && route.samples.fetch_add(1, std::memory_order_relaxed) < 48;
    const auto before = trace ? ld<unsigned long long>(item.address) : 0;
    if (use_cpp_shovel_item_queries) {
        GuestMemory memory;
        ShovelItemRuntimeServices<Index> services{cpu};
        const acnh::ShovelItemQueries queries{main_base, memory};
        if constexpr (Index == 0) cpu.x[0] = queries.recognized(item, services);
        if constexpr (Index == 1) cpu.x[0] = queries.blocked_by_alternate(item, services);
        if constexpr (Index == 2) cpu.x[0] = queries.suitable(item, services);
    } else {
        route.original(cpu);
    }
    if (trace)
        log("acnh shovel item class %s entry=%llx caller=%llx before=%llx after=%llx out=%u",
            use_cpp_shovel_item_queries ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
            static_cast<unsigned long long>(caller), before, ld<unsigned long long>(item.address),
            unsigned(cpu.x[0] & 1));
}

template<size_t Index>
void shovel_stage_adapter(CPU& cpu);

struct ShovelStageRoute {
    uint64_t entry;
    Fn adapter;
    Fn original = nullptr;
    std::atomic<unsigned> samples{0};
};

std::array<ShovelStageRoute, 3> shovel_stage_routes{{
    {0x16f37d0, shovel_stage_adapter<0>},
    {0x16f3a18, shovel_stage_adapter<1>},
    {0x16baf44, shovel_stage_adapter<2>},
}};
bool use_cpp_shovel_stage = false;
bool trace_shovel_stage = false;

struct ShovelStageServices {
    CPU cpu;

    explicit ShovelStageServices(const CPU& caller) : cpu(caller) {
        cpu.x[31] -= 0x20;
        st<uint64_t>(cpu.x[31], caller.x[29]);
        st<uint64_t>(cpu.x[31] + 8, caller.x[30]);
        cpu.x[29] = cpu.x[31];
    }

    bool invoke(acnh::ShovelStageKey key, uint64_t entry, uint64_t resume) const {
        CPU nested = cpu;
        nested.x[0] = key.hash;
        nested.x[30] = main_base + resume;
        call(nested, main_base + entry);
        return (nested.x[0] & 1) != 0;
    }

    bool primary(acnh::ShovelStageKey key) const { return invoke(key, 0x16f37d0, 0x16baf58); }
    bool secondary(acnh::ShovelStageKey key) const { return invoke(key, 0x16f3a18, 0x16bb510); }
};

template<size_t Index>
void shovel_stage_adapter(CPU& cpu) {
    auto& route = shovel_stage_routes[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    bool on_shovel_path;
    if constexpr (Index == 2) {
        on_shovel_path = caller == 0x127cb6c && ld<uint64_t>(cpu.x[29] + 8) == main_base + 0x21f7618;
    } else {
        constexpr uint64_t resume = Index == 0 ? 0x16baf58 : 0x16bb510;
        const uint64_t frame = cpu.x[29];
        on_shovel_path = caller == resume && ld<uint64_t>(frame + 8) == main_base + 0x127cb6c &&
                         ld<uint64_t>(ld<uint64_t>(frame) + 8) == main_base + 0x21f7618;
    }

    if (!on_shovel_path) {
        route.original(cpu);
        return;
    }

    const acnh::ShovelStageKey key{static_cast<uint32_t>(cpu.x[0])};
    if (use_cpp_shovel_stage) {
        if constexpr (Index == 0) cpu.x[0] = acnh::shovel_stage_primary(key);
        if constexpr (Index == 1) cpu.x[0] = acnh::shovel_stage_secondary(key);
        if constexpr (Index == 2) {
            ShovelStageServices services{cpu};
            cpu.x[0] = acnh::shovel_stage_matches(key, services);
        }
    } else {
        route.original(cpu);
    }

    if (trace_shovel_stage && route.samples.fetch_add(1, std::memory_order_relaxed) < 24)
        log("acnh shovel stage %s entry=%llx key=%x out=%u",
            use_cpp_shovel_stage ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
            key.hash, unsigned(cpu.x[0] & 1));
}

template<size_t Index>
void provider_anchor_adapter(CPU& cpu);

struct ProviderAnchorRoute {
    uint64_t entry;
    Fn adapter;
    Fn original = nullptr;
    std::atomic<unsigned> samples{0};
};

std::array<ProviderAnchorRoute, 2> provider_anchor_routes{{
    {0x11e4578, provider_anchor_adapter<0>},
    {0x2540184, provider_anchor_adapter<1>},
}};
bool use_cpp_provider_anchor = false;
bool trace_provider_anchor = false;

struct ProviderAnchorServices {
    CPU cpu;

    explicit ProviderAnchorServices(const CPU& source) : cpu(source) {
        cpu.x[31] -= 0x40;
        st<uint64_t>(cpu.x[31], source.x[29]);
        st<uint64_t>(cpu.x[31] + 8, source.x[30]);
        cpu.x[29] = cpu.x[31];
    }

    uint64_t invoke(uint64_t object, uint64_t slot, uint64_t resume) const {
        CPU nested = cpu;
        nested.x[0] = object;
        nested.x[30] = main_base + resume;
        nested.x[8] = ld<uint64_t>(ld<uint64_t>(object) + slot);
        call(nested, nested.x[8]);
        return nested.x[0];
    }

    acnh::ProviderTable table(acnh::ProviderReader reader) const {
        return {invoke(reader.address, 0, 0x11e45a0)};
    }

    uint32_t width(acnh::ProviderTable table) const {
        return static_cast<uint32_t>(invoke(table.address, 0x18, 0x11e45b0));
    }

    uint32_t height(acnh::ProviderTable table) const {
        return static_cast<uint32_t>(invoke(table.address, 0x20, 0x11e45c4));
    }

    bool begin(acnh::ProviderContext context) const {
        return (invoke(context.address, 0, 0x11e45f0) & 1) != 0;
    }
};

template<size_t Index>
void provider_anchor_adapter(CPU& cpu) {
    auto& route = provider_anchor_routes[Index];
    constexpr uint64_t resume = Index == 0 ? 0x11e51b8 : 0x11e5200;
    const uint64_t frame = cpu.x[29];
    if (cpu.x[30] != main_base + resume || !provider_read_frame(frame)) {
        route.original(cpu);
        return;
    }

    acnh::ProviderCell position{};
    const acnh::ProviderAnchorLease lease{Index == 0 ? cpu.x[8] : cpu.x[0]};
    if constexpr (Index == 0)
        position = {ld<uint32_t>(cpu.x[1] + 8), ld<uint32_t>(cpu.x[1] + 12)};

    const bool trace = trace_provider_anchor && route.samples.fetch_add(1, std::memory_order_relaxed) < 16;
    if (use_cpp_provider_anchor) {
        GuestMemory memory;
        if constexpr (Index == 0) {
            ProviderAnchorServices services{cpu};
            acnh::acquire_provider_anchor(main_base, {cpu.x[0]}, position, lease, memory, services);
        } else {
            acnh::release_provider_anchor(main_base, lease, memory, [&](acnh::ProviderContext context) {
                cpu.x[0] = context.address;
                cpu.x[8] = ld<uint64_t>(context.address);
                cpu.x[1] = ld<uint64_t>(cpu.x[8]);
                call(cpu, cpu.x[1]);
            });
        }
    } else {
        route.original(cpu);
    }

    if (trace)
        log("acnh shovel anchor %s entry=%llx state=pos=%x,%x type=%llx table=%u tags=%x,%x out=%x,%x",
            use_cpp_provider_anchor ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
            position.x, position.z, static_cast<unsigned long long>(ld<uint64_t>(lease.address) - main_base),
            unsigned(ld<uint64_t>(lease.address + 8) != 0), unsigned(ld<uint8_t>(lease.address + 0x10)),
            unsigned(ld<uint8_t>(lease.address + 0x11)), ld<uint32_t>(lease.address + 0x14),
            ld<uint32_t>(lease.address + 0x18));
}

unsigned shovel_item_init_scope(uint64_t caller, uint64_t frame) {
    if (caller == 0x1950384)
        return shovel_cell_caller(ld<uint64_t>(frame + 8) - main_base) ? 2 : 0;
    if (caller == 0x21f7574) return 1;
    constexpr uint64_t first = 0x127c540;
    constexpr uint64_t last = first + 34 * 12;
    if (caller >= first && caller <= last && (caller - first) % 12 == 0 &&
        ld<uint64_t>(frame + 8) == main_base + 0x21f7574)
        return 1;
    return 0;
}

Fn original_empty_item_init = nullptr;
Fn original_empty_item_finalize = nullptr;
bool use_cpp_empty_item_init = false;
bool trace_empty_item_init = false;
std::array<std::atomic<unsigned>, 2> empty_item_init_counts{};
std::array<std::atomic<unsigned>, 2> empty_item_finalize_counts{};

void log_empty_item_init(bool native, uint64_t entry, uint64_t caller, uint64_t before, acnh::FieldItem item) {
    log("acnh shovel item init %s entry=%llx caller=%llx before=%llx after=%llx",
        native ? "cpp" : "original", static_cast<unsigned long long>(entry), static_cast<unsigned long long>(caller),
        static_cast<unsigned long long>(before), ld<unsigned long long>(item.address));
}

void empty_item_init_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const unsigned scope = shovel_item_init_scope(caller, cpu.x[29]);
    if (!scope) {
        original_empty_item_init(cpu);
        return;
    }

    const acnh::FieldItem item{cpu.x[0]};
    const bool trace = trace_empty_item_init &&
        empty_item_init_counts[scope - 1].fetch_add(1, std::memory_order_relaxed) < 40;
    const uint64_t before = trace ? ld<uint64_t>(item.address) : 0;
    GuestMemory memory;
    const bool native = use_cpp_empty_item_init && acnh::initialize_empty_field_item(main_base, item, memory,
        [&](acnh::FieldItem initialized) {
            CPU nested = cpu;
            nested.x[31] -= 0x60;
            nested.x[29] = nested.x[31] + 0x20;
            st<uint64_t>(nested.x[29], cpu.x[29]);
            st<uint64_t>(nested.x[29] + 8, cpu.x[30]);
            nested.x[0] = initialized.address;
            nested.x[30] = main_base + 0x267d6ac;
            call(nested, main_base + 0x267f248);
        });
    if (!native) original_empty_item_init(cpu);
    if (trace) log_empty_item_init(native, 0x267d4ac, caller, before, item);
}

void empty_item_finalize_adapter(CPU& cpu) {
    const uint64_t frame = cpu.x[29];
    if (cpu.x[30] != main_base + 0x267d6ac) {
        original_empty_item_finalize(cpu);
        return;
    }

    const uint64_t parent = ld<uint64_t>(frame + 8) - main_base;
    const unsigned scope = shovel_item_init_scope(parent, ld<uint64_t>(frame));
    if (!scope) {
        original_empty_item_finalize(cpu);
        return;
    }

    const acnh::FieldItem item{cpu.x[0]};
    const bool trace = trace_empty_item_init &&
        empty_item_finalize_counts[scope - 1].fetch_add(1, std::memory_order_relaxed) < 40;
    const uint64_t before = trace ? ld<uint64_t>(item.address) : 0;
    const bool native = use_cpp_empty_item_init && acnh::empty_field_item_finalized(item, GuestMemory{});
    if (!native) original_empty_item_finalize(cpu);
    if (trace) log_empty_item_init(native, 0x267f248, parent, before, item);
}

Fn original_shovel_metadata = nullptr;
bool use_cpp_shovel_metadata = false;
bool trace_shovel_metadata = false;
std::atomic<unsigned> shovel_metadata_samples{0};

bool use_cpp_shovel_alternate_kind = false;
bool use_cpp_shovel_metadata_predicate = false;
bool trace_shovel_metadata_leaves = false;

struct ShovelAlternateKindCallbacks {
    const CPU& caller;

    bool has_special_kind(acnh::ItemDescriptor descriptor) const {
        CPU nested = caller;
        nested.x[31] -= 0x20;
        nested.x[0] = descriptor.address;
        nested.x[30] = main_base + 0x19f9764;
        st<uint64_t>(nested.x[31] + 0x10, caller.x[29]);
        st<uint64_t>(nested.x[31] + 0x18, main_base + 0x19ee428);
        nested.x[29] = nested.x[31] + 0x10;
        trace_shovel_dependency(0x19f4c7c, 0x19f9764);
        call(nested, main_base + 0x19f4c7c);
        return (nested.x[0] & 1) != 0;
    }
};

void log_shovel_metadata_leaf(uint64_t entry, uint64_t resume, bool native,
                              acnh::ItemDescriptor descriptor, uint32_t result) {
    if (!trace_shovel_metadata_leaves) return;
    log("acnh shovel metadata leaf %s entry=%llx call=%llx item=%llx row=%u out=%x",
        native ? "cpp" : "original", static_cast<unsigned long long>(entry),
        static_cast<unsigned long long>(resume - 4),
        static_cast<unsigned long long>(ld<uint64_t>(descriptor.address + 8)),
        unsigned(ld<uint64_t>(descriptor.address + 0x10) != 0), result);
}

struct ShovelMetadataServices {
    CPU cpu;

    explicit ShovelMetadataServices(const CPU& caller) : cpu(caller) {
        cpu.x[31] -= 0x30;
        st<uint64_t>(cpu.x[31], caller.x[29]);
        st<uint64_t>(cpu.x[31] + 8, caller.x[30]);
        cpu.x[29] = cpu.x[31];
    }

    uint64_t invoke(uint64_t entry, uint64_t resume, uint64_t address) const {
        CPU nested = cpu;
        nested.x[0] = address;
        nested.x[30] = main_base + resume;
        if (entry != 0x267d6c4) trace_shovel_dependency(entry, resume);
        call(nested, main_base + entry);
        return nested.x[0];
    }

    uint32_t item_id(acnh::FieldItem item) const {
        return static_cast<uint32_t>(invoke(0x267d6c4, 0x19ee3e4, item.address));
    }

    uint32_t alternate_kind(acnh::ItemDescriptor descriptor) const {
        constexpr uint64_t entry = 0x19f93e8, resume = 0x19ee428;
        ShovelAlternateKindCallbacks callbacks{cpu};
        const auto result = use_cpp_shovel_alternate_kind
            ? acnh::shovel_alternate_kind(main_base, descriptor, GuestMemory{}, callbacks)
            : static_cast<uint32_t>(invoke(entry, resume, descriptor.address));
        log_shovel_metadata_leaf(entry, resume, use_cpp_shovel_alternate_kind, descriptor, result);
        return result;
    }

    bool metadata_enabled(acnh::ItemDescriptor descriptor, size_t path) const {
        constexpr std::array resumes{0x19ee444, 0x19ee450, 0x19ee470};
        constexpr uint64_t entry = 0x19e59b0;
        GuestMemory memory;
        const auto result = use_cpp_shovel_metadata_predicate
            ? acnh::shovel_metadata_enabled(main_base, descriptor, memory)
            : (invoke(entry, resumes[path], descriptor.address) & 1) != 0;
        log_shovel_metadata_leaf(entry, resumes[path], use_cpp_shovel_metadata_predicate, descriptor, result);
        return result;
    }

    acnh::CatalogEntry extended_row(acnh::ItemDescriptor descriptor) const {
        return {invoke(0x19da7a0, 0x19ee71c, descriptor.address)};
    }
};

void shovel_metadata_adapter(CPU& cpu) {
    if (cpu.x[30] != main_base + 0x127cd98 || ld<uint64_t>(cpu.x[29] + 8) != main_base + 0x21f7618) {
        original_shovel_metadata(cpu);
        return;
    }
    const acnh::ItemDescriptor descriptor{cpu.x[0]};
    const bool trace = trace_shovel_metadata && shovel_metadata_samples.fetch_add(1, std::memory_order_relaxed) < 16;
    const uint64_t before = trace ? ld<uint64_t>(descriptor.address + 8) : 0;
    const bool has_row = trace && ld<uint64_t>(descriptor.address + 0x10) != 0;
    if (use_cpp_shovel_metadata) {
        GuestMemory memory;
        ShovelMetadataServices services{cpu};
        cpu.x[0] = acnh::shovel_metadata_kind(main_base, descriptor, memory, services);
    } else {
        original_shovel_metadata(cpu);
    }
    if (trace)
        log("acnh shovel metadata %s item=%llx row=%u out=%x",
            use_cpp_shovel_metadata ? "cpp" : "original", static_cast<unsigned long long>(before),
            unsigned(has_row), static_cast<unsigned>(cpu.x[0]));
}

template<size_t Index>
void provider_binding_adapter(CPU& cpu);

struct ProviderBindingRoute {
    uint64_t entry;
    Fn adapter;
    Fn original = nullptr;
    std::atomic<unsigned> samples{0};
};

std::array<ProviderBindingRoute, 3> provider_binding_routes{{
    {0x1267360, provider_binding_adapter<0>},
    {0x260b860, provider_binding_adapter<1>},
    {0x260b934, provider_binding_adapter<2>},
}};
bool use_cpp_provider_binding = false;
bool trace_provider_binding = false;

bool provider_table_binding_caller(uint64_t caller, uint64_t frame) {
    switch (caller) {
        case 0x11e4fa8: case 0x11e4fd0: case 0x11e5008:
        case 0x11e50a4: case 0x11e5170: case 0x11e5194:
            return provider_read_frame(frame);
        case 0x11e45a0:
            return provider_acquire_frame(frame);
        default:
            return false;
    }
}

template<size_t Index>
void provider_binding_adapter(CPU& cpu) {
    auto& route = provider_binding_routes[Index];
    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t frame = cpu.x[29];
    bool on_shovel_path;
    if constexpr (Index == 0) {
        on_shovel_path = provider_table_binding_caller(caller, frame);
    } else if constexpr (Index == 1) {
        on_shovel_path = (caller == 0x11e50f4 && provider_read_frame(frame)) ||
                         (caller == 0x11e45f0 && provider_acquire_frame(frame));
    } else {
        on_shovel_path = (caller == 0x1950414 && shovel_cell_caller(ld<uint64_t>(frame + 8) - main_base)) ||
                         (caller == 0x11e5200 && provider_read_frame(frame));
    }
    if (!on_shovel_path) {
        route.original(cpu);
        return;
    }

    const bool trace = trace_provider_binding && route.samples.fetch_add(1, std::memory_order_relaxed) < 12;
    const GuestMemory memory;
    uint64_t target = 0;
    uint64_t dispatch_kind = 0;
    if constexpr (Index == 0) {
        if (use_cpp_provider_binding || trace) {
            const auto table = acnh::resolve_provider_table(main_base, memory, [&](acnh::ProviderBinding binding, bool extra_layer) {
                target = ld<uint64_t>(ld<uint64_t>(binding.address) + 0x38);
                dispatch_kind = extra_layer ? 2 : 1;
                if (!use_cpp_provider_binding) return uint64_t{0};
                CPU nested = cpu;
                nested.x[31] -= 0x10;
                st<uint64_t>(nested.x[31], cpu.x[29]);
                st<uint64_t>(nested.x[31] + 8, cpu.x[30]);
                nested.x[29] = nested.x[31];
                nested.x[0] = binding.address;
                nested.x[8] = target;
                nested.x[30] = main_base + (extra_layer ? 0x12673c4 : 0x1267394);
                call(nested, target);
                return nested.x[0];
            });
            if (use_cpp_provider_binding) cpu.x[0] = table.address;
        }
        if (!use_cpp_provider_binding) route.original(cpu);
        if (trace) {
            const uint64_t vtable = cpu.x[0] != 0xe7690 ? ld<uint64_t>(cpu.x[0]) - main_base : 0;
            log("acnh shovel provider binding %s entry=%llx state=caller=%llx kind=%llu target=%llx table=%llx",
                use_cpp_provider_binding ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
                static_cast<unsigned long long>(caller), static_cast<unsigned long long>(dispatch_kind),
                static_cast<unsigned long long>(target ? target - main_base : 0), static_cast<unsigned long long>(vtable));
        }
    } else {
        const uint64_t context = cpu.x[0];
        if (use_cpp_provider_binding || trace) {
            const auto member = provider_member_target(context, memory);
            if (member.present) {
                target = member.target;
                if (trace) dispatch_kind = (ld<uint64_t>(context + 0x18) & 1) + 1;
                if (use_cpp_provider_binding) {
                    cpu.x[0] = member.receiver;
                    cpu.x[1] = member.target;
                    cpu.x[8] = member.dispatch_base;
                    call(cpu, member.target);
                }
            } else if constexpr (Index == 1) {
                if (use_cpp_provider_binding) cpu.x[0] = 0;
            }
        }
        if (!use_cpp_provider_binding) route.original(cpu);
        if (trace)
            log("acnh shovel provider binding %s entry=%llx state=caller=%llx kind=%llu target=%llx out=%u",
                use_cpp_provider_binding ? "cpp" : "original", static_cast<unsigned long long>(route.entry),
                static_cast<unsigned long long>(caller), static_cast<unsigned long long>(dispatch_kind),
                static_cast<unsigned long long>(target ? target - main_base : 0), Index == 1 ? unsigned(cpu.x[0] & 1) : 0);
    }
}

Fn original_provider_context_acquire = nullptr;
Fn original_provider_context_release = nullptr;
bool use_cpp_provider_context = false;
bool trace_provider_context = false;
bool use_cpp_provider_context_flag = false;
bool trace_provider_context_flag = false;
std::atomic<unsigned> provider_context_flag_samples{0};
bool use_cpp_provider_context_pair = false;
bool trace_provider_context_pair = false;
std::atomic<unsigned> provider_context_pair_samples{0};

struct ProviderContextFlagServices {
    CPU frame;

    explicit ProviderContextFlagServices(const CPU& caller) : frame(caller) {
        frame.x[31] -= 0x30;
        st<uint64_t>(frame.x[31], caller.x[29]);
        st<uint64_t>(frame.x[31] + 8, main_base + 0x25ee678);
        frame.x[29] = frame.x[31];
    }

    uint64_t resolve_thread(uint32_t id) const {
        CPU nested = frame;
        nested.x[0] = id;
        nested.x[30] = main_base + 0x260113c;
        call(nested, main_base + 0x3f033a0);
        return nested.x[0];
    }

    void refresh_thread_name(uint64_t receiver, unsigned index) const {
        constexpr uint64_t resumes[]{0x2601174, 0x2601184, 0x26011f8, 0x2601208};
        CPU nested = frame;
        nested.x[0] = receiver;
        nested.x[8] = ld<uint64_t>(ld<uint64_t>(receiver) + 0x18);
        nested.x[30] = main_base + resumes[index];
        call(nested, nested.x[8]);
    }
};

bool use_cpp_provider_handle = false;
bool trace_provider_handle = false;
std::atomic<unsigned> provider_handle_samples[2]{};

template<bool Acquire>
void provider_handle_call(CPU& cpu);

bool use_cpp_provider_handle_prepare = false;
bool trace_provider_handle_prepare = false;
std::atomic<unsigned> provider_handle_prepare_samples{0};

uint32_t adjust_provider_word(CPU& cpu, uint64_t address, uint32_t delta) {
    uint32_t before;
    do {
        before = nx::ldxr<uint32_t>(cpu, address);
    } while (nx::stxr<uint32_t>(cpu, address, before + delta));
    return before;
}

struct ProviderContextServices {
    CPU frame;
    CPU& caller;

    ProviderContextServices(CPU& cpu, bool acquire) : frame(cpu), caller(cpu) {
        frame.x[31] -= acquire ? 0x30 : 0x20;
        st<uint64_t>(frame.x[31], cpu.x[29]);
        st<uint64_t>(frame.x[31] + 8, cpu.x[30]);
        frame.x[29] = frame.x[31];
    }

    uint64_t invoke(uint64_t entry, uint64_t resume, uint64_t receiver) {
        CPU nested = frame;
        nested.x[0] = receiver;
        nested.x[30] = main_base + resume;
        call(nested, main_base + entry);
        return nested.x[0];
    }

    uint64_t disabled_flag(acnh::ProviderContextOwner owner) {
        uint64_t result;
        if (use_cpp_provider_context_flag) {
            ProviderContextFlagServices services{frame};
            result = acnh::provider_context_flag(main_base, owner, GuestMemory{}, services);
        } else {
            result = invoke(0x2601110, 0x25ee678, owner.address);
        }
        if (trace_provider_context_flag &&
            provider_context_flag_samples.fetch_add(1, std::memory_order_relaxed) < 24) {
            log("acnh shovel context flag %s caller=%llx out=%llx value=%u",
                use_cpp_provider_context_flag ? "cpp" : "original",
                static_cast<unsigned long long>(caller.x[30] - main_base),
                static_cast<unsigned long long>(result ? result - owner.address : 0),
                result ? unsigned(ld<uint8_t>(result)) : 0);
        }
        return result;
    }

    void enter_guard(uint64_t guard) { invoke(0x3f03880, 0x25ee6bc, guard); }
    bool try_guard(uint64_t guard) { return (invoke(0x3f04c70, 0x25ee6c4, guard) & 1) != 0; }
    void leave_guard(uint64_t guard) { invoke(0x3f038c0, 0x25ee790, guard); }
    void enter_pair(uint64_t pair) {
        const bool trace = trace_provider_context_pair &&
            provider_context_pair_samples.fetch_add(1, std::memory_order_relaxed) < 24;
        if (trace) {
            const auto first = ld<uint64_t>(pair);
            const auto second = ld<uint64_t>(pair + 8);
            log("acnh shovel context pair %s caller=%llx handles=%u,%u kinds=%d,%d out=void",
                use_cpp_provider_context_pair ? "cpp" : "original",
                static_cast<unsigned long long>(caller.x[30] - main_base),
                unsigned(first != 0), unsigned(second != 0),
                first ? ld<int32_t>(first) : 0, second ? ld<int32_t>(second) : 0);
        }
        if (!use_cpp_provider_context_pair) {
            invoke(0x262e9bc, 0x25ee6e4, pair);
            return;
        }

        CPU nested = frame;
        nested.x[0] = pair;
        nested.x[30] = main_base + 0x25ee6e4;
        ProviderContextServices paired{nested, true};
        struct PairCallbacks {
            ProviderContextServices& paired;
            uint64_t counter(uint64_t handle, bool first) {
                return paired.handle_counter(handle, first ? 0x262ea38 : 0x262e9f8);
            }
            void prepare_counter(uint64_t counters, bool first) {
                paired.prepare_handle_counters(counters, first ? 0x262ea50 : 0x262ea10);
            }
        } callbacks{paired};
        GuestMemory memory;
        acnh::acquire_provider_pair({pair}, memory, callbacks);
    }

    uint64_t handle_counter(uint64_t handle, uint64_t resume) {
        CPU nested = frame;
        nested.x[0] = handle;
        nested.x[1] = 1;
        nested.x[30] = main_base + resume;
        provider_handle_call<true>(nested);
        return nested.x[0];
    }

    uint64_t counter(uint64_t handle, bool) { return handle_counter(handle, 0x25ee714); }

    void prepare_counter(uint64_t counters, bool) { prepare_handle_counters(counters, 0x25ee72c); }
    void prepare_handle_counters(uint64_t counters, uint64_t resume);
    void enter_first(uint64_t handle) { invoke(0x37569c0, 0x25ee744, handle); }
    void leave_second(uint64_t handle, bool nested) {
        CPU call_state = frame;
        call_state.x[0] = handle;
        call_state.x[30] = main_base + (nested ? 0x25ee7d8 : 0x25ee7b4);
        provider_handle_call<false>(call_state);
    }

    void leave_first(uint64_t handle, bool nested) {
        caller.x[0] = handle;
        if (nested) call(caller, main_base + 0x3756a80);
        else provider_handle_call<false>(caller);
    }
};

void ProviderContextServices::prepare_handle_counters(uint64_t counters, uint64_t resume) {
    const bool trace = trace_provider_handle_prepare &&
        provider_handle_prepare_samples.fetch_add(1, std::memory_order_relaxed) < 48;
    const auto before = trace ? ld<uint32_t>(counters) : 0;

    if (!use_cpp_provider_handle_prepare) {
        invoke(0x37e2e80, resume, counters);
    } else {
        CPU nested = frame;
        nested.x[0] = counters;
        nested.x[30] = main_base + resume;
        nested.x[31] -= 0x20;
        st<uint64_t>(nested.x[31], frame.x[29]);
        st<uint64_t>(nested.x[31] + 8, nested.x[30]);
        nested.x[29] = nested.x[31];
        struct CounterCallbacks {
            CPU& nested;
            const CPU& parent;

            uint32_t adjust(uint64_t address, uint32_t delta) {
                return adjust_provider_word(nested, address, delta);
            }

            void wait_readers(uint64_t address, bool initial) {
                CPU saved = nested;
                saved.x[0] = address;
                saved.x[30] = main_base + (initial ? 0x37e2eb0 : 0x37e2eec);
                call(saved, main_base + 0x3f05820);
            }

            void wake_readers(uint64_t address) {
                nested.x[0] = address;
                nested.x[29] = parent.x[29];
                nested.x[31] = parent.x[31];
                call(nested, main_base + 0x3f05830);
            }
        } callbacks{nested, frame};
        acnh::prepare_provider_handle_counters(counters, callbacks);
    }
    if (trace)
        log("acnh shovel handle prepare %s caller=%llx before=%u out=%u",
            use_cpp_provider_handle_prepare ? "cpp" : "original",
            static_cast<unsigned long long>(resume), before, ld<uint32_t>(counters));
}

struct ProviderHandleCallbacks {
    ProviderContextServices& services;
    bool acquire;

    uint64_t resolve_thread(uint32_t id, bool second = false) {
        const uint64_t resume = acquire ? (second ? 0x375680c : 0x37567fc) : 0x37568e8;
        return services.invoke(0x3f033a0, resume, id);
    }

    void enter_records(uint64_t guard) { services.invoke(0x3f03880, 0x375684c, guard); }
    void leave_records(uint64_t guard) { services.invoke(0x3f038c0, 0x37568ac, guard); }
    void release_shared(uint64_t holders) {
        trace_shovel_dependency(0x37e3040, 0x3756998);
        services.invoke(0x37e3040, 0x3756998, holders);
    }
    void signal_waiter(uint64_t waiter) { services.invoke(0x3f05830, 0x3756990, waiter); }
    void barrier() { nx::barrier(); }

    uint32_t decrement(uint64_t address) {
        return adjust_provider_word(services.caller, address, uint32_t(-1));
    }
};

template<bool Acquire>
void provider_handle_call(CPU& cpu) {
    constexpr uint64_t entry = Acquire ? 0x37567d0 : 0x37568c0;
    const auto handle = cpu.x[0];
    const auto resume = cpu.x[30] - main_base;
    const bool trace = trace_provider_handle &&
        provider_handle_samples[Acquire].fetch_add(1, std::memory_order_relaxed) < 48;
    const auto capacity = trace ? ld<uint32_t>(handle + 8) : 0;
    const auto records = trace && Acquire ? ld<uint64_t>(handle + 0x10) : 0;
    if (!use_cpp_provider_handle) {
        call(cpu, main_base + entry);
    } else {
        GuestMemory memory;
        ProviderContextServices services{cpu, true};
        ProviderHandleCallbacks callbacks{services, Acquire};
        if constexpr (Acquire)
            cpu.x[0] = acnh::acquire_provider_handle_record(main_base, {handle},
                static_cast<uint32_t>(cpu.x[1]), memory, callbacks);
        else acnh::release_provider_handle(main_base, {handle}, memory, callbacks);
    }

    if (trace) {
        const auto record = Acquire ? cpu.x[0] : 0;
        log("acnh shovel handle %s entry=%llx state=caller=%llx capacity=%u found=%u record=%llx count=%d",
            use_cpp_provider_handle ? "cpp" : "original", static_cast<unsigned long long>(entry),
            static_cast<unsigned long long>(resume), capacity, unsigned(record != 0),
            static_cast<unsigned long long>(record ? record - records : 0),
            record ? ld<int32_t>(record + 0xc) : 0);
    }
}

std::atomic<unsigned> provider_context_samples[2]{};

template<bool Acquire>
void provider_context_adapter(CPU& cpu) {
    const auto original = Acquire ? original_provider_context_acquire : original_provider_context_release;
    const auto caller = cpu.x[30] - main_base;
    const auto frame = cpu.x[29];
    const bool shovel = Acquire
        ? (caller == 0x11e50f4 && provider_read_frame(frame)) ||
          (caller == 0x11e45f0 && provider_acquire_frame(frame))
        : (caller == 0x1950414 && shovel_cell_caller(ld<uint64_t>(frame + 8) - main_base)) ||
          (caller == 0x11e5200 && provider_read_frame(frame));
    if (!shovel) {
        original(cpu);
        return;
    }

    const auto receiver = cpu.x[0];
    const bool trace = trace_provider_context &&
        provider_context_samples[Acquire].fetch_add(1, std::memory_order_relaxed) < 24;
    const bool first = trace && ld<uint64_t>(receiver + 0x308) != 0;
    const bool second = trace && ld<uint64_t>(receiver + 0x310) != 0;
    const auto mode = trace ? ld<uint32_t>(receiver + 0x358) : 0;
    const auto guard = trace ? ld<uint8_t>(receiver + 0x35c) : 0;
    const auto variant = trace ? ld<uint8_t>(receiver + 0x35e) : 0;

    if (!use_cpp_provider_context) {
        original(cpu);
    } else {
        GuestMemory memory;
        ProviderContextServices services{cpu, Acquire};
        if constexpr (Acquire) cpu.x[0] = acnh::acquire_provider_context({receiver}, memory, services);
        else acnh::release_provider_context({receiver}, memory, services);
    }

    if (trace)
        log("acnh shovel context %s entry=%x state=caller=%llx handles=%u,%u mode=%x guard=%x,%x out=%u",
            use_cpp_provider_context ? "cpp" : "original",
            Acquire ? 0x25ee660u : 0x25ee758u, static_cast<unsigned long long>(caller),
            unsigned(first), unsigned(second), mode, unsigned(guard), unsigned(variant),
            Acquire ? unsigned(cpu.x[0] & 1) : 0);
}

Fn original_provider_storage = nullptr;
bool use_cpp_provider_storage = false;
bool trace_provider_storage = false;
std::atomic<unsigned> provider_storage_samples{0};

void provider_storage_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t frame = cpu.x[29];
    if ((caller != 0x1267394 && caller != 0x12673c4) ||
        !provider_table_binding_caller(ld<uint64_t>(frame + 8) - main_base, ld<uint64_t>(frame))) {
        original_provider_storage(cpu);
        return;
    }

    const acnh::ProviderBinding binding{cpu.x[0]};
    const bool trace = trace_provider_storage && provider_storage_samples.fetch_add(1, std::memory_order_relaxed) < 24;
    const auto expected = (use_cpp_provider_storage || trace)
        ? acnh::provider_bound_storage(binding, GuestMemory{}).address : 0;
    if (use_cpp_provider_storage) cpu.x[0] = expected;
    else original_provider_storage(cpu);
    if (trace)
        log("acnh shovel provider storage %s caller=%llx binding=%llx input=%llx out=%llx",
            use_cpp_provider_storage ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(binding.address), static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(cpu.x[0]));
}

void install_field_record(const ModuleInfo& module, FuncEntry* functions) {
    if (std::string_view(module.name) != "main" ||
        mod_package::build_id(plat::data_root()) != camera303::build_id) return;
    main_base = module.base;
    const std::span<FuncEntry> entries{functions, module.func_count};
    const char* mode = std::getenv("NX_ACNH_FIELD_RECORD");
    use_cpp = !mode || std::string_view(mode) == "cpp";
    trace_field_record = std::getenv("NX_ACNH_FIELD_RECORD_TRACE") != nullptr;
    if (use_cpp || trace_field_record)
        wrap_function(entries, row_entry, field_record_adapter, original_field_record);

    mode = std::getenv("NX_ACNH_INDEXED_RECORD");
    use_cpp_indexed_record = mode && std::string_view(mode) == "cpp";
    trace_indexed_record = std::getenv("NX_ACNH_INDEXED_RECORD_TRACE") != nullptr;
    if (use_cpp_indexed_record || trace_indexed_record)
        wrap_function(entries, 0x240f090, indexed_record_adapter, original_indexed_record);

    mode = std::getenv("NX_ACNH_SHOVEL_ALTERNATE_TERRAIN");
    use_cpp_shovel_alternate_terrain = !mode || std::string_view(mode) == "cpp";
    trace_shovel_alternate_terrain = std::getenv("NX_ACNH_SHOVEL_ALTERNATE_TERRAIN_TRACE") != nullptr;
    if (use_cpp_shovel_alternate_terrain || trace_shovel_alternate_terrain)
        wrap_function(entries, 0x87bf30, shovel_alternate_terrain_adapter, original_shovel_alternate_terrain);

    mode = std::getenv("NX_ACNH_HALF_TILE_LAYER_FLAG");
    use_cpp_half_tile_layer_flag = !mode || std::string_view(mode) == "cpp";
    trace_half_tile_layer_flag = std::getenv("NX_ACNH_HALF_TILE_LAYER_FLAG_TRACE") != nullptr;
    if (use_cpp_half_tile_layer_flag || trace_half_tile_layer_flag || trace_shovel_alternate_terrain)
        wrap_function(entries, 0x1285fe0, half_tile_layer_flag_adapter, original_half_tile_layer_flag);

    mode = std::getenv("NX_ACNH_FIELD_COLUMN");
    use_cpp_column = !mode || std::string_view(mode) == "cpp";
    trace_field_column = std::getenv("NX_ACNH_FIELD_COLUMN_TRACE") != nullptr;
    const uint64_t data_end = module.bss ? module.bss + module.bss_size : module.data + module.data_size;
    if ((use_cpp_column || trace_field_column) && column_cache_offset >= module.data &&
        column_cache_offset + ColumnCache{}.size() <= data_end)
        wrap_function(entries, column_entry, field_column_adapter, original_field_column);

    mode = std::getenv("NX_ACNH_FIELD_FLAG");
    use_cpp_field_flag = mode && std::string_view(mode) == "cpp";
    trace_field_flag = std::getenv("NX_ACNH_FIELD_FLAG_TRACE") != nullptr;
    if (use_cpp_field_flag || trace_field_flag)
        wrap_function(entries, field_flag_entry, field_flag_adapter, original_field_flag);

    mode = std::getenv("NX_ACNH_TILE_RECORD_FLAG");
    use_cpp_tile_record_flag = !mode || std::string_view(mode) == "cpp";
    trace_tile_record_flag = std::getenv("NX_ACNH_TILE_RECORD_FLAG_TRACE") != nullptr;
    if (use_cpp_tile_record_flag || trace_tile_record_flag)
        wrap_function(entries, 0x1218220, tile_record_flag_adapter, original_tile_record_flag);

    trace_cached_field = std::getenv("NX_ACNH_EXTENDED_FIELD_TRACE") != nullptr;
    const char* const cache_modes[]{"NX_ACNH_FIELD_COLUMN_25", "NX_ACNH_CACHED_FIELD_BIT"};
    for (size_t i = 0; i < cached_field_routes.size(); ++i) {
        auto& route = cached_field_routes[i];
        mode = std::getenv(cache_modes[i]);
        route.use_cpp = !mode || std::string_view(mode) == "cpp";
        if (route.use_cpp || trace_cached_field)
            wrap_function(entries, route.entry, route.adapter, route.original);
    }

    trace_terrain_dispatch = std::getenv("NX_ACNH_SHOVEL_TERRAIN_DISPATCH_TRACE") != nullptr;
    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_DISPATCH");
    use_cpp_shovel_terrain = !mode || std::string_view(mode) == "cpp";
    if (use_cpp_shovel_terrain || trace_terrain_dispatch)
        wrap_function(entries, 0x877130, shovel_terrain_adapter, original_shovel_terrain);
    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_SAMPLE");
    use_cpp_terrain_sample = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_SHOVEL_CAST_HEIGHT");
    use_cpp_shovel_cast_height = !mode || std::string_view(mode) == "cpp";
    trace_shovel_cast_height = std::getenv("NX_ACNH_SHOVEL_CAST_HEIGHT_TRACE") != nullptr;

    mode = std::getenv("NX_ACNH_SHOVEL_MARKER");
    use_cpp_shovel_marker = !mode || std::string_view(mode) == "cpp";
    trace_shovel_marker = std::getenv("NX_ACNH_SHOVEL_MARKER_TRACE") != nullptr;
    if (use_cpp_shovel_marker || trace_shovel_marker) {
        constexpr std::array<Fn, 4> marker_adapters{
            shovel_marker_adapter<0>, shovel_marker_adapter<1>, shovel_marker_adapter<2>, shovel_marker_adapter<3>};
        for (size_t i = 0; i < marker_adapters.size(); ++i)
            wrap_function(entries, shovel_marker_entries[i], marker_adapters[i], original_shovel_marker[i]);
    }
    if (use_cpp_shovel_marker || trace_shovel_marker || use_cpp_shovel_cast_height || trace_shovel_cast_height)
        wrap_function(entries, 0x87a980, shovel_primary_terrain_fallback_adapter, original_shovel_primary_terrain_fallback);
    if (use_cpp_shovel_cast_height || trace_shovel_cast_height)
        wrap_function(entries, 0x87aa70, shovel_cast_secondary_adapter, original_shovel_cast_secondary);
    if (use_cpp_terrain_sample || trace_terrain_dispatch || trace_shovel_marker || trace_shovel_alternate_terrain)
        wrap_function(entries, 0x862530, terrain_sample_adapter, original_terrain_sample);

    trace_terrain_helpers = std::getenv("NX_ACNH_SHOVEL_TERRAIN_HELPERS_TRACE") != nullptr;
    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_LEVEL");
    use_cpp_terrain_level = !mode || std::string_view(mode) == "cpp";
    if (use_cpp_terrain_level || trace_terrain_helpers || trace_shovel_alternate_terrain)
        wrap_function(entries, 0x871f80, terrain_level_adapter, original_terrain_level);
    mode = std::getenv("NX_ACNH_SHOVEL_HEIGHT_CHAIN");
    use_cpp_shovel_height_chain = !mode || std::string_view(mode) == "cpp";
    trace_shovel_height_chain = std::getenv("NX_ACNH_SHOVEL_HEIGHT_CHAIN_TRACE") != nullptr;

    mode = std::getenv("NX_ACNH_SHOVEL_GRID_HEIGHT");
    use_cpp_shovel_grid_height = !mode || std::string_view(mode) == "cpp";
    trace_shovel_grid_height = std::getenv("NX_ACNH_SHOVEL_GRID_HEIGHT_TRACE") != nullptr;
    if (use_cpp_shovel_grid_height || trace_shovel_grid_height || trace_shovel_height_chain || trace_shovel_cast_height)
        wrap_function(entries, 0x862700, shovel_grid_height_adapter, original_shovel_grid_height);

    mode = std::getenv("NX_ACNH_SHOVEL_STAGE_PROVIDER");
    use_cpp_shovel_provider_record = !mode || std::string_view(mode) == "cpp";
    trace_shovel_record_leaves = std::getenv("NX_ACNH_SHOVEL_STAGE_PROVIDER_TRACE") != nullptr;
    if (use_cpp_shovel_provider_record || trace_shovel_record_leaves)
        wrap_function(entries, 0x11eb21c, shovel_provider_record_adapter, original_shovel_provider_record);

    mode = std::getenv("NX_ACNH_SHOVEL_STEP");
    use_cpp_shovel_step = !mode || std::string_view(mode) == "cpp";
    trace_shovel_step = std::getenv("NX_ACNH_SHOVEL_STEP_TRACE") != nullptr;
    if (use_cpp_shovel_step || trace_shovel_step)
        wrap_function(entries, 0x15f0c64, shovel_step_adapter, original_shovel_step);

    mode = std::getenv("NX_ACNH_SHOVEL_INTERMEDIATE");
    use_cpp_shovel_intermediate = !mode || std::string_view(mode) == "cpp";
    trace_shovel_intermediate = std::getenv("NX_ACNH_SHOVEL_INTERMEDIATE_TRACE") != nullptr;
    if (use_cpp_shovel_intermediate || trace_shovel_intermediate)
        wrap_function(entries, 0x15e9c80, shovel_intermediate_adapter, original_shovel_intermediate);

    mode = std::getenv("NX_ACNH_SHOVEL_SIMPLE_CAST");
    use_cpp_shovel_simple_cast = !mode || std::string_view(mode) == "cpp";
    trace_shovel_simple_cast = std::getenv("NX_ACNH_SHOVEL_SIMPLE_CAST_TRACE") != nullptr;
    mode = std::getenv("NX_ACNH_SHOVEL_MIXED_CAST");
    use_cpp_shovel_mixed_cast = !mode || std::string_view(mode) == "cpp";
    trace_shovel_mixed_cast = std::getenv("NX_ACNH_SHOVEL_MIXED_CAST_TRACE") != nullptr;
    if (use_cpp_shovel_simple_cast || trace_shovel_simple_cast || use_cpp_shovel_mixed_cast || trace_shovel_mixed_cast)
        wrap_function(entries, 0x15ed290, shovel_step_cast_adapter, original_shovel_step_cast);

    mode = std::getenv("NX_ACNH_SHOVEL_COLLISION_PROBES");
    use_cpp_shovel_collision_probes = !mode || std::string_view(mode) == "cpp";
    trace_shovel_collision_probes = std::getenv("NX_ACNH_SHOVEL_COLLISION_PROBES_TRACE") != nullptr;
    if (use_cpp_shovel_collision_probes || trace_shovel_collision_probes) {
        constexpr std::array<Fn, 2> probes{shovel_collision_probe_adapter<0>, shovel_collision_probe_adapter<1>};
        for (size_t i = 0; i < probes.size(); ++i)
            wrap_function(entries, shovel_collision_probe_entries[i], probes[i], original_shovel_collision_probes[i]);
    }

    mode = std::getenv("NX_ACNH_SHOVEL_STAGE_CONTEXT");
    use_cpp_shovel_stage_context = !mode || std::string_view(mode) == "cpp";
    trace_shovel_stage_context = std::getenv("NX_ACNH_SHOVEL_STAGE_CONTEXT_TRACE") != nullptr;
    if (use_cpp_shovel_stage_context || trace_shovel_stage_context) {
        constexpr std::array<Fn, 3> context_adapters{
            shovel_stage_context_adapter<0>, shovel_stage_context_adapter<1>, shovel_stage_context_adapter<2>};
        for (size_t i = 0; i < context_adapters.size(); ++i)
            wrap_function(entries, shovel_stage_context_entries[i], context_adapters[i], original_shovel_stage_context[i]);
    }

    mode = std::getenv("NX_ACNH_SHOVEL_STAGE_HEIGHT");
    use_cpp_shovel_stage_height = !mode || std::string_view(mode) == "cpp";
    trace_shovel_stage_height = std::getenv("NX_ACNH_SHOVEL_STAGE_HEIGHT_TRACE") != nullptr;
    if (use_cpp_shovel_stage_height || trace_shovel_stage_height)
        wrap_function(entries, 0x15e06d0, shovel_stage_height_adapter, original_shovel_stage_height);

    mode = std::getenv("NX_ACNH_SHOVEL_POINT_HEIGHT");
    use_cpp_shovel_point_height = !mode || std::string_view(mode) == "cpp";
    trace_shovel_point_height = std::getenv("NX_ACNH_SHOVEL_POINT_HEIGHT_TRACE") != nullptr;
    mode = std::getenv("NX_ACNH_SHOVEL_POINT_OBSTACLE");
    use_cpp_shovel_obstacle = !mode || std::string_view(mode) == "cpp";
    if ((use_cpp_shovel_point_height && use_cpp_shovel_obstacle) || trace_shovel_point_height)
        wrap_function(entries, 0x15ea82c, shovel_obstacle_adapter, original_shovel_obstacle);

    mode = std::getenv("NX_ACNH_SHOVEL_POINT_CLASSIFIER");
    use_cpp_shovel_point_classifier = !mode || std::string_view(mode) == "cpp";
    trace_shovel_point_classifier = std::getenv("NX_ACNH_SHOVEL_POINT_CLASSIFIER_TRACE") != nullptr;
    if (use_cpp_shovel_point_classifier || trace_shovel_point_classifier)
        wrap_function(entries, 0x15f3ed0, shovel_point_classifier_adapter, original_shovel_point_classifier);

    mode = std::getenv("NX_ACNH_SHOVEL_HEIGHT_PARAMETER_TYPE");
    use_cpp_shovel_height_parameter_type = !mode || std::string_view(mode) == "cpp";
    trace_shovel_height_parameter_type = std::getenv("NX_ACNH_SHOVEL_HEIGHT_PARAMETER_TYPE_TRACE") != nullptr;
    if (use_cpp_shovel_height_parameter_type || trace_shovel_height_parameter_type || trace_shovel_height_chain || trace_shovel_cast_height)
        wrap_function(entries, 0x22688e4, shovel_height_parameter_type_adapter, original_shovel_height_parameter_type);

    trace_shovel_height = std::getenv("NX_ACNH_SHOVEL_HEIGHT_TRACE") != nullptr;
    constexpr std::array<Fn, 5> height_adapters{
        shovel_height_adapter<0>, shovel_height_adapter<1>, shovel_height_adapter<2>,
        shovel_height_adapter<3>, shovel_height_adapter<4>};
    for (size_t i = 0; i < height_adapters.size(); ++i) {
        const char* setting = std::getenv(shovel_height_switches[i]);
        use_cpp_shovel_height[i] = !setting || std::string_view(setting) == "cpp";
        if (use_cpp_shovel_height[i] || trace_shovel_height || trace_shovel_height_chain || trace_shovel_cast_height || (i == 0 && trace_shovel_point_height))
            wrap_function(entries, shovel_height_entries[i], height_adapters[i], original_shovel_height[i]);
    }

    mode = std::getenv("NX_ACNH_SHOVEL_TARGET");
    use_cpp_shovel_target = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_SETUP");
    use_cpp_shovel_terrain_setup = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_SHOVEL_LAYER_ONE");
    use_cpp_shovel_layer_one = !mode || std::string_view(mode) == "cpp";
    trace_shovel_target = std::getenv("NX_ACNH_SHOVEL_TARGET_TRACE") != nullptr;
    if (use_cpp_shovel_target || trace_shovel_target)
        wrap_function(entries, 0x15f36e0, shovel_target_adapter, original_shovel_target);
    if (use_cpp_shovel_terrain_setup || trace_shovel_target)
        wrap_function(entries, 0x87b460, shovel_terrain_setup_adapter, original_shovel_terrain_setup);
    if (use_cpp_shovel_layer_one || trace_shovel_target)
        wrap_function(entries, 0x1284ee0, shovel_layer_one_adapter, original_shovel_layer_one);

    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_PRIMARY");
    use_cpp_terrain_primary = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_SECONDARY");
    use_cpp_terrain_secondary = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_FIELD_MATERIAL");
    use_cpp_terrain_field_material = !mode || std::string_view(mode) == "cpp";
    trace_terrain_fallbacks = std::getenv("NX_ACNH_SHOVEL_TERRAIN_FALLBACK_TRACE") != nullptr;
    trace_sampler_terrain = std::getenv("NX_ACNH_SHOVEL_SAMPLER_TERRAIN_TRACE") != nullptr;
    mode = std::getenv("NX_ACNH_SHOVEL_SAMPLER_TERRAIN");
    use_cpp_sampler_terrain = !mode || std::string_view(mode) == "cpp";
    if (use_cpp_terrain_field_material || trace_terrain_fallbacks || trace_sampler_terrain || trace_shovel_cast_height)
        wrap_function(entries, 0x1285040, terrain_field_material_adapter, original_terrain_field_material);

    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_MATERIALS");
    use_cpp_terrain_materials = !mode || std::string_view(mode) == "cpp";
    if (use_cpp_terrain_materials || trace_terrain_helpers)
        wrap_function(entries, 0x848e68, terrain_materials_adapter, original_terrain_materials);

    mode = std::getenv("NX_ACNH_SHOVEL_TERRAIN_PAIR");
    use_cpp_shovel_terrain_pair = !mode || std::string_view(mode) == "cpp";
    trace_shovel_terrain_pair = std::getenv("NX_ACNH_SHOVEL_TERRAIN_PAIR_TRACE") != nullptr;
    if (use_cpp_shovel_terrain_pair || trace_shovel_terrain_pair) {
        constexpr std::array<Fn, 2> pair{shovel_terrain_pair_adapter<0>, shovel_terrain_pair_adapter<1>};
        for (size_t i = 0; i < pair.size(); ++i)
            wrap_function(entries, shovel_terrain_pair_entries[i], pair[i], original_shovel_terrain_pair[i]);
    }

    trace_terrain = std::getenv("NX_ACNH_SHOVEL_TERRAIN_TRACE") != nullptr;
    const char* const terrain_modes[]{"NX_ACNH_TERRAIN_DIGGABLE", "NX_ACNH_TERRAIN_ALTERNATE"};
    for (size_t i = 0; i < terrain_routes.size(); ++i) {
        auto& route = terrain_routes[i];
        mode = std::getenv(terrain_modes[i]);
        route.use_cpp = (i == 0 && !mode) || (mode && std::string_view(mode) == "cpp");
        if (route.use_cpp || trace_terrain)
            wrap_function(entries, route.entry, route.adapter, route.original);
    }

    mode = std::getenv("NX_ACNH_FIELD_ITEM_ID");
    use_cpp_field_item_id = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_AUXILIARY_ITEM_INDEX");
    use_cpp_auxiliary_item = mode && std::string_view(mode) == "cpp";
    trace_field_item = std::getenv("NX_ACNH_FIELD_ITEM_TRACE") != nullptr;
    if (use_cpp_auxiliary_item || use_cpp_field_item_id || trace_field_item) {
        if (!wrap_function(entries, 0x1a03610, auxiliary_item_adapter, original_auxiliary_item))
            use_cpp_field_item_id = false;
    }
    if (use_cpp_field_item_id || trace_field_item)
        wrap_function(entries, 0x267d6c4, field_item_id_adapter, original_field_item_id);

    mode = std::getenv("NX_ACNH_SHOVEL_FIELD_BOUNDS");
    use_cpp_field_bounds = !mode || std::string_view(mode) == "cpp";
    trace_field_bounds = std::getenv("NX_ACNH_SHOVEL_FIELD_BOUNDS_TRACE") != nullptr;
    if (use_cpp_field_bounds || trace_field_bounds || trace_shovel_record_leaves)
        wrap_function(entries, 0x194f798, field_bounds_adapter, original_field_bounds);

    mode = std::getenv("NX_ACNH_SHOVEL_FIELD_CELL");
    use_cpp_field_cell = !mode || std::string_view(mode) == "cpp";
    trace_field_cell = std::getenv("NX_ACNH_SHOVEL_FIELD_CELL_TRACE") != nullptr;
    if (use_cpp_field_cell || trace_field_cell)
        wrap_function(entries, 0x195033c, field_cell_adapter, original_field_cell);

    mode = std::getenv("NX_ACNH_PROVIDER_CELL");
    use_cpp_provider_cell = !mode || std::string_view(mode) == "cpp";
    trace_provider_cell = std::getenv("NX_ACNH_PROVIDER_CELL_TRACE") != nullptr;
    if (use_cpp_provider_cell || trace_provider_cell)
        wrap_function(entries, 0x11e4f58, provider_cell_adapter, original_provider_cell);

    mode = std::getenv("NX_ACNH_PROVIDER_ANCHOR");
    use_cpp_provider_anchor = !mode || std::string_view(mode) == "cpp";
    trace_provider_anchor = std::getenv("NX_ACNH_PROVIDER_ANCHOR_TRACE") != nullptr;
    if (use_cpp_provider_anchor || trace_provider_anchor)
        for (auto& route : provider_anchor_routes)
            wrap_function(entries, route.entry, route.adapter, route.original);

    mode = std::getenv("NX_ACNH_SHOVEL_EMPTY_ITEM_INIT");
    use_cpp_empty_item_init = !mode || std::string_view(mode) == "cpp";
    trace_empty_item_init = std::getenv("NX_ACNH_SHOVEL_EMPTY_ITEM_INIT_TRACE") != nullptr;
    if (use_cpp_empty_item_init || trace_empty_item_init) {
        wrap_function(entries, 0x267d4ac, empty_item_init_adapter, original_empty_item_init);
        wrap_function(entries, 0x267f248, empty_item_finalize_adapter, original_empty_item_finalize);
    }

    mode = std::getenv("NX_ACNH_SHOVEL_ALTERNATE_KIND");
    use_cpp_shovel_alternate_kind = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_SHOVEL_METADATA_PREDICATE");
    use_cpp_shovel_metadata_predicate = !mode || std::string_view(mode) == "cpp";
    trace_shovel_metadata_leaves = std::getenv("NX_ACNH_SHOVEL_METADATA_LEAF_TRACE") != nullptr;

    mode = std::getenv("NX_ACNH_SHOVEL_METADATA");
    use_cpp_shovel_metadata = !mode || std::string_view(mode) == "cpp";
    trace_shovel_dependencies = std::getenv("NX_ACNH_SHOVEL_DEPENDENCY_TRACE") != nullptr;
    trace_shovel_metadata = std::getenv("NX_ACNH_SHOVEL_METADATA_TRACE") != nullptr;
    if (use_cpp_shovel_metadata || trace_shovel_metadata)
        wrap_function(entries, 0x19ee3b4, shovel_metadata_adapter, original_shovel_metadata);

    mode = std::getenv("NX_ACNH_PROVIDER_BINDING");
    use_cpp_provider_binding = !mode || std::string_view(mode) == "cpp";
    trace_provider_binding = std::getenv("NX_ACNH_PROVIDER_BINDING_TRACE") != nullptr;
    if (use_cpp_provider_binding || trace_provider_binding)
        for (auto& route : provider_binding_routes)
            wrap_function(entries, route.entry, route.adapter, route.original);

    mode = std::getenv("NX_ACNH_PROVIDER_HANDLE_PREPARE");
    use_cpp_provider_handle_prepare = !mode || std::string_view(mode) == "cpp";
    trace_provider_handle_prepare = std::getenv("NX_ACNH_PROVIDER_HANDLE_PREPARE_TRACE") != nullptr;

    mode = std::getenv("NX_ACNH_PROVIDER_HANDLE");
    use_cpp_provider_handle = !mode || std::string_view(mode) == "cpp";
    trace_provider_handle = std::getenv("NX_ACNH_PROVIDER_HANDLE_TRACE") != nullptr;

    mode = std::getenv("NX_ACNH_PROVIDER_CONTEXT_PAIR");
    use_cpp_provider_context_pair = !mode || std::string_view(mode) == "cpp";
    trace_provider_context_pair = std::getenv("NX_ACNH_PROVIDER_CONTEXT_PAIR_TRACE") != nullptr;

    mode = std::getenv("NX_ACNH_PROVIDER_CONTEXT_FLAG");
    use_cpp_provider_context_flag = !mode || std::string_view(mode) == "cpp";
    trace_provider_context_flag = std::getenv("NX_ACNH_PROVIDER_CONTEXT_FLAG_TRACE") != nullptr;
    mode = std::getenv("NX_ACNH_PROVIDER_CONTEXT");
    use_cpp_provider_context = !mode || std::string_view(mode) == "cpp";
    trace_provider_context = std::getenv("NX_ACNH_PROVIDER_CONTEXT_TRACE") != nullptr;
    if (use_cpp_provider_context || trace_provider_context) {
        wrap_function(entries, 0x25ee660, provider_context_adapter<true>, original_provider_context_acquire);
        wrap_function(entries, 0x25ee758, provider_context_adapter<false>, original_provider_context_release);
    }

    mode = std::getenv("NX_ACNH_PROVIDER_STORAGE");
    use_cpp_provider_storage = !mode || std::string_view(mode) == "cpp";
    trace_provider_storage = std::getenv("NX_ACNH_PROVIDER_STORAGE_TRACE") != nullptr;
    if (use_cpp_provider_storage || trace_provider_storage)
        wrap_function(entries, 0x260bd50, provider_storage_adapter, original_provider_storage);

    mode = std::getenv("NX_ACNH_PROVIDER_METHODS");
    use_cpp_provider_methods = !mode || std::string_view(mode) == "cpp";
    trace_provider_methods = std::getenv("NX_ACNH_PROVIDER_METHODS_TRACE") != nullptr;
    if (use_cpp_provider_methods || trace_provider_methods)
        for (auto& route : provider_methods)
            wrap_function(entries, route.entry, route.adapter, route.original);

    mode = std::getenv("NX_ACNH_SHOVEL_ITEM_CLASS");
    use_cpp_shovel_item_queries = !mode || std::string_view(mode) == "cpp";
    trace_shovel_item_queries = std::getenv("NX_ACNH_SHOVEL_ITEM_CLASS_TRACE") != nullptr;
    if (use_cpp_shovel_item_queries || trace_shovel_item_queries)
        for (auto& route : shovel_item_queries)
            wrap_function(entries, route.entry, route.adapter, route.original);

    mode = std::getenv("NX_ACNH_SHOVEL_STAGE");
    use_cpp_shovel_stage = !mode || std::string_view(mode) == "cpp";
    trace_shovel_stage = std::getenv("NX_ACNH_SHOVEL_STAGE_TRACE") != nullptr;
    if (use_cpp_shovel_stage || trace_shovel_stage)
        for (auto& route : shovel_stage_routes)
            wrap_function(entries, route.entry, route.adapter, route.original);

    mode = std::getenv("NX_ACNH_RECORD_CATEGORY");
    use_cpp_categories = !mode || std::string_view(mode) == "cpp";
    trace_categories = std::getenv("NX_ACNH_RECORD_CATEGORY_TRACE") != nullptr;
    if ((use_cpp_categories || trace_categories) && module.data <= 0x5390120 && 0x5390130 <= data_end)
        for (auto& route : category_routes)
            wrap_function(entries, route.entry, route.adapter, route.original);
}

}  // namespace

void register_acnh_field_record(Registry& registry) {
    Definition feature;
    feature.id = "acnh.field-record";
    feature.module_loaded = install_field_record;
    registry.add(std::move(feature));
}

}  // namespace nx::features
