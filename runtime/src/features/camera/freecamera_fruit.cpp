#include "native.h"
#include "version_303.h"
// The game shares these origins between attached fruit and fruit-drop setup.
// Keep the model-local table, rather than translating the fruit mesh (which is
// also used for loose items). Opt-in profiles accompany the reviewed tree mod.
#include "freecamera_fruit.h"
#include "freecamera_fruit_layout.h"
#include "platform.h"
#include <nx/runtime.h>
#include <cstring>
#include <sstream>
#include <mutex>
#include <atomic>

namespace nx {
namespace {
std::mutex fruit_lock;
freecamera::FruitLayouts layouts;
uint64_t oak_table = 0;
uint64_t palm_table = 0;
int active_variant = -1;
std::atomic<bool> ready{false};
freecamera::FruitPoints last_oak=freecamera::stock_oak_fruit;
std::array<float,6> last_palm=freecamera::stock_palm_fruit;
constexpr uint64_t palm_offset=camera303::offset_40746a8, oak_offset=camera303::offset_40746c0;

void select_variant(int variant) {
    if (!oak_table || variant<0 || variant==active_variant) return;
    const auto& points=variant==2?layouts.snow:variant==1?layouts.sakura:layouts.oak;
    if (std::memcmp(reinterpret_cast<void*>(oak_table),last_oak.data(),sizeof(last_oak))) return;
    std::memcpy(reinterpret_cast<void*>(oak_table),points.data(),sizeof(points));
    last_oak=points;
    active_variant=variant;
    log("freecamera: fruit attachment layout %s (shared hanging/drop origins)",
        variant==2?"snow oak":variant==1?"blossom oak":"oak");
}
}

void freecamera_fruit_install(const ModuleInfo* module, bool enabled) {
    if (!enabled || std::strcmp(module->name,"main")) return;
    std::istringstream input{std::string(camera_profile("tree_fruit_offsets.txt"))};
    freecamera::FruitLayouts candidate;
    if (!freecamera::read_fruit_layouts(input,candidate)) {
        log("freecamera: invalid tree_fruit_offsets.txt; original fruit origins retained");
        return;
    }
    // Check the complete native table AND its consumer instructions. Other game
    // versions/layouts fail closed; no executable or game files are rewritten.
    if (oak_offset+sizeof(freecamera::stock_oak_fruit)>module->ro+module->ro_size ||
        palm_offset<module->ro || module->text_size<camera303::offset_21b747c ||
        ld<uint32_t>(module->base+camera303::offset_10e669c)!=camera303::instruction_d10483ff ||
        ld<uint32_t>(module->base+camera303::offset_21b7458)!=camera303::instruction_bd400100 ||
        std::memcmp(reinterpret_cast<void*>(module->base+palm_offset),freecamera::stock_palm_fruit.data(),sizeof(freecamera::stock_palm_fruit)) ||
        std::memcmp(reinterpret_cast<void*>(module->base+oak_offset),freecamera::stock_oak_fruit.data(),sizeof(freecamera::stock_oak_fruit))) {
        log("freecamera: fruit placement patch skipped: 3.0.3 fingerprint mismatch");
        return;
    }
    std::lock_guard guard(fruit_lock);
    layouts=candidate;
    oak_table=module->base+oak_offset;
    palm_table=module->base+palm_offset;
    active_variant=-1;
    ready.store(true,std::memory_order_release);
}
bool freecamera_fruit_ready() { return ready.load(std::memory_order_acquire); }

void freecamera_fruit_resource(const std::string& guest_path, bool native_replacement) {
    if (guest_path.ends_with("/PltTreePalm.Nin_NX_NVN.zs")) {
        std::lock_guard guard(fruit_lock);
        if(palm_table && !std::memcmp(reinterpret_cast<void*>(palm_table),last_palm.data(),sizeof(last_palm))) {
            last_palm=native_replacement?layouts.palm:freecamera::stock_palm_fruit;
            std::memcpy(reinterpret_cast<void*>(palm_table),last_palm.data(),sizeof(last_palm));
        }
        return;
    }
    const int variant=freecamera::fruit_variant(guest_path);
    if (variant<0) return;
    // The outdoor tree resource determines the island's seasonal geometry.
    // Animation files and museum/furniture trees must not change the selection.
    std::lock_guard guard(fruit_lock);
    if (native_replacement) select_variant(variant);
    else if(oak_table && active_variant!=-1 &&
            !std::memcmp(reinterpret_cast<void*>(oak_table),last_oak.data(),sizeof(last_oak))) {
        std::memcpy(reinterpret_cast<void*>(oak_table),freecamera::stock_oak_fruit.data(),sizeof(freecamera::stock_oak_fruit));
        last_oak=freecamera::stock_oak_fruit;
        active_variant=-1;
    }
}
}
