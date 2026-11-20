#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace nx::acnh {

struct TerrainCullVolume { uint64_t address; };
struct TerrainCullPlane { uint64_t address; };
struct TerrainCullPoint { uint64_t address; };
struct TerrainCullVector { uint64_t address; };

struct TerrainCullResult {
    bool visible;
    float scalar;
};

namespace terrain_cull_detail {

struct Vec3 { float x, y, z; };

template<class Memory>
float read_scalar(const Memory& memory, uint64_t address) {
    return std::bit_cast<float>(memory.template read<uint32_t>(address));
}

template<class Memory>
float dot(const Memory& memory, uint64_t coefficients, Vec3 point) {
    const auto x = terrain_cull_detail::read_scalar(memory, coefficients);
    const auto y = terrain_cull_detail::read_scalar(memory, coefficients + 4);
    const auto z = terrain_cull_detail::read_scalar(memory, coefficients + 8);
    return std::fma(z, point.z, std::fma(x, point.x, y * point.y));
}

} // namespace terrain_cull_detail

template<class Memory>
bool intersects_terrain_cull_volume(TerrainCullVolume volume, TerrainCullPoint point, float radius,
                                    const Memory& memory) {
    const auto config = volume.address;
    const terrain_cull_detail::Vec3 position{terrain_cull_detail::read_scalar(memory, point.address),
                                            terrain_cull_detail::read_scalar(memory, point.address + 4),
                                            terrain_cull_detail::read_scalar(memory, point.address + 8)};
    const auto z = terrain_cull_detail::dot(memory, config + 0x20, position)
        + terrain_cull_detail::read_scalar(memory, config + 0x2c);
    const auto depth = -z;
    if (terrain_cull_detail::read_scalar(memory, config + 0xe0) - radius > depth
        || terrain_cull_detail::read_scalar(memory, config + 0xe4) < depth - radius) {
        return false;
    }
    const terrain_cull_detail::Vec3 transformed{
        terrain_cull_detail::dot(memory, config, position) + terrain_cull_detail::read_scalar(memory, config + 0xc),
        terrain_cull_detail::dot(memory, config + 0x10, position) + terrain_cull_detail::read_scalar(memory, config + 0x1c), z};
    for (unsigned plane = 0; plane < 4; ++plane) {
        const auto distance = terrain_cull_detail::dot(memory, config + 0x104 + 12 * plane, transformed)
            - terrain_cull_detail::read_scalar(memory, config + 0x134 + 4 * plane);
        // AArch64 FCMP followed by PL rejects unordered values as well.
        if (!(distance < radius)) return false;
    }
    return true;
}

template<class Memory>
TerrainCullResult passes_terrain_cull_plane(TerrainCullPlane plane, TerrainCullPoint point,
                                          TerrainCullVector vector, const Memory& memory) {
    const auto config = plane.address;
    const auto strength = terrain_cull_detail::read_scalar(memory, config + 0x3c);
    if (!(strength > 0.f)) return {true, strength};
    const auto gate = terrain_cull_detail::read_scalar(memory, vector.address + 4);
    if (!(gate >= 0.f)) return {true, gate};
    const auto z = terrain_cull_detail::read_scalar(memory, point.address + 8);
    if (!(z < terrain_cull_detail::read_scalar(memory, config + 0x58))) return {true, z};
    const terrain_cull_detail::Vec3 position{terrain_cull_detail::read_scalar(memory, point.address),
                                            terrain_cull_detail::read_scalar(memory, point.address + 4), z};
    const auto distance = terrain_cull_detail::dot(memory, config + 0x60, position)
        - terrain_cull_detail::read_scalar(memory, config + 0x6c);
    if (!(distance < 0.f)) return {true, distance};
    const auto depth = -distance;
    return {!(terrain_cull_detail::read_scalar(memory, point.address + 0xc) < depth), depth};
}

} // namespace nx::acnh
