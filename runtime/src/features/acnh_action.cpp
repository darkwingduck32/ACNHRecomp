#include "../../../games/acnh/action_mask.h"
#include "../../../games/acnh/field_primitives.h"
#include "../../../games/acnh/field_queries.h"
#include "../../../games/acnh/shovel_survey.h"
#include "../../../games/acnh/field_state.h"
#include "../../../games/acnh/terminated_hash.h"

#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

#include <atomic>
#include <string>
#include <cstdlib>
#include <string_view>
#include <nx/runtime.h>

namespace nx::features {
namespace {

constexpr uint64_t mask_entry = 0x24699b8;
Fn original_mask = nullptr;
uint64_t main_base = 0;
bool use_cpp = false;
bool trace_mask = false;

struct GuestMemory {
    template<class T>
    T read(uint64_t address) const { return ld<T>(address); }

    template<class T>
    void write(uint64_t address, T value) { st<T>(address, value); }
};

void action_mask_adapter(CPU& cpu) {
    const uint64_t object = cpu.x[0];
    const uint64_t caller = cpu.x[30] - main_base;
    const auto apply = [&] {
        if (use_cpp) {
            GuestMemory memory;
            acnh::initialize_action_mask(object, memory);
        } else {
            original_mask(cpu);
        }
    };
    if (!trace_mask) {
        apply();
        return;
    }

    const auto source = ld<unsigned long long>(object + 0x10);
    const auto working = ld<unsigned long long>(object + 0x20);
    const auto initialized = ld<uint8_t>(object + 0x28);
    apply();
    log("acnh action mask %s caller=%llx before=%llx,%llx,%x after=%llx,%llx,%x",
        use_cpp ? "cpp" : "original", static_cast<unsigned long long>(caller),
        source, working, unsigned(initialized), ld<unsigned long long>(object + 0x10),
        ld<unsigned long long>(object + 0x20), unsigned(ld<uint8_t>(object + 0x28)));
}

Fn original_inline_strings = nullptr;
Fn original_gameplay_percent = nullptr;
bool use_cpp_strings = false;
bool use_cpp_percent = false;
bool trace_field_state = false;
std::atomic<unsigned> string_samples{0};
std::atomic<unsigned> percent_samples{0};

std::string field_bytes(uint64_t address, size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(2 * size);
    for (size_t i = 0; i < size; ++i) {
        const auto value = ld<uint8_t>(address + i);
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0xf]);
    }
    return result;
}

void inline_strings_adapter(CPU& cpu) {
    const uint64_t object = cpu.x[0];
    const auto apply = [&] {
        if (use_cpp_strings) {
            GuestMemory memory;
            acnh::initialize_inline_strings(main_base, object, memory);
        } else original_inline_strings(cpu);
    };
    if (!trace_field_state || cpu.x[30] - main_base != 0x15fd47c) {
        apply();
        return;
    }
    const auto sample = string_samples.fetch_add(1, std::memory_order_relaxed);
    if (sample >= 12) {
        apply();
        return;
    }
    log("acnh field state %s entry=1632d80 sample=%u base=%llx object=%llx descriptor=%llx before=%s",
        use_cpp_strings ? "cpp" : "original", sample,
        static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(object),
        ld<unsigned long long>(main_base + 0x4ab2bc0), field_bytes(object, 0x200).c_str());
    apply();
    log("acnh field state result sample=%u after=%s out=%llx", sample,
        field_bytes(object, 0x200).c_str(), static_cast<unsigned long long>(cpu.x[0]));
}

void gameplay_percent_adapter(CPU& cpu) {
    const auto apply = [&] {
        if (use_cpp_percent) {
            GuestMemory memory;
            cpu.x[0] = acnh::draw_gameplay_percent(main_base, memory);
        } else original_gameplay_percent(cpu);
    };
    if (!trace_field_state || cpu.x[30] - main_base != 0x21bc414 ||
        percent_samples.fetch_add(1, std::memory_order_relaxed) >= 12) {
        apply();
        return;
    }
    const uint64_t address = main_base + 0x504cb58;
    const auto before = field_bytes(address, 16);
    apply();
    log("acnh field state %s entry=112b8ec base=%llx object=0 descriptor=0 before=%s after=%s out=%llx",
        use_cpp_percent ? "cpp" : "original", static_cast<unsigned long long>(main_base),
        before.c_str(), field_bytes(address, 16).c_str(), static_cast<unsigned long long>(cpu.x[0]));
}

