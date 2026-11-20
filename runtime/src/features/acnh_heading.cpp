#include "../../../games/acnh/heading.h"

#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <nx/runtime.h>

namespace nx::features {
namespace {

constexpr uint64_t entry = 0x5f0400;
constexpr uint64_t heading_table = 0x409a7a8;
constexpr uint64_t shake_return = 0x23e03dc;
std::array<acnh::HeadingSample, 129> table;
Fn original_heading = nullptr;
uint64_t main_base = 0;
bool use_cpp = false;
bool trace_heading = false;

void heading_adapter(CPU& cpu) {
    const float x = cpu.v[0].f32[0];
    const float z = cpu.v[1].f32[0];
    const bool replaced = use_cpp && std::isfinite(x) && std::isfinite(z);
    const bool trace = trace_heading && cpu.x[30] == main_base + shake_return;
    if (replaced) {
        cpu.x[0] = acnh::heading(x, z, table);
    } else {
        original_heading(cpu);
    }
    if (trace) {
        log("acnh heading tree-shake %s s0=%08x s1=%08x w0=%08x",
            replaced ? "cpp" : "original", std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(z),
            static_cast<uint32_t>(cpu.x[0]));
    }
}

void install_heading(const ModuleInfo& module, FuncEntry* entries) {
    if (std::string_view(module.name) != "main") return;
    const char* mode = std::getenv("NX_ACNH_HEADING");
    use_cpp = !mode || std::string_view(mode) == "cpp";
    trace_heading = std::getenv("NX_ACNH_HEADING_TRACE") != nullptr;
    if (!use_cpp && !trace_heading) return;
    if (mod_package::build_id(plat::data_root()) != camera303::build_id) return;
    static_assert(sizeof(acnh::HeadingSample) == 8);
    if (heading_table < module.ro || heading_table - module.ro > module.ro_size ||
        sizeof(table) > module.ro_size - (heading_table - module.ro)) return;
    if (!wrap_function({entries, module.func_count}, entry, heading_adapter, original_heading)) return;

    main_base = module.base;
    std::memcpy(table.data(), reinterpret_cast<const void*>(main_base + heading_table), sizeof(table));
}

}  // namespace

void register_acnh_heading(Registry& registry) {
    Definition feature;
    feature.id = "acnh.heading";
    feature.module_loaded = install_heading;
    registry.add(std::move(feature));
}

}  // namespace nx::features
