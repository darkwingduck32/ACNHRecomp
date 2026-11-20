#pragma once

#include "shovel_simple_cast.h"
#include "field_queries.h"

namespace nx::acnh {

struct ShovelSweepCollisionQuery {
    ShovelTargetPoint center;
    float radius;
    uint64_t contact_position;
    uint64_t block_flag;
};

struct ShovelSweepCollisionRoots {
    HalfTile contact;
    HalfTile reference;
};

inline bool same_collision_root(HalfTile left, HalfTile right) {
    return left.x == right.x && left.z == right.z;
}

template<class Memory, class Services>
uint32_t shovel_sweep_collision(uint64_t base, ShovelSweepCollisionQuery query, Memory& memory, Services& services) {
    auto center = query.center;
    uint32_t hit_mask = 0;
    HalfTile previous_root{};
    memory.template write<uint8_t>(query.block_flag, 1);

    const auto test_contact = [&](ShovelTargetPoint& point, bool at_center) {
        if (!services.height_probe(point, at_center, at_center ? query.radius : point.z))
            return;
        if (!hit_mask) {
            hit_mask = static_cast<uint32_t>(ShovelStepCastMask::height);
            memory.template write<uint32_t>(query.contact_position + 8, std::bit_cast<uint32_t>(point.z));
            memory.template write<uint32_t>(query.contact_position, std::bit_cast<uint32_t>(point.x));
            memory.template write<uint32_t>(query.contact_position + 4, std::bit_cast<uint32_t>(point.y));
        }
        const auto roots = services.contact_roots(point, at_center);
        if (at_center) {
            if ((roots.reference.x || roots.reference.z) && (roots.contact.x || roots.contact.z))
                memory.template write<uint8_t>(query.block_flag, 0);
        } else if (same_collision_root(previous_root, roots.reference)) {
            previous_root = roots.contact;
        } else if (!same_collision_root(roots.contact, previous_root)) {
            memory.template write<uint8_t>(query.block_flag, 0);
        }
    };

    test_contact(center, true);
    if (std::fabs(query.radius) < 0x1p-23f) return 0;

    const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00);
    const auto step = memory.template read<uint32_t>(base + 0x5140234);
    for (unsigned sector = 0; sector < 16; ++sector) {
        const auto offset = shovel_cast_rim_offset(trig, step * sector, query.radius, memory);
        ShovelTargetPoint point{offset.x + center.x, offset.y + center.y, offset.z + center.z};
        test_contact(point, false);
    }
    return hit_mask;
}

} // namespace nx::acnh
