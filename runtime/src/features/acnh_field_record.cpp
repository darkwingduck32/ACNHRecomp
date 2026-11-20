#include "../../../games/acnh/field_column.h"
#include "../../../games/acnh/field_column_25.h"
#include "../../../games/acnh/field_flag.h"
#include "../../../games/acnh/field_record.h"
#include "../../../games/acnh/field_queries.h"
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
    std::atomic<unsigned> samples{0};
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

    if (!trace_terrain || caller < route.first_caller || caller > route.last_caller ||
        route.samples.fetch_add(1, std::memory_order_relaxed) >= 24) {
        apply();
        return;
    }

    const uint64_t parent = ld<uint64_t>(cpu.x[29] + 8) - main_base;
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
        route.use_cpp = mode && std::string_view(mode) == "cpp";
        if (route.use_cpp || trace_terrain)
            wrap_function(entries, route.entry, route.adapter, route.original);
    }

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
