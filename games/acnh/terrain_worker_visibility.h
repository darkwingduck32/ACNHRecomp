#pragma once

#include "terrain_cull_predicates.h"

namespace nx::acnh {

struct TerrainWorkerPlaneSpan { uint64_t address; uint32_t count; };
struct TerrainWorkerPlaneResult { bool passes; float scalar; uint32_t tested; };

// The first row has already been evaluated by the caller. The span contains at most 24 rows.
template<class Memory>
TerrainWorkerPlaneResult terrain_worker_remaining_planes(TerrainWorkerPlaneSpan planes,
        terrain_cull_detail::Vec3 point, float margin, float first_distance, const Memory& memory) {
    if (margin < first_distance) return {false, first_distance, 0};
    float distance = first_distance;
    for (uint32_t index = 1; index < planes.count; ++index) {
        const uint64_t row = planes.address + 16ull * index;
        distance = terrain_cull_detail::dot(memory, row, point)
            - terrain_cull_detail::read_scalar(memory, row + 12);
        if (margin < distance) return {false, distance, index};
    }
    return {true, distance, planes.count - 1};
}

} // namespace nx::acnh
