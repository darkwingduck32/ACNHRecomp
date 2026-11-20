#pragma once

#include "shovel_terrain.h"

namespace nx::acnh {

struct TraversalRectangleQuery {
    TerrainGrid grid;
    TerrainRect bounds;
    bool ground_only;
};

template<class Memory>
bool shovel_traversal_rectangle_has_height_break(TraversalRectangleQuery query, const Memory& memory) {
    constexpr float maximum_edge_delta = 1.25f;
    if (query.bounds.min.z > query.bounds.max.z) return false;
    const auto root = query.grid.address;
    const uint32_t width = memory.template read<uint32_t>(root + 0x38);
    const uint32_t depth = memory.template read<uint32_t>(root + 0x3c);
    const uint32_t ground_count = memory.template read<uint32_t>(root + 0x48);
    const uint64_t ground = memory.template read<uint64_t>(root + 0x50);
    const uint64_t overlay = query.ground_only ? 0 : memory.template read<uint64_t>(root + 0x60);
    const uint32_t overlay_count = overlay ? memory.template read<uint32_t>(root + 0x58) : 0;
    const auto load_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const auto slope_threshold = load_float(root + 0x30);
    const auto read_triangle = [&](int32_t x, int32_t z, unsigned corner) {
        const uint32_t index = width * uint32_t(z) + uint32_t(x);
        const int64_t offset = 0x34ll * std::bit_cast<int32_t>(index);
        const auto overlay_row = overlay + offset;
        const bool use_overlay = index < overlay_count
            && memory.template read<uint8_t>(overlay_row + 0x30 + corner) != 7;
        const uint64_t row = use_overlay ? overlay_row : index < ground_count ? ground + offset : 0;
        const uint64_t vertex = row + 12u * corner;
        return std::array{load_float(vertex), load_float(vertex + 4), load_float(vertex + 8)};
    };
    const auto height_range = [](std::array<float, 3> triangle) {
        const auto [a, b, c] = triangle;
        // Strict comparisons preserve the original conditional select order for unordered operands.
        const float low = a < b ? (a < c ? a : c) : (b < c ? b : c);
        const float high = a > b ? (a > c ? a : c) : (b > c ? b : c);
        return high - low;
    };
    constexpr std::array<HalfTile, 4> directions{{{0, -1}, {-1, 0}, {0, 1}, {1, 0}}};
    for (int64_t z = query.bounds.min.z; z <= query.bounds.max.z; ++z) {
        if (z < 0 || z >= depth) continue;
        for (int64_t x = query.bounds.min.x; x <= std::max(query.bounds.min.x, query.bounds.max.x); ++x) {
            if (x < 0 || x >= width) continue;
            std::array<std::array<float, 3>, 4> triangles;
            for (unsigned corner = 0; corner < triangles.size(); ++corner)
                triangles[corner] = read_triangle(int32_t(x), int32_t(z), corner);

            for (unsigned corner = 0; corner < triangles.size(); ++corner) {
                const auto [dx, dz] = directions[corner];
                const auto nx = x + dx, nz = z + dz;
                if (nx < query.bounds.min.x || nx > query.bounds.max.x || nz < query.bounds.min.z
                    || nz > query.bounds.max.z || nx < 0 || nx >= width || nz < 0 || nz >= depth)
                    continue;
                const auto adjacent = read_triangle(int32_t(nx), int32_t(nz), (corner + 2) % 4);
                const auto& current = triangles[corner];
                const float across_first = adjacent[2] - current[1];
                const float across_second = adjacent[1] - current[2];
                if (across_first > maximum_edge_delta || across_second > maximum_edge_delta) return true;
                if (height_range(current) < slope_threshold
                    && (across_first < -maximum_edge_delta || across_second < -maximum_edge_delta
                        || !(height_range(adjacent) < slope_threshold)))
                    return true;
            }
            for (unsigned corner = 0; corner < triangles.size(); ++corner) {
                const auto& current = triangles[corner];
                const auto& next = triangles[(corner + 1) % 4];
                const float inner = next[0] - current[0];
                const float outer = next[1] - current[2];
                if (inner > maximum_edge_delta || inner < -maximum_edge_delta
                    || outer > maximum_edge_delta || outer < -maximum_edge_delta)
                    return true;
            }
        }
    }
    return false;
}

} // namespace nx::acnh