Fn original_terminated_hash = nullptr;
bool use_cpp_hash = false;
bool trace_hash = false;
std::atomic<unsigned> hash_samples{0};

void terminated_hash_adapter(CPU& cpu) {
    const uint64_t address = cpu.x[0];
    const uint32_t seed = static_cast<uint32_t>(cpu.x[1]);
    const uint64_t caller = cpu.x[30] - main_base;
    const bool trace = trace_hash && (caller == 0x5bd7a8 ||
        (caller >= 0x15dec40 && caller < 0x15e026c)) &&
        hash_samples.fetch_add(1, std::memory_order_relaxed) < 48;
    std::string bytes;
    if (trace) {
        size_t length = 0;
        while (length < 512 && ld<uint8_t>(address + length)) ++length;
        if (length < 512) bytes = field_bytes(address, length + 1);
    }
    if (use_cpp_hash) cpu.x[0] = acnh::terminated_hash(address, seed, GuestMemory{});
    else original_terminated_hash(cpu);
    if (!bytes.empty())
        log("acnh terminated hash %s caller=%llx seed=%x w0=%x bytes=%s",
            use_cpp_hash ? "cpp" : "original", static_cast<unsigned long long>(caller), seed,
            static_cast<uint32_t>(cpu.x[0]), bytes.c_str());
}

Fn original_half_tile = nullptr;
bool use_cpp_half_tile = false;
bool trace_half_tile = false;
std::atomic<unsigned> half_tile_samples{0};

void half_tile_adapter(CPU& cpu) {
    const uint64_t grid = cpu.x[0];
    const uint64_t query = cpu.x[1];
    GuestMemory memory;
    const auto apply = [&] {
        if (use_cpp_half_tile) {
            const acnh::FieldQueries fields{main_base, memory};
            const acnh::HalfTile tile{ld<int32_t>(query + 8), ld<int32_t>(query + 12)};
            cpu.x[0] = fields.record_at({grid}, tile).address;
        }
        else original_half_tile(cpu);
    };
    const uint64_t caller = cpu.x[30] - main_base;
    if (!trace_half_tile || caller != 0x21f7880 ||
        half_tile_samples.fetch_add(1, std::memory_order_relaxed) >= 24) {
        apply();
        return;
    }

    const auto x = acnh::half_tile_coordinate(ld<int32_t>(query + 8));
    const auto z = acnh::half_tile_coordinate(ld<int32_t>(query + 12));
    const uint64_t record = acnh::tile_record(main_base + 0x50a50f0, grid, x, z, memory);
    const auto grid_bytes = field_bytes(grid, 0xa8);
    const auto query_bytes = field_bytes(query, 16);
    const auto record_bytes = field_bytes(record, 0x68);
    apply();
    log("acnh half tile %s caller=%llx base=%llx grid=%llx query=%llx record=%llx out=%llx g=%s q=%s r=%s",
        use_cpp_half_tile ? "cpp" : "original", static_cast<unsigned long long>(caller),
        static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(grid),
        static_cast<unsigned long long>(query), static_cast<unsigned long long>(record),
        static_cast<unsigned long long>(cpu.x[0]), grid_bytes.c_str(), query_bytes.c_str(),
        record_bytes.c_str());
}

Fn original_category_mask = nullptr;
bool use_cpp_category_mask = false;
bool trace_category_mask = false;
std::atomic<unsigned> category_mask_samples{0};

