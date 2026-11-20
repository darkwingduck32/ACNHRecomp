#include "../../../games/acnh/field_column.h"
#include "../../../games/acnh/field_record.h"

#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

#include <array>
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
constexpr uint64_t column_cache_offset = 0x4bfab80;
using ColumnCache = std::array<uint8_t, 60>;
Fn original_field_record = nullptr;
uint64_t main_base = 0;
bool use_cpp = false;
bool trace_field_record = false;
Fn original_field_column = nullptr;
bool use_cpp_column = false;
bool trace_field_column = false;

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

std::string cache_hex(const ColumnCache& cache) {
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

    mode = std::getenv("NX_ACNH_FIELD_COLUMN");
    use_cpp_column = !mode || std::string_view(mode) == "cpp";
    trace_field_column = std::getenv("NX_ACNH_FIELD_COLUMN_TRACE") != nullptr;
    const uint64_t data_end = module.bss ? module.bss + module.bss_size : module.data + module.data_size;
    if ((use_cpp_column || trace_field_column) && column_cache_offset >= module.data &&
        column_cache_offset + ColumnCache{}.size() <= data_end)
        wrap_function(entries, column_entry, field_column_adapter, original_field_column);
}

}  // namespace

void register_acnh_field_record(Registry& registry) {
    Definition feature;
    feature.id = "acnh.field-record";
    feature.module_loaded = install_field_record;
    registry.add(std::move(feature));
}

}  // namespace nx::features
