#pragma once

#include "shovel_terrain_helpers.h"

namespace nx::acnh {

struct ShovelHeightQuery {
    uint64_t point;
    uint64_t material;
    uint32_t flags;
    float bias;
};

struct HeightSample {
    float height;
    uint32_t material;
};

enum class HeightPropertyUse { special_base, special_overlay, raised_base, strip_base, strip_overlay };

inline constexpr uint64_t shovel_height_column_cache = 0x4bfab80;

template<class Memory>
bool shovel_height_material_property(uint64_t base, uint32_t kind, Memory& memory) {
    const auto db = memory.template read<uint64_t>(base + 0x4bfab78) + 8;
    const auto row = field_record(db, kind, memory);
    if (!row) return false;
    const auto column = field_column(row, 13, 0x58da05ed, base + shovel_height_column_cache, memory);
    return column && memory.template read<uint8_t>(column);
}

template<class Memory>
bool shovel_height_scene_flag(uint64_t base, int16_t scene, unsigned bit, const Memory& memory) {
    if (scene == -1) return false;
    const auto table = memory.template read<uint64_t>(base + 0x4ac39f8);
    const auto index = static_cast<uint32_t>(scene);
    const auto entry = table + (index < 256 ? 8ull * index : 0);
    return (memory.template read<uint32_t>(entry) & (1u << bit)) != 0;
}

template<class Memory>
bool shovel_height_grid_flag(uint64_t owner, HalfTile point, const Memory& memory) {
    if (!owner || point.x < 0 || point.z < 0) return false;
    const auto grid = memory.template read<uint64_t>(owner + 0xa8);
    if (!grid) return false;
    const auto width = memory.template read<uint32_t>(grid + 0x38);
    if (uint32_t(point.x) >= width || uint32_t(point.z) >= memory.template read<uint32_t>(grid + 0x3c))
        return false;
    const uint32_t index = width * uint32_t(point.z) + uint32_t(point.x);
    const auto count = memory.template read<uint32_t>(grid + 0x68);
    const auto entries = memory.template read<uint64_t>(grid + 0x70);
    return index < count && entries &&
           (memory.template read<uint32_t>(entries + 4ll * std::bit_cast<int32_t>(index)) & 8u);
}

template<class Memory, class Services>
float shovel_height(uint64_t base, ShovelHeightQuery query, Memory& memory, Services& services) {
    const auto area_slot = memory.template read<uint64_t>(base + 0x4ab2200);
    const auto terrain_slot = memory.template read<uint64_t>(base + 0x4ab27c8);
    const auto ground = services.sample_base(memory.template read<uint64_t>(terrain_slot) + 0x28, query.point);
    const auto overlay = services.sample_overlay(memory.template read<uint64_t>(terrain_slot) + 0x28, query.point);
    const float limit = services.height_limit();
    const auto scene_slot = memory.template read<uint64_t>(base + 0x4ab6298);
    const auto scene_flag = [&](unsigned bit) {
        return shovel_height_scene_flag(base, memory.template read<int16_t>(scene_slot), bit, memory);
    };
    auto selected = ground;
    const auto consider_overlay = [&] {
        selected = overlay;
        if (selected.height > limit) selected.height = limit;
    };
    if (overlay.material == 8) {
        if (services.material_property(ground.material, HeightPropertyUse::special_base) &&
            !services.material_property(overlay.material, HeightPropertyUse::special_overlay))
            consider_overlay();
    } else {
        if ((query.flags & 1) && services.material_property(ground.material, HeightPropertyUse::raised_base) &&
            ground.material == overlay.material)
            return overlay.height + 12.f;

        const float x = std::bit_cast<float>(memory.template read<uint32_t>(query.point));
        const float z = std::bit_cast<float>(memory.template read<uint32_t>(query.point + 8));
        const auto coordinate = [](float value) {
            return std::isnan(value) ? 0 : terrain_half_tile(value);
        };
        selected = overlay;
        if (!shovel_height_grid_flag(memory.template read<uint64_t>(terrain_slot), {coordinate(x), coordinate(z)}, memory)
            && scene_flag(3) && z >= 300.f
            && services.material_property(ground.material, HeightPropertyUse::strip_base)
            && !services.material_property(overlay.material, HeightPropertyUse::strip_overlay))
            consider_overlay();
    }

    if (query.material) memory.template write<uint32_t>(query.material, selected.material);
    if (!scene_flag(1)) return selected.height;

    float floor = -20.f;
    const auto environment = memory.template read<uint64_t>(area_slot);
    const auto area = environment ? memory.template read<uint64_t>(environment + 0x50) : 0;
    const auto actor = area ? memory.template read<uint64_t>(area + 0x118) : 0;
    if (actor) {
        const auto link = memory.template read<uint64_t>(actor + 0x280);
        const auto holder = link ? memory.template read<uint64_t>(link + 8) : 0;
        const float offset = holder ? services.actor_height(holder + 0x488) : 0.f;
        floor = offset + services.world_height();
    }
    return (selected.height < floor ? floor : selected.height) + query.bias;
}

template<class Memory, class Services>
float shovel_ground_height(uint64_t base, uint64_t owner, uint64_t point, uint64_t material,
                           Memory& memory, Services& services) {
    const auto grid = memory.template read<uint64_t>(owner + 0x80);
    if (!grid) {
        if (material) {
            const auto scene = memory.template read<int16_t>(base + 0x4b487d4);
            memory.template write<uint32_t>(material, shovel_height_scene_flag(base, scene, 1, memory) ? 5 : 7);
        }
        return -500.f;
    }
    const auto scene_slot = memory.template read<uint64_t>(base + 0x4ab6298);
    const auto scene = memory.template read<int16_t>(scene_slot);
    const auto sample = services.ground_grid(grid, point, shovel_height_scene_flag(base, scene, 1, memory) ? 5 : 7);
    if (material) {
        memory.template write<uint32_t>(material, sample.material);
        const auto override = services.adjustment(point, TerrainFallback::primary);
        if (override != 7) memory.template write<uint32_t>(material, override);
    }
    return sample.height;
}

struct OverlayHeightSample {
    float height;
    bool material_final;
};

template<class Memory, class Services>
float shovel_overlay_height(uint64_t base, uint64_t owner, uint64_t point, uint64_t material,
                            Memory& memory, Services& services) {
    const auto scene_slot = memory.template read<uint64_t>(base + 0x4ab6298);
    const auto scene = memory.template read<int16_t>(scene_slot);
    uint32_t kind = shovel_height_scene_flag(base, scene, 1, memory) ? 5 : 7;
    if (material) memory.template write<uint32_t>(material, kind);

    float height = -500.f;
    bool material_final = false;
    const auto lock = owner + 0x28;
    services.lock_overlay(lock);
    auto link = memory.template read<uint64_t>(owner + 0x50);
    const auto sentinel = owner + 0x48;
    if (link != sentinel) {
        const auto offset = memory.template read<int32_t>(owner + 0x5c);
        while (link != sentinel) {
            const auto sample = services.overlay_grid(link - offset, point, kind, material != 0);
            if (height < sample.height) {
                height = sample.height;
                if (material) memory.template write<uint32_t>(material, kind);
                material_final = sample.material_final;
            }
            link = memory.template read<uint64_t>(link + 8);
        }
    }
    services.unlock_overlay(lock);
    if (material && !material_final) {
        for (auto route : {TerrainFallback::primary, TerrainFallback::secondary}) {
            const auto override = services.adjustment(point, route);
            if (override != 7) memory.template write<uint32_t>(material, override);
        }
    }
    return height;
}

enum class HeightParameterGuard { type, fallback };

template<class Memory, class Services>
uint64_t height_parameters(uint64_t base, uint32_t index, Memory& memory, Services& services) {
    if (index <= 0xeb) {
        const auto slot = memory.template read<uint64_t>(base + 0x4ab2f58);
        const auto table = memory.template read<uint64_t>(slot);
        if (table) {
            const auto object = memory.template read<uint64_t>(table + 16ull * index);
            if (object) {
                const auto guard = memory.template read<uint64_t>(base + 0x4ac3110);
                if (!(services.guard_byte(guard) & 1) && services.acquire_guard(guard, HeightParameterGuard::type)) {
                    const auto type_info = memory.template read<uint64_t>(base + 0x4ac3108);
                    memory.template write<uint64_t>(type_info, memory.template read<uint64_t>(base + 0x4ab3608) + 0x10);
                    services.release_guard(memory.template read<uint64_t>(base + 0x4ac3110), HeightParameterGuard::type);
                }
                if (services.parameter_type_matches(object, memory.template read<uint64_t>(base + 0x4ac3108)))
                    return object;
            }
        }
    }
    const uint64_t guard = base + 0x53f4258;
    const uint64_t fallback = base + 0x53f1368;
    if (!(services.guard_byte(guard) & 1) && services.acquire_guard(guard, HeightParameterGuard::fallback)) {
        services.construct_parameters(fallback);
        services.register_parameter_destructor(fallback);
        services.release_guard(guard, HeightParameterGuard::fallback);
    }
    return fallback;
}

enum class HeightParameterType { primary, secondary };

template<class Memory, class Services>
uint32_t height_parameter_type_matches(uint64_t base, uint64_t requested, Memory& memory, Services& services) {
    struct TypeSlot {
        HeightParameterType kind;
        uint64_t descriptor_slot;
        uint64_t guard_slot;
        uint64_t initializer_slot;
    };
    constexpr std::array types{
        TypeSlot{HeightParameterType::primary, 0x4ac3108, 0x4ac3110, 0x4ab3608},
        TypeSlot{HeightParameterType::secondary, 0x4ab35f0, 0x4ab35f8, 0x4ab3600},
    };
    for (const auto& type : types) {
        const auto guard = memory.template read<uint64_t>(base + type.guard_slot);
        if (!(services.guard_byte(guard) & 1) && services.acquire_guard(guard, type.kind)) {
            const auto descriptor = memory.template read<uint64_t>(base + type.descriptor_slot);
            const auto initializer = memory.template read<uint64_t>(base + type.initializer_slot);
            memory.template write<uint64_t>(descriptor, initializer + 0x10);
            services.release_guard(memory.template read<uint64_t>(base + type.guard_slot), type.kind);
        }
        if (requested == memory.template read<uint64_t>(base + type.descriptor_slot)) return 1;
    }
    return services.matches_base_type(requested);
}

constexpr uint64_t height_float_address(uint64_t reference) {
    return reference + 0x24;
}

} // namespace nx::acnh
