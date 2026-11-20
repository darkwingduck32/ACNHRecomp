#pragma once

#include "shovel_height.h"
#include "shovel_stage_lookup.h"

namespace nx::acnh {

enum class ShovelCollisionProbe { height_difference, stage_item };

struct ShovelCollisionHeightQuery {
    uint64_t point;
    bool alternate_height;
    bool check_stage_locations;
};

struct ShovelCollisionItemQuery {
    uint64_t point;
    bool skip_distance;
};

template<class Memory, class Services>
uint64_t shovel_collision_stage(uint64_t base, ShovelCollisionProbe probe,
                                const Memory& memory, Services& services) {
    const auto fallback = base + 0x54c04a0;
    const auto scene = memory.template read<uint64_t>(memory.template read<uint64_t>(base + 0x4ab2720));
    if (!scene || !memory.template read<uint64_t>(scene + 0x330)) return fallback;
    const auto slot = probe == ShovelCollisionProbe::height_difference
        ? base + 0x59ed668 : memory.template read<uint64_t>(base + 0x4ab2640);
    const auto owner = memory.template read<uint64_t>(slot);
    const auto index = memory.template read<uint32_t>(scene + 0x338);
    return services.stage_hash(owner, index, probe) == 0x758f7eaf
        ? memory.template read<uint64_t>(base + 0x4ac3a80) : fallback;
}

template<class Memory>
TerrainWorldPoint shovel_collision_point(uint64_t point, const Memory& memory) {
    return {std::bit_cast<float>(memory.template read<uint32_t>(point)),
            std::bit_cast<float>(memory.template read<uint32_t>(point + 8))};
}

inline float shovel_collision_distance(TerrainWorldPoint a, TerrainWorldPoint b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(std::fma(dx, dx, dz * dz));
}

template<class Memory, class Services>
bool shovel_collision_height_difference(uint64_t base, ShovelCollisionHeightQuery query,
                                        const Memory& memory, Services& services) {
    const float surface = services.surface_height(query.point, ShovelCollisionProbe::height_difference);
    const auto stage = shovel_collision_stage(base, ShovelCollisionProbe::height_difference, memory, services);
    const auto ordinal = shovel_stage_ordinal(base, stage, memory);
    const float height = services.stage_height(query.point, query.alternate_height, ordinal);

    const auto scene = memory.template read<int16_t>(base + 0x4b487d4);
    if (shovel_height_scene_flag(base, scene, 25, memory)
        && memory.template read<uint32_t>(memory.template read<uint64_t>(base + 0x4ac3a80) + 0x38) == 0x91045f65
        && query.check_stage_locations) {
        const auto point = shovel_collision_point(query.point, memory);
        for (float x : {395.f, 385.f, 405.f}) {
            if (shovel_collision_distance({x, 650.f}, point) < 12.5f) return true;
        }
        if (shovel_collision_distance({430.f, 665.f}, point) < 10.f) return true;
    }
    return std::fabs(surface - height) > 0.1f;
}

template<class Memory, class Services>
bool shovel_collision_stage_item(uint64_t base, ShovelCollisionItemQuery query,
                                 const Memory& memory, Services& services) {
    const auto half_tile = [](float value) { return std::isnan(value) ? 0 : terrain_half_tile(value); };
    const auto initial = shovel_collision_point(query.point, memory);
    services.surface_height(query.point, ShovelCollisionProbe::stage_item);
    const auto current = shovel_collision_point(query.point, memory);
    const HalfTile tile{half_tile(current.x), half_tile(current.z)};
    const auto stage = shovel_collision_stage(base, ShovelCollisionProbe::stage_item, memory, services);
    const auto hash = memory.template read<uint32_t>(stage + 0x38);
    if (services.stage_item_id(tile, hash) != 0xea79) return false;
    if (query.skip_distance) return true;
    const TerrainWorldPoint center{static_cast<float>(half_tile(initial.x) & ~1) * 5.f + 5.f,
                                   static_cast<float>(half_tile(initial.z) & ~1) * 5.f + 5.f};
    return shovel_collision_distance(center, shovel_collision_point(query.point, memory)) <= 5.f;
}

} // namespace nx::acnh
