#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace nx::acnh {

inline int32_t truncate_coordinate(float value) {
    if (std::isnan(value)) return 0;
    if (value >= 0x1p31f) return std::numeric_limits<int32_t>::max();
    if (value <= -0x1p31f) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(value);
}

inline int32_t field_cell(float coordinate) {
    const float whole = static_cast<float>(truncate_coordinate(coordinate));
    return static_cast<int32_t>(whole / 5.f) - static_cast<int32_t>(coordinate < 0.f);
}

// Check the central cell, then sample the circumference when a radius is present.
template<class Memory>
bool field_flag(uint64_t base, uint64_t point, float radius, uint32_t flags, const Memory& memory) {
    const auto read_float = [&](uint64_t address) {
        return std::bit_cast<float>(memory.template read<uint32_t>(address));
    };
    const float x = read_float(point);
    const float z = read_float(point + 8);
    const uint64_t singleton = memory.template read<uint64_t>(base + 0x4ab27c8);
    const uint64_t manager = memory.template read<uint64_t>(singleton);
    const auto blocked = [&](float sample_x, float sample_z) {
        if (!manager) return false;
        const uint64_t grid = memory.template read<uint64_t>(manager + 0xa8);
        if (!grid) return false;
        const int32_t column = field_cell(sample_x);
        if (column < 0) return false;
        const int32_t row = field_cell(sample_z);
        if (row < 0) return false;
        const uint32_t width = memory.template read<uint32_t>(grid + 0x38);
        const uint32_t height = memory.template read<uint32_t>(grid + 0x3c);
        if (static_cast<uint32_t>(column) >= width || static_cast<uint32_t>(row) >= height)
            return false;
        const uint32_t index = width * static_cast<uint32_t>(row) + static_cast<uint32_t>(column);
        const uint32_t count = memory.template read<uint32_t>(grid + 0x68);
        const uint64_t cells = memory.template read<uint64_t>(grid + 0x70);
        if (!cells || index >= count) return false;
        const int64_t offset = 4ll * static_cast<int32_t>(index);
        return (memory.template read<uint32_t>(cells + offset) & 8u) != 0;
    };
    if (blocked(x, z)) return true;
    if (std::fabs(radius) <= 0x1p-23f) return false;

    const bool dense = (flags & 1u) != 0;
    const uint32_t step = memory.template read<uint32_t>(base + 0x5140230 + 4 * uint32_t{dense});
    uint32_t angle = 0;
    for (int i = 0; i < (dense ? 16 : 8); ++i, angle += step) {
        const uint64_t sample = base + 0x4099798 + 16ull * (angle >> 24);
        const float fraction = static_cast<float>(angle & 0x00ffffff) * 0x1p-24f;
        const float dx = std::fma(read_float(sample + 4), fraction, read_float(sample));
        const float dz = std::fma(read_float(sample + 12), fraction, read_float(sample + 8));
        if (blocked(x + dx * radius, z + dz * radius)) return true;
    }
    return false;
}

}  // namespace nx::acnh