void category_mask_adapter(CPU& cpu) {
    const uint64_t owner = cpu.x[0];
    const uint64_t query = cpu.x[1];
    const uint64_t mask = cpu.x[2];
    GuestMemory memory;
    const auto apply = [&] {
        if (use_cpp_category_mask) cpu.x[0] = acnh::tile_category_mask(owner, query, mask, memory);
        else original_category_mask(cpu);
    };
    const uint64_t caller = cpu.x[30] - main_base;
    if (!trace_category_mask || (caller != 0x14c4680 && caller != 0x14c46e8) ||
        category_mask_samples.fetch_add(1, std::memory_order_relaxed) >= 12) {
        apply();
        return;
    }

    const auto x = ld<int32_t>(query + 8);
    const auto z = ld<int32_t>(query + 12);
    const uint64_t grid = x >= 0 && z >= 0 ? ld<uint64_t>(owner + 0x80) : 0;
    const uint64_t record = acnh::tile_category_record(owner, x, z, memory);
    const auto grid_bytes = grid ? field_bytes(grid + 0x38, 0x20) : "-";
    const auto query_bytes = field_bytes(query + 8, 8);
    const auto record_bytes = record ? field_bytes(record + 0x30, 4) : "-";
    const auto mask_bytes = record ? field_bytes(mask, 12) : "-";
    apply();
    log("acnh tile category mask %s caller=%llx base=%llx owner=%llx query=%llx mask=%llx grid=%llx record=%llx out=%llx g=%s q=%s r=%s m=%s",
        use_cpp_category_mask ? "cpp" : "original", static_cast<unsigned long long>(caller),
        static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(owner),
        static_cast<unsigned long long>(query), static_cast<unsigned long long>(mask),
        static_cast<unsigned long long>(grid), static_cast<unsigned long long>(record),
        static_cast<unsigned long long>(cpu.x[0]), grid_bytes.c_str(), query_bytes.c_str(),
        record_bytes.c_str(), mask_bytes.c_str());
}

Fn original_field_case_key = nullptr;
bool use_cpp_field_case_key = false;
bool trace_field_case_key = false;
std::atomic<unsigned> field_case_key_samples{0};

void field_case_key_adapter(CPU& cpu) {
    const std::array<uint8_t, 4> codes{
        static_cast<uint8_t>(cpu.x[0]), static_cast<uint8_t>(cpu.x[1]),
        static_cast<uint8_t>(cpu.x[2]), static_cast<uint8_t>(cpu.x[3]),
    };
    const uint64_t caller = cpu.x[30] - main_base;
    if (use_cpp_field_case_key) cpu.x[0] = acnh::field_case_key(main_base, codes, GuestMemory{});
    else original_field_case_key(cpu);
    if (trace_field_case_key && caller == 0x1141814 &&
        field_case_key_samples.fetch_add(1, std::memory_order_relaxed) < 12) {
        log("acnh field case key %s caller=%llx base=%llx args=%x,%x,%x,%x table=%s out=%llx",
            use_cpp_field_case_key ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(main_base), unsigned(codes[0]), unsigned(codes[1]),
            unsigned(codes[2]), unsigned(codes[3]), field_bytes(main_base + 0x40749e0, 3).c_str(),
            static_cast<unsigned long long>(cpu.x[0]));
    }
}

Fn original_shovel_survey_init = nullptr;
bool use_cpp_shovel_survey_init = false;
bool trace_shovel_survey_init = false;
std::atomic<unsigned> survey_init_samples{0};

void shovel_survey_init_adapter(CPU& cpu) {
    const acnh::SurveyScratch scratch{cpu.x[0]};
    const uint64_t caller = cpu.x[30] - main_base;
    const auto apply = [&] {
        if (use_cpp_shovel_survey_init) {
            GuestMemory memory;
            acnh::FieldQueries{main_base, memory}.initialize_survey(scratch);
        } else {
            original_shovel_survey_init(cpu);
        }
    };
    if (!trace_shovel_survey_init || (caller != 0x21f75a0 && caller != 0x21f75a8) ||
        survey_init_samples.fetch_add(1, std::memory_order_relaxed) >= 16) {
        apply();
        return;
    }

    const auto empty = field_bytes(main_base + 0x5486008, 8);
    const auto before = field_bytes(scratch.address, 0x178);
    apply();
    log("acnh shovel scratch %s caller=%llx base=%llx object=%llx empty=%s before=%s after=%s out=%llx",
        use_cpp_shovel_survey_init ? "cpp" : "original", static_cast<unsigned long long>(caller),
        static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(scratch.address),
        empty.c_str(), before.c_str(), field_bytes(scratch.address, 0x178).c_str(),
        static_cast<unsigned long long>(cpu.x[0]));
}

struct SurveyItemServices {
    const CPU& caller;
    uint32_t index = 0;

    void initialize(acnh::FieldItem item) {
        CPU nested = caller;
        // The last original initializer is a tail call with the caller's frame restored.
        if (index < 35) {
            nested.x[31] -= 0x20;
            nested.x[29] = nested.x[31];
            st<uint64_t>(nested.x[29], caller.x[29]);
            st<uint64_t>(nested.x[29] + 8, caller.x[30]);
            nested.x[30] = main_base + 0x127c540 + 12ull * index;
        }
        ++index;
        nested.x[0] = item.address;
        call(nested, main_base + 0x267d4ac);
    }
};

