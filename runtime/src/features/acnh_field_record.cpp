#include "../../../games/acnh/field_record.h"

#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <nx/runtime.h>

namespace nx::features {
namespace {

constexpr uint64_t entry = 0x8807cc;
Fn original_field_record = nullptr;
uint64_t main_base = 0;
bool use_cpp = false;
bool trace_field_record = false;

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

void install_field_record(const ModuleInfo& module, FuncEntry* functions) {
    if (std::string_view(module.name) != "main" ||
        mod_package::build_id(plat::data_root()) != camera303::build_id) return;
    const char* mode = std::getenv("NX_ACNH_FIELD_RECORD");
    use_cpp = !mode || std::string_view(mode) == "cpp";
    trace_field_record = std::getenv("NX_ACNH_FIELD_RECORD_TRACE") != nullptr;
    if (!use_cpp && !trace_field_record) return;
    main_base = module.base;
    wrap_function({functions, module.func_count}, entry, field_record_adapter, original_field_record);
}

}  // namespace

void register_acnh_field_record(Registry& registry) {
    Definition feature;
    feature.id = "acnh.field-record";
    feature.module_loaded = install_field_record;
    registry.add(std::move(feature));
}

}  // namespace nx::features
