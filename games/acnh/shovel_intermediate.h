#pragma once

#include "shovel_stage_lookup.h"
#include "shovel_target.h"

namespace nx::acnh {

struct ShovelIntermediateQuery {
    uint64_t point;
    float radius;
    bool check_obstruction;
    bool surface_only;
    bool skip_blocked_offsets;
};

enum class ShovelIntermediateHeight { center_stage, center_surface, offset };
enum class ShovelIntermediateCast { center, skip_offset, check_offset };

template<class Memory, class Services>
bool shovel_intermediate_point(uint64_t base, ShovelIntermediateQuery query, const Memory& memory, Services& services) {
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const auto point_at = [&](uint64_t address) {
        return ShovelTargetPoint{read_float(address), read_float(address + 4), read_float(address + 8)};
    };
    const auto stage_at = [&](bool center) {
        const auto scene = center ? memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab2720))
                                  : memory.template read<uint64_t>(base + 0x59ed670);
        uint64_t stage = base + 0x54c04a0;
        if (scene && memory.template read<uint64_t>(scene + 0x330)) {
            const auto owner = memory.template read<uint64_t>(base + 0x59ed668);
            const auto index = memory.template read<uint32_t>(scene + 0x338);
            if (services.stage_hash(owner, index, center) == 0x758f7eaf)
                stage = memory.template read<uint64_t>(base + 0x4ac3a80);
        }
        return shovel_stage_ordinal(base, stage, memory);
    };

    if (query.surface_only && services.blocked_at_center(query.point, query.radius)) return false;
    const auto center = query.surface_only ? ShovelIntermediateHeight::center_surface : ShovelIntermediateHeight::center_stage;
    const float reference = services.surface_height(query.point, center);
    if (!query.surface_only && std::fabs(reference - services.stage_height(query.point, stage_at(true))) > 1.f)
        return false;
    if (std::fabs(query.radius) <= 0x1p-23f) return true;
    if (query.check_obstruction
        && services.cast(point_at(query.point), query.radius, ShovelIntermediateCast::center))
        return false;

    const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00);
    const float zero_offset = query.radius * 0.f;
    for (unsigned sector = 0; sector < 8; ++sector) {
        const uint32_t heading = memory.template read<uint32_t>(base + 0x5140230) * sector;
        const auto row = trig + 16 * (heading >> 24);
        const float fraction = static_cast<float>(heading & 0xffffffu) * 0x1p-24f;
        const float dx = std::fma(read_float(row + 4), fraction, read_float(row)) * query.radius;
        const float dz = std::fma(read_float(row + 12), fraction, read_float(row + 8)) * query.radius;
        const auto origin = point_at(query.point);
        const ShovelTargetPoint point{origin.x + dx, origin.y + zero_offset, origin.z + dz};
        services.prepare_offset(point);
        if (query.skip_blocked_offsets && services.cast(point, 0.f, ShovelIntermediateCast::skip_offset)) continue;
        const float height = query.surface_only ? services.offset_surface_height() : services.offset_stage_height(stage_at(false));
        if (query.check_obstruction && services.cast(point, 0.f, ShovelIntermediateCast::check_offset)) return false;
        if (std::fabs(reference - height) > 1.f) return false;
    }
    return true;
}

} // namespace nx::acnh
