#pragma once

#include "shovel_target.h"
#include "shovel_terrain.h"

#include <optional>

namespace nx::acnh {

struct ShovelTraversalSweepQuery {
    uint64_t descriptor;
    uint64_t output;
    bool snap_to_edges;
};

struct ShovelSweepCast {
    ShovelTargetPoint point;
    std::optional<HalfTile> destination;
};

struct ShovelSweepResult {
    ShovelTargetPoint point;
    bool accepted;
    bool blocked;
};

template<class Memory, class Services>
bool shovel_traversal_sweep(ShovelTraversalSweepQuery query, Memory& memory, Services& services) {
    const auto read_point = [&](uint64_t address) {
        return ShovelTargetPoint{
            std::bit_cast<float>(memory.template read<uint32_t>(address)),
            std::bit_cast<float>(memory.template read<uint32_t>(address + 4)),
            std::bit_cast<float>(memory.template read<uint32_t>(address + 8)),
        };
    };
    const auto half_tile = [](float value) {
        return std::isnan(value) ? 0 : terrain_half_tile(value);
    };
    const auto boundary = [&](float value) {
        const auto tile = static_cast<float>(half_tile(value));
        const float edge = tile * 5.f;
        return value > std::fma(tile, 5.f, 2.5f) ? edge + 5.f : edge;
    };
    const auto snap = [&](ShovelTargetPoint point, unsigned index) {
        point.x = boundary(point.x);
        point.z = boundary(point.z);
        point.y = services.sample_height(point, index);
        return point;
    };

    auto start = read_point(query.descriptor);
    auto goal = read_point(query.descriptor + 12);
    if (query.snap_to_edges) {
        start = snap(start, 0);
        goal = snap(read_point(query.descriptor + 12), 1);
    }

    const auto x = start.x - goal.x;
    const auto z = start.z - goal.z;
    const float span = std::sqrt(std::fma(x, x, z * z));
    const uint32_t heading = services.heading(goal.x - start.x, goal.z - start.z);
    const auto destination = services.prepare_casts({half_tile(goal.x), half_tile(goal.z)});
    const auto pair = services.heading_samples(heading >> 24);
    const float fraction = static_cast<float>(heading & 0xffffffu) * 0x1p-24f;
    const float sin = std::fma(pair[1], fraction, pair[0]);
    const float cos = std::fma(pair[3], fraction, pair[2]);
    constexpr float tilt = 0.f;
    const float slope = sin * tilt;
    const float depth = cos * tilt;
    const float x_shift = std::fma(cos, tilt, slope * tilt);
    const float y_shift = std::fma(depth, tilt, std::fma(slope, tilt, 1.f) * tilt);
    const float z_shift = std::fma(-sin, tilt, depth * tilt);

    for (unsigned step = 1; step <= 19; ++step) {
        const float travel = (static_cast<float>(step) * 5.f) * 0.1f;
        const float distance = travel > span ? span : travel;
        const ShovelTargetPoint point{
            start.x + std::fma(sin + tilt, distance, x_shift),
            start.y + std::fma(slope, distance, y_shift),
            start.z + std::fma(cos, distance, z_shift),
        };
        const auto result = services.cast(ShovelSweepCast{point, destination});
        if (!result.accepted || result.blocked) continue;
        if (query.output) {
            const uint64_t xy = std::bit_cast<uint32_t>(result.point.x)
                | (uint64_t(std::bit_cast<uint32_t>(result.point.y)) << 32);
            memory.template write<uint32_t>(query.output + 8, std::bit_cast<uint32_t>(result.point.z));
            memory.template write<uint64_t>(query.output, xy);
        }
        return false;
    }
    return true;
}

} // namespace nx::acnh
