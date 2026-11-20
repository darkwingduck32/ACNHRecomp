#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace nx::acnh {

struct ShovelTargetRequest { uint64_t address; };
struct ShovelTargetPoint { float x, y, z; };

enum class ShovelTargetHeight { origin, desired, origin_check };
enum class ShovelTargetProbe { approach, radius, nearby };

template<class Memory, class Services>
uint32_t select_shovel_target(uint64_t base, ShovelTargetRequest request, Memory& memory, Services& services) {
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const auto read_point = [&](uint64_t address) {
        return ShovelTargetPoint{read_float(address), read_float(address + 4), read_float(address + 8)};
    };
    const auto write_point = [&](uint64_t address, ShovelTargetPoint point) {
        memory.template write<uint32_t>(address, std::bit_cast<uint32_t>(point.x));
        memory.template write<uint32_t>(address + 4, std::bit_cast<uint32_t>(point.y));
        memory.template write<uint32_t>(address + 8, std::bit_cast<uint32_t>(point.z));
    };
    const uint64_t origin_address = request.address;
    const uint64_t desired_address = origin_address + 0xc;
    auto origin = read_point(origin_address);
    auto desired = read_point(desired_address);
    const float dx = origin.x - desired.x;
    const float dz = origin.z - desired.z;
    if (std::sqrt(std::fma(dx, dx, dz * dz)) > 10.f) {
        const bool fixed = memory.template read<uint8_t>(origin_address + 0x28) != 0;
        const uint32_t heading = services.heading(desired.x - origin.x, desired.z - origin.z);
        constexpr auto degrees_per_unit = std::bit_cast<float>(0x33b40000u);
        const float degrees = static_cast<float>(heading) * degrees_per_unit;
        const float axis = static_cast<float>((heading + 0x20000000u) & 0xc0000000u) * degrees_per_unit;
        const float difference = std::fabs(degrees - axis);
        const float deviation = difference <= 180.f ? difference : 360.f - difference;
        const float extra = fixed ? 0.f : std::bit_cast<float>(0x40848c60u);
        const float distance = std::fma(extra, deviation / 45.f, 10.f);
        const auto trig = memory.template read<uint64_t>(base + 0x4ab2d00) + 16 * (heading >> 24);
        const float fraction = static_cast<float>(heading & 0xffffffu) * 0x1p-24f;
        const float sine = std::fma(read_float(trig + 4), fraction, read_float(trig));
        const float cosine = std::fma(read_float(trig + 12), fraction, read_float(trig + 8));
        // The heading projects the horizontal movement onto a zero-tilt vector.
        const float tilt = 0.f;
        const float projected = sine * tilt;
        const ShovelTargetPoint offset{
            std::fma(sine + tilt, distance, std::fma(cosine, tilt, projected * tilt)),
            std::fma(projected, distance, std::fma(cosine * tilt, tilt, std::fma(projected, tilt, 1.f) * tilt)),
            std::fma(cosine, distance, std::fma(-sine, tilt, (cosine * tilt) * tilt)),
        };
        origin = read_point(origin_address);
        desired = {origin.x + offset.x, origin.y + offset.y, origin.z + offset.z};
        write_point(desired_address, desired);
    }
    write_point(origin_address + 0x2c, desired);

    const float height = services.height(request, ShovelTargetHeight::origin);
    const float radius = std::fabs(read_float(origin_address + 0x3c));
    memory.template write<uint32_t>(origin_address + 4, std::bit_cast<uint32_t>(height));
    origin = read_point(origin_address);
    desired = read_point(desired_address);
    auto step = ShovelTargetPoint{desired.x - origin.x, 0.f, desired.z - origin.z};
    const auto normalize = [](ShovelTargetPoint point) {
        const float length = std::sqrt(std::fma(point.z, point.z, std::fma(point.x, point.x, 0.f)));
        if (length > 0.f) {
            const float scale = 1.f / length;
            point = {point.x * scale, point.y * scale, point.z * scale};
        }
        return point;
    };
    step = normalize(step);
    step = {step.x * 5.f, step.y * 5.f, step.z * 5.f};
    constexpr std::array approach_scales{1.1f, 1.15f, 1.25f};
    for (size_t attempt = 0; attempt < approach_scales.size(); ++attempt) {
        const auto current = read_point(origin_address);
        const float scale = approach_scales[attempt];
        const auto result = services.probe(
            {current.x + step.x * scale, current.y + step.y * scale, current.z + step.z * scale},
            request, ShovelTargetProbe::approach, attempt);
        if (result == 1 || result == 5) return result;
    }

    constexpr float small = std::bit_cast<float>(0x3e10cb2au);
    constexpr std::array<ShovelTargetPoint, 9> nearby{{
        {0, 0, 0}, {0.2f, 0, 0}, {0, 0, 0.2f}, {-0.2f, 0, 0}, {0, 0, -0.2f},
        {small, 0, small}, {-small, 0, small}, {-small, 0, -small}, {small, 0, -small},
    }};
    unsigned rejected = 0;
    for (size_t attempt = 0; attempt < nearby.size(); ++attempt) {
        const auto current = read_point(desired_address);
        auto offset = nearby[attempt];
        auto method = ShovelTargetProbe::nearby;
        if (!(radius <= 0x1p-23f)) {
            const auto unit = base + 0x4076bac + 12 * attempt;
            offset = normalize({read_float(unit), 0.f, read_float(unit + 8)});
            const float current_radius = read_float(origin_address + 0x3c);
            offset = {offset.x * current_radius, offset.y * current_radius, offset.z * current_radius};
            method = ShovelTargetProbe::radius;
        }
        const uint32_t result = services.probe({current.x + offset.x, current.y + offset.y, current.z + offset.z},
                                              request, method, attempt);
        if (result == 14) ++rejected;
        else if (result) return result;
    }

    uint32_t result = 14;
    if (rejected != nearby.size()) {
        const float desired_height = services.height(request, ShovelTargetHeight::desired);
        const float origin_height = services.height(request, ShovelTargetHeight::origin_check);
        if (!(desired_height - origin_height < -10.f)) result = 0;
    }
    memory.template write<uint32_t>(origin_address + 0x38, result);
    return result;
}

} // namespace nx::acnh
