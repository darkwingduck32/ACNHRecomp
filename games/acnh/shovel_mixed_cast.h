#pragma once

#include "shovel_simple_cast.h"
#include "shovel_terrain.h"

namespace nx::acnh {

enum class ShovelCastMaterial { base, overlay, overlay_code, overlay_property };
enum class ShovelCastProperty { base, overlay };

inline HalfTile shovel_cast_half_tile(ShovelTargetPoint point) {
    const auto tile = [](float value) { return std::isnan(value) ? 0 : terrain_half_tile(value); };
    return {tile(point.x), tile(point.z)};
}

inline HalfTile shovel_cast_full_tile(ShovelTargetPoint point) {
    const auto tile = shovel_cast_half_tile(point);
    return {int32_t(std::floor(static_cast<float>(tile.x) * 0.5f)),
            int32_t(std::floor(static_cast<float>(tile.z) * 0.5f))};
}

template<class Memory, class Services>
uint32_t shovel_mixed_cast_point(uint64_t base, ShovelTargetPoint& point, bool center,
                                 const Memory& memory, Services& services) {
    // Each sampler may update the point used by the next query.
    const auto ground = services.cast_material(point, ShovelCastMaterial::base, center);
    const auto overlay = services.cast_material(point, ShovelCastMaterial::overlay, center);
    if (services.material_flag(ground, ShovelCastProperty::base, center) && ground == overlay)
        return 1;

    if (services.cast_tile_flag(shovel_cast_full_tile(point), center)) return 2;

    if (services.cast_material(point, ShovelCastMaterial::overlay_code, center) == 0x2e)
        return 0x20;
    const auto kind = services.cast_material(point, ShovelCastMaterial::overlay_property, center);
    if (services.material_flag(kind, ShovelCastProperty::overlay, center)) return 0x40;

    if (!services.probe(point, ShovelStepCastMask::height, center)) return 0;
    const auto stage = shovel_step_cast_stage_key(base, center, memory, services);
    services.record_height_contact(point, stage, center);
    return 4;
}

template<class Memory, class Services>
uint32_t shovel_mixed_step_cast(uint64_t base, ShovelStepCastQuery query, const Memory& memory, Services& services) {
    return shovel_walk_step_cast(base, query, memory, [&](ShovelTargetPoint& point, bool center) {
        return shovel_mixed_cast_point(base, point, center, memory, services);
    });
}

} // namespace nx::acnh