Fn original_survey_items = nullptr;
bool use_cpp_survey_items = false;
bool trace_survey_items = false;
std::atomic<unsigned> survey_items_samples{0};

void survey_items_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    if (caller != 0x21f7574) {
        original_survey_items(cpu);
        return;
    }

    const acnh::FieldItem first{cpu.x[0]};
    const bool trace = trace_survey_items && survey_items_samples.fetch_add(1, std::memory_order_relaxed) < 12;
    const auto before = trace ? field_bytes(first.address, 36 * 8) : std::string{};
    if (use_cpp_survey_items) {
        GuestMemory memory;
        SurveyItemServices services{cpu};
        acnh::FieldQueries{main_base, memory}.initialize_survey_items(first, services);
    } else {
        original_survey_items(cpu);
    }
    if (trace)
        log("acnh shovel items %s caller=%llx before=%s after=%s",
            use_cpp_survey_items ? "cpp" : "original", static_cast<unsigned long long>(caller),
            before.c_str(), field_bytes(first.address, 36 * 8).c_str());
}

struct ShovelGatherServices {
    CPU cpu;
    uint64_t stack;
    unsigned phase = 0;

    explicit ShovelGatherServices(const CPU& caller) : cpu(caller), stack(caller.x[31] - 0x140) {
        cpu.x[31] = stack;
        cpu.x[29] = stack + 0xe0;
        st<uint64_t>(cpu.x[29], caller.x[29]);
        st<uint64_t>(cpu.x[29] + 8, caller.x[30]);
        st<uint64_t>(stack + 0x88, vtable(0x4abfe68));
        st<uint32_t>(stack + 0x90, 0);
    }

    static uint64_t vtable(uint64_t offset) { return ld<uint64_t>(main_base + offset) + 0x10; }

    uint32_t stage_hash(uint32_t index) const {
        CPU nested = cpu;
        nested.x[0] = ld<uint64_t>(ld<uint64_t>(main_base + 0x4ab2640));
        nested.x[1] = index;
        nested.x[30] = main_base + 0x127cb30;
        call(nested, ld<uint64_t>(ld<uint64_t>(nested.x[0]) + 0x18));
        nested.x[0] = ld<uint64_t>(nested.x[0]);
        nested.x[1] = 0;
        nested.x[30] = main_base + 0x127cb3c;
        call(nested, main_base + 0x5bd550);
        return static_cast<uint32_t>(nested.x[0]);
    }

    bool stage_rule(uint32_t key) const {
        CPU nested = cpu;
        nested.x[0] = key;
        nested.x[30] = main_base + 0x127cb6c;
        call(nested, main_base + 0x16baf44);
        return (nested.x[0] & 1) != 0;
    }

    void set_stage(uint32_t key, bool rule) const {
        st<uint32_t>(stack + 0x94, key);
        st<uint8_t>(stack + 0x98, rule);
    }

    acnh::SurveyCell read(acnh::HalfTile point, bool anchor) {
        constexpr uint64_t returns[]{0x127cc88, 0x127cf88, 0x127cfd4, 0x127cffc};
        const uint64_t position = stack + (anchor ? 0x78 : 0x28);
        const uint64_t root = stack + 0x68;
        const uint64_t result = stack + (anchor ? 0x60 : 0x78);
        st<uint64_t>(position, vtable(anchor ? 0x4ab2010 : 0x4ab2018));
        st<int32_t>(position + 8, point.x);
        st<int32_t>(position + 12, point.z);
        if (anchor) {
            st<uint64_t>(root, vtable(0x4ab2010));
            st<uint64_t>(root + 8, 0);
        }

        CPU nested = cpu;
        nested.x[0] = stack + 0x88;
        nested.x[1] = 0;
        nested.x[2] = position;
        nested.x[3] = anchor ? root : 0;
        nested.x[8] = result;
        nested.x[30] = main_base + returns[phase];
        phase = (phase + 1) % 4;
        call(nested, main_base + 0x195033c);
        return {ld<uint64_t>(result), anchor
            ? acnh::HalfTile{ld<int32_t>(root + 8), ld<int32_t>(root + 12)} : acnh::HalfTile{}};
    }

