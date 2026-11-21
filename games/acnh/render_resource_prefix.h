#pragma once

#include <cstdint>

namespace nx::acnh {

struct RenderStateRef { uint64_t address; };
struct RenderSlotTableRef { uint64_t address; };

enum class RenderResourceRoute { empty, changed, matching };

struct RenderResourcePrefix {
    RenderResourceRoute route{};
    int32_t lane{};
    uint64_t record{};
    uint64_t resource{};
    uint64_t key{};
    uint64_t binding{};
    uint64_t previous_binding{};
    uint32_t packed{};
    uint32_t shifted{};
};

template<class Memory>
RenderResourcePrefix select_render_resource_prefix(RenderStateRef state, RenderSlotTableRef slots,
                                                   int8_t request, const Memory& memory) {
    RenderResourcePrefix result{};
    result.lane = request;
    const auto category = memory.template read<uint8_t>(state.address + 0x21);
    if (request == -1) {
        result.packed = memory.template read<uint32_t>(slots.address + 0x230 + ((category >> 1) & 0x7c));
        result.shifted = result.packed >> ((category & 7) * 2);
        result.lane = result.shifted & 3;
    }
    const int32_t index = 4 * category + result.lane;
    result.record = memory.template read<uint64_t>(slots.address + 0x30 + 8 * index);
    result.resource = memory.template read<uint64_t>(result.record);
    if (!result.resource) return result;

    result.key = memory.template read<uint64_t>(result.record + 8);
    const auto previous_key = memory.template read<uint64_t>(state.address + 0x1c8);
    result.binding = memory.template read<uint64_t>(slots.address + 0x20);
    result.route = RenderResourceRoute::changed;
    if (result.key == previous_key) {
        result.route = RenderResourceRoute::matching;
        result.previous_binding = memory.template read<uint64_t>(state.address + 0x1c0);
    }
    return result;
}

} // namespace nx::acnh
