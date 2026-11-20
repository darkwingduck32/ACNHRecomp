#pragma once

#include "shovel_target.h"

namespace nx::acnh {

struct ShovelStepQuery { uint64_t input, output; };
struct ShovelStepMove { ShovelTargetPoint origin, end; uint64_t options; };

template<class Memory, class Services>
bool shovel_check_step(ShovelStepQuery query, Memory& memory, Services& services) {
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const ShovelTargetPoint origin{read_float(query.input), read_float(query.input + 4), read_float(query.input + 8)};
    const float dx = read_float(query.input + 0xc) - origin.x;
    const float dz = read_float(query.input + 0x14) - origin.z;
    const float length = std::sqrt(std::fma(dz, dz, std::fma(dx, dx, 0.f)));
    ShovelTargetPoint direction{dx, 0.f, dz};
    if (length > 0.f) {
        const float scale = 1.f / length;
        direction = {dx * scale, 0.f * scale, dz * scale};
    }
    float distance = 10.f;
    if (memory.template read<uint8_t>(query.input + 0x1a)) {
        const float span = std::sqrt(std::fma(dx, dx, dz * dz));
        distance = span > 10.f ? 10.f : span;
    } else if (memory.template read<uint8_t>(query.input + 0x19)) {
        const uint32_t heading = services.heading(dx, dz);
        const uint32_t snapped = (heading + 0x20000000u) & 0xc0000000u;
        constexpr auto degrees_per_unit = std::bit_cast<float>(0x33b40000u);
        const auto degrees = [](float value) {
            return value > 360.f ? std::fma(-std::trunc(value / 360.f), 360.f, value) : value;
        };
        const float difference = std::fabs(degrees(static_cast<float>(heading) * degrees_per_unit)
            - degrees(static_cast<float>(snapped) * degrees_per_unit));
        const float shortest = difference <= 180.f ? difference : 360.f - difference;
        distance = std::fma(shortest / 45.f, std::bit_cast<float>(0x40848c60u), 10.f);
    }
    const ShovelTargetPoint offset{direction.x * distance, direction.y * distance, direction.z * distance};
    const ShovelTargetPoint end{origin.x + offset.x, origin.y + offset.y, origin.z + offset.z};
    const auto options = memory.template read<uint64_t>(query.input + 0x18);
    services.prepare({origin, end, options});
    if (query.output) {
        memory.template write<uint32_t>(query.output, std::bit_cast<uint32_t>(end.x));
        memory.template write<uint32_t>(query.output + 4, std::bit_cast<uint32_t>(end.y));
        memory.template write<uint32_t>(query.output + 8, std::bit_cast<uint32_t>(end.z));
    }

    if (memory.template read<uint8_t>(query.input + 0x18)) {
        const float radius = services.collision_radius();
        const float endpoint_radius = radius * 0.6f;
        if (services.try_intermediate_probes(end, endpoint_radius)) {
            const float moved_x = end.x - origin.x;
            const float moved_z = end.z - origin.z;
            const float quarter_x = moved_x * 0.25f;
            const float quarter_z = moved_z * 0.25f;
            const float probe_y = origin.y + 0.f;
            const float probe_radius = radius * 0.9f;
            for (unsigned i = 1; i <= 3; ++i) {
                const float probe_x = quarter_x * static_cast<float>(i);
                const float probe_z = quarter_z * static_cast<float>(i);
                if (services.accept_intermediate({origin.x + probe_x, probe_y, origin.z + probe_z}, probe_radius, i - 1))
                    return true;
            }
        }
    }
    return services.fallback(query.output);
}

} // namespace nx::acnh