    uint32_t item_id(uint64_t item, acnh::SurveyItemUse use) const {
        constexpr uint64_t returns[]{0x127ccbc, 0x127cd10, 0x127cd64, 0x127ce08, 0x127ce54};
        uint64_t record = stack + 0x60;
        if (use == acnh::SurveyItemUse::metadata_key || use == acnh::SurveyItemUse::extent) {
            const uint64_t type = use == acnh::SurveyItemUse::metadata_key ? 0x4ac0510 : 0x4ac35c0;
            st<uint64_t>(stack + 0x28, vtable(type));
            record = stack + 0x30;
        }
        st<uint64_t>(record, item);
        CPU nested = cpu;
        nested.x[0] = record;
        nested.x[30] = main_base + returns[static_cast<size_t>(use)];
        call(nested, main_base + 0x267d6c4);
        return static_cast<uint32_t>(nested.x[0]);
    }

    bool metadata_occupied(uint64_t item, uint64_t detail) const {
        st<uint64_t>(stack + 0x28, vtable(0x4ac0510));
        st<uint64_t>(stack + 0x30, item);
        st<uint64_t>(stack + 0x38, detail);
        st<uint32_t>(stack + 0x50, 0);
        CPU nested = cpu;
        nested.x[0] = stack + 0x28;
        nested.x[30] = main_base + 0x127cd98;
        call(nested, main_base + 0x19ee3b4);
        return static_cast<uint32_t>(nested.x[0]) == ld<uint32_t>(stack + 0x50);
    }

    bool single_tile_extent(uint64_t item) const {
        st<uint64_t>(stack + 0x50, vtable(0x4ac35c0));
        st<uint64_t>(stack + 0x58, item);
        st<uint64_t>(stack + 0x28, vtable(0x4abc1a0));
        st<uint64_t>(stack + 0x30, vtable(0x4ab2010));
        st<uint64_t>(stack + 0x38, 0);
        st<uint64_t>(stack + 0x40, vtable(0x4ab2010));
        st<uint64_t>(stack + 0x48, 0);
        CPU nested = cpu;
        nested.x[0] = stack + 0x50;
        nested.x[1] = stack + 0x28;
        nested.x[30] = main_base + 0x127cea4;
        call(nested, main_base + 0x253fe7c);
        return ld<uint32_t>(stack + 0x48) - ld<uint32_t>(stack + 0x38) == 1 &&
               ld<uint32_t>(stack + 0x4c) - ld<uint32_t>(stack + 0x3c) == 1;
    }
};

Fn original_shovel_gather = nullptr;
bool use_cpp_shovel_gather = false;
bool trace_shovel_gather = false;
std::atomic<unsigned> shovel_gather_samples{0};

void shovel_gather_adapter(CPU& cpu) {
    const uint64_t caller = cpu.x[30] - main_base;
    if (caller != 0x21f7618) {
        original_shovel_gather(cpu);
        return;
    }

    const acnh::SurveyScratch scratch{cpu.x[0]};
    const bool trace = trace_shovel_gather && shovel_gather_samples.fetch_add(1, std::memory_order_relaxed) < 12;
    const auto before = trace ? field_bytes(scratch.address, 0x178) : std::string{};
    if (use_cpp_shovel_gather) {
        GuestMemory memory;
        ShovelGatherServices services{cpu};
        acnh::gather_shovel_neighborhood(main_base, scratch, memory, services);
    } else {
        original_shovel_gather(cpu);
    }
    if (trace)
        log("acnh shovel gather %s caller=%llx base=%llx scratch=%llx before=%s after=%s",
            use_cpp_shovel_gather ? "cpp" : "original", static_cast<unsigned long long>(caller),
            static_cast<unsigned long long>(main_base), static_cast<unsigned long long>(scratch.address),
            before.c_str(), field_bytes(scratch.address, 0x178).c_str());
}

