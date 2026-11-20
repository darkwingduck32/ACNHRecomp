#include "../../../games/acnh/field_column.h"
#include "../../../games/acnh/field_column_25.h"
#include "../../../games/acnh/field_flag.h"
#include "../../../games/acnh/field_record.h"
#include "../../../games/acnh/field_queries.h"
#include "../../../games/acnh/field_provider.h"
#include "../../../games/acnh/record_category.h"
#include "../../../games/acnh/tile_record_flag.h"

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
    if (!use_cpp_half_tile_layer_flag && !trace) {
        original_half_tile_layer_flag(cpu);
        return;
    }

    const uint64_t query = cpu.x[0];
    const acnh::FieldLayer layer{static_cast<uint32_t>(cpu.x[1])};
    std::string reads;
    const GuestMemory memory{trace ? &reads : nullptr};
    const acnh::FieldQueries fields{main_base, memory};
    const auto grid = fields.grid_for(layer);
    bool found = false;
    if (grid) {
        const acnh::HalfTile tile{memory.read<int32_t>(query + 8), memory.read<int32_t>(query + 12)};
        found = fields.record_flag(fields.record_at(grid, tile));
    }
    if (use_cpp_half_tile_layer_flag) cpu.x[0] = found;
    else original_half_tile_layer_flag(cpu);
    if (trace)
        log("acnh half tile layer %s caller=%llx base=%llx query=%llx layer=%x out=%llx reads=%s parent=%llx",
            use_cpp_half_tile_layer_flag ? "cpp" : "original", static_cast<unsigned long long>(caller),
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

void field_bounds_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    const uint64_t parent = trace_field_bounds && caller == 0x1950370
        ? ld<uint64_t>(cpu.x[29] + 8) - main_base : 0;
    const bool trace = shovel_cell_caller(parent) &&
        field_bounds_samples.fetch_add(1, std::memory_order_relaxed) < 32;
    if (!use_cpp_field_bounds && !trace) {
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
        ld<uint64_t>(cpu.x[29] + 8) != main_base + 0x1950414 ||
        !shovel_cell_caller(ld<uint64_t>(ld<uint64_t>(cpu.x[29]) + 8) - main_base)) {
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

    mode = std::getenv("NX_ACNH_HALF_TILE_LAYER_FLAG");
    use_cpp_half_tile_layer_flag = !mode || std::string_view(mode) == "cpp";
    trace_half_tile_layer_flag = std::getenv("NX_ACNH_HALF_TILE_LAYER_FLAG_TRACE") != nullptr;
    if (use_cpp_half_tile_layer_flag || trace_half_tile_layer_flag)
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
    if (use_cpp_field_bounds || trace_field_bounds)
        wrap_function(entries, 0x194f798, field_bounds_adapter, original_field_bounds);

    mode = std::getenv("NX_ACNH_SHOVEL_FIELD_CELL");
    use_cpp_field_cell = !mode || std::string_view(mode) == "cpp";
    trace_field_cell = std::getenv("NX_ACNH_SHOVEL_FIELD_CELL_TRACE") != nullptr;
    if (use_cpp_field_cell || trace_field_cell)
        wrap_function(entries, 0x195033c, field_cell_adapter, original_field_cell);

    mode = std::getenv("NX_ACNH_PROVIDER_CELL");
    use_cpp_provider_cell = mode && std::string_view(mode) == "cpp";
    trace_provider_cell = std::getenv("NX_ACNH_PROVIDER_CELL_TRACE") != nullptr;
    if (use_cpp_provider_cell || trace_provider_cell)
        wrap_function(entries, 0x11e4f58, provider_cell_adapter, original_provider_cell);

    mode = std::getenv("NX_ACNH_PROVIDER_METHODS");
    use_cpp_provider_methods = !mode || std::string_view(mode) == "cpp";
    trace_provider_methods = std::getenv("NX_ACNH_PROVIDER_METHODS_TRACE") != nullptr;
    if (use_cpp_provider_methods || trace_provider_methods)
        for (auto& route : provider_methods)
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
