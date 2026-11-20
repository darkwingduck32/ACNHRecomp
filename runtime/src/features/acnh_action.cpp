#include "../../../games/acnh/action_mask.h"

#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

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

void install_action_mask(const ModuleInfo& module, FuncEntry* entries) {
    if (std::string_view(module.name) != "main" ||
        mod_package::build_id(plat::data_root()) != camera303::build_id) return;
    const char* mode = std::getenv("NX_ACNH_ACTION_MASK");
    use_cpp = !mode || std::string_view(mode) == "cpp";
    trace_mask = std::getenv("NX_ACNH_ACTION_MASK_TRACE") != nullptr;
    if (!use_cpp && !trace_mask) return;
    main_base = module.base;
    wrap_function({entries, module.func_count}, mask_entry, action_mask_adapter, original_mask);
}

}  // namespace

void register_acnh_action(Registry& registry) {
    Definition feature;
    feature.id = "acnh.action";
    feature.module_loaded = install_action_mask;
    registry.add(std::move(feature));
}

}  // namespace nx::features