void install_acnh_action(const ModuleInfo& module, FuncEntry* entries) {
    if (std::string_view(module.name) != "main" ||
        mod_package::build_id(plat::data_root()) != camera303::build_id) return;
    main_base = module.base;
    const std::span<FuncEntry> functions{entries, module.func_count};
    const char* mode = std::getenv("NX_ACNH_ACTION_MASK");
    use_cpp = !mode || std::string_view(mode) == "cpp";
    trace_mask = std::getenv("NX_ACNH_ACTION_MASK_TRACE") != nullptr;
    if (use_cpp || trace_mask)
        wrap_function(functions, mask_entry, action_mask_adapter, original_mask);

    mode = std::getenv("NX_ACNH_INLINE_STRINGS");
    use_cpp_strings = !mode || std::string_view(mode) == "cpp";
    mode = std::getenv("NX_ACNH_GAMEPLAY_PERCENT");
    use_cpp_percent = !mode || std::string_view(mode) == "cpp";
    trace_field_state = std::getenv("NX_ACNH_FIELD_STATE_TRACE") != nullptr;
    if (use_cpp_strings || trace_field_state)
        wrap_function(functions, 0x1632d80, inline_strings_adapter, original_inline_strings);
    if (use_cpp_percent || trace_field_state)
        wrap_function(functions, 0x112b8ec, gameplay_percent_adapter, original_gameplay_percent);

    mode = std::getenv("NX_ACNH_TERMINATED_HASH");
    use_cpp_hash = !mode || std::string_view(mode) == "cpp";
    trace_hash = std::getenv("NX_ACNH_TERMINATED_HASH_TRACE") != nullptr;
    if (use_cpp_hash || trace_hash)
        wrap_function(functions, 0x5bd550, terminated_hash_adapter, original_terminated_hash);

    mode = std::getenv("NX_ACNH_HALF_TILE_RECORD");
    use_cpp_half_tile = !mode || std::string_view(mode) == "cpp";
    trace_half_tile = std::getenv("NX_ACNH_HALF_TILE_RECORD_TRACE") != nullptr;
    if (use_cpp_half_tile || trace_half_tile)
        wrap_function(functions, 0x1281c54, half_tile_adapter, original_half_tile);

    mode = std::getenv("NX_ACNH_SHOVEL_SURVEY_INIT");
    use_cpp_shovel_survey_init = !mode || std::string_view(mode) == "cpp";
    trace_shovel_survey_init = std::getenv("NX_ACNH_SHOVEL_SURVEY_INIT_TRACE") != nullptr;
    if (use_cpp_shovel_survey_init || trace_shovel_survey_init)
        wrap_function(functions, 0x127c6ec, shovel_survey_init_adapter, original_shovel_survey_init);

    mode = std::getenv("NX_ACNH_SHOVEL_SURVEY_ITEMS");
    use_cpp_survey_items = !mode || std::string_view(mode) == "cpp";
    trace_survey_items = std::getenv("NX_ACNH_SHOVEL_SURVEY_ITEMS_TRACE") != nullptr;
    if (use_cpp_survey_items || trace_survey_items)
        wrap_function(functions, 0x127c524, survey_items_adapter, original_survey_items);

    mode = std::getenv("NX_ACNH_SHOVEL_GATHER");
    use_cpp_shovel_gather = !mode || std::string_view(mode) == "cpp";
    trace_shovel_gather = std::getenv("NX_ACNH_SHOVEL_GATHER_TRACE") != nullptr;
    if (use_cpp_shovel_gather || trace_shovel_gather)
        wrap_function(functions, 0x127cab0, shovel_gather_adapter, original_shovel_gather);

    mode = std::getenv("NX_ACNH_TILE_CATEGORY_MASK");
    use_cpp_category_mask = !mode || std::string_view(mode) == "cpp";
    trace_category_mask = std::getenv("NX_ACNH_TILE_CATEGORY_MASK_TRACE") != nullptr;
    if (use_cpp_category_mask || trace_category_mask)
        wrap_function(functions, 0x849898, category_mask_adapter, original_category_mask);

    mode = std::getenv("NX_ACNH_FIELD_CASE_KEY");
    use_cpp_field_case_key = !mode || std::string_view(mode) == "cpp";
    trace_field_case_key = std::getenv("NX_ACNH_FIELD_CASE_KEY_TRACE") != nullptr;
    if (use_cpp_field_case_key || trace_field_case_key)
        wrap_function(functions, 0x1140fd0, field_case_key_adapter, original_field_case_key);
}

}  // namespace

void register_acnh_action(Registry& registry) {
    Definition feature;
    feature.id = "acnh.action";
    feature.module_loaded = install_acnh_action;
    registry.add(std::move(feature));
}

}  // namespace nx::features
