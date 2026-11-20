#include "../../../games/acnh/angle_difference.h"
#include "../../../games/acnh/approach_position.h"
#include "../../../games/acnh/heading.h"

#include "features/camera/version_303.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "mod_package.h"
#include "platform.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <nx/runtime.h>

namespace nx::features {
namespace {

constexpr uint64_t heading_entry = 0x5f0400;
constexpr uint64_t angle_difference_entry = 0x5f05f8;
constexpr uint64_t approach_entry = 0x3db4b60;
constexpr uint64_t heading_table = 0x409a7a8;
constexpr uint64_t shake_return = 0x23e03dc;
constexpr uint64_t approach_shake_return = 0x23e0594;
std::array<acnh::HeadingSample, 129> table;
Fn original_heading = nullptr;
uint64_t main_base = 0;
bool use_cpp_heading = false;
bool trace_heading = false;
Fn original_angle_difference = nullptr;
bool use_cpp_angle_difference = false;
bool trace_angle_difference = false;
Fn original_approach = nullptr;
bool use_cpp_approach = false;
bool trace_approach = false;

void heading_adapter(CPU& cpu) {
    const float x = cpu.v[0].f32[0];
    const float z = cpu.v[1].f32[0];
    const bool replaced = use_cpp_heading && std::isfinite(x) && std::isfinite(z);
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

void angle_difference_adapter(CPU& cpu) {
    const float first = cpu.v[0].f32[0];
    const float second = cpu.v[1].f32[0];
    const uint64_t destination = cpu.x[0];
    const uint64_t caller = cpu.x[30] - main_base;
    const bool replaced = use_cpp_angle_difference && std::isfinite(first) && std::isfinite(second);
    if (replaced) {
        const acnh::AngleDifference result = acnh::angle_difference(first, second);
        st<float>(destination, result.distance);
        cpu.x[0] = static_cast<uint32_t>(result.direction);
    } else {
        original_angle_difference(cpu);
    }
    if (trace_angle_difference) {
        log("acnh angle difference %s caller=%08x s0=%08x s1=%08x w0=%08x distance=%08x",
            replaced ? "cpp" : "original", static_cast<uint32_t>(caller),
            std::bit_cast<uint32_t>(first), std::bit_cast<uint32_t>(second),
            static_cast<uint32_t>(cpu.x[0]), ld<uint32_t>(destination));
    }
}

acnh::Position guest_position(uint64_t address) {
    return {ld<float>(address), ld<float>(address + 4), ld<float>(address + 8)};
}

void approach_adapter(CPU& cpu) {
    const uint64_t destination = cpu.x[0];
    const uint64_t target_address = cpu.x[1];
    const bool trace = trace_approach && cpu.x[30] == main_base + approach_shake_return;
    if (!use_cpp_approach && !trace) {
        original_approach(cpu);
        return;
    }

    const auto position = guest_position(destination);
    const auto target = guest_position(target_address);
    const float gain = cpu.v[0].f32[0];
    const float maximum = cpu.v[1].f32[0];
    const float minimum = cpu.v[2].f32[0];
    const bool disjoint = std::max(destination, target_address) - std::min(destination, target_address) >= 12;
    const auto supported = [](float value) { return std::fabs(value) <= 0x1p20f; };
    const bool replaced = use_cpp_approach && disjoint &&
        std::ranges::all_of(position, supported) && std::ranges::all_of(target, supported) &&
        supported(gain) && supported(maximum) && supported(minimum);
    if (replaced) {
        const auto result = acnh::approach_position(position, target, gain, maximum, minimum);
        if (result.wrote) {
            for (size_t i = 0; i < result.position.size(); ++i)
                st<float>(destination + 4 * i, result.position[i]);
        }
        cpu.v[0].f32[0] = result.remaining;
    } else {
        original_approach(cpu);
    }
    if (trace) {
        const auto after = guest_position(destination);
        const auto bits = [](float value) { return std::bit_cast<uint32_t>(value); };
        log("acnh position approach %s p=%08x,%08x,%08x t=%08x,%08x,%08x args=%08x,%08x,%08x "
            "after=%08x,%08x,%08x s0=%08x",
            replaced ? "cpp" : "original", bits(position[0]), bits(position[1]), bits(position[2]),
            bits(target[0]), bits(target[1]), bits(target[2]), bits(gain), bits(maximum), bits(minimum),
            bits(after[0]), bits(after[1]), bits(after[2]), bits(cpu.v[0].f32[0]));
    }
}

void install_heading(const ModuleInfo& module, std::span<FuncEntry> entries) {
    const char* mode = std::getenv("NX_ACNH_HEADING");
    use_cpp_heading = !mode || std::string_view(mode) == "cpp";
    trace_heading = std::getenv("NX_ACNH_HEADING_TRACE") != nullptr;
    if (!use_cpp_heading && !trace_heading) return;
    static_assert(sizeof(acnh::HeadingSample) == 8);
    if (heading_table < module.ro || heading_table - module.ro > module.ro_size ||
        sizeof(table) > module.ro_size - (heading_table - module.ro)) return;
    if (!wrap_function(entries, heading_entry, heading_adapter, original_heading)) return;

    std::memcpy(table.data(), reinterpret_cast<const void*>(main_base + heading_table), sizeof(table));
}

void install_math_adapters(const ModuleInfo& module, FuncEntry* functions) {
    if (std::string_view(module.name) != "main" ||
        mod_package::build_id(plat::data_root()) != camera303::build_id) return;

    main_base = module.base;
    std::span<FuncEntry> entries{functions, module.func_count};
    install_heading(module, entries);
    const char* mode = std::getenv("NX_ACNH_ANGLE_DIFFERENCE");
    use_cpp_angle_difference = !mode || std::string_view(mode) == "cpp";
    trace_angle_difference = std::getenv("NX_ACNH_ANGLE_DIFFERENCE_TRACE") != nullptr;
    if (use_cpp_angle_difference || trace_angle_difference)
        wrap_function(entries, angle_difference_entry, angle_difference_adapter, original_angle_difference);

    mode = std::getenv("NX_ACNH_APPROACH");
    use_cpp_approach = !mode || std::string_view(mode) == "cpp";
    trace_approach = std::getenv("NX_ACNH_APPROACH_TRACE") != nullptr;
    if (use_cpp_approach || trace_approach)
        wrap_function(entries, approach_entry, approach_adapter, original_approach);
}

}  // namespace

void register_acnh_math(Registry& registry) {
    Definition feature;
    feature.id = "acnh.math";
    feature.module_loaded = install_math_adapters;
    registry.add(std::move(feature));
}

}  // namespace nx::features
