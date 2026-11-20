#pragma once

#include "field_queries.h"

#include <bit>
#include <cstdint>

namespace nx::acnh {

struct ProviderReader { uint64_t address; };
struct ProviderTable { uint64_t address; };
struct ProviderCell { uint32_t x, z; };

struct ProviderContext { uint64_t address; };
enum class ProviderTableUse { x_bound, z_bound, available, result, anchor_x_bound, anchor_z_bound };
enum class ProviderDimensionUse { initial, result, anchor };

struct ProviderQuery {
    FieldCellProvider provider;
    HalfTileRef point;
    HalfTileRef root;
    FieldItem output;
};

struct ProviderAnchor {
    ProviderTable table;
    ProviderCell position;
};

template<class Memory, class Services>
void read_provider_cell(uint64_t base, ProviderQuery query, Memory& memory, Services& services) {
    const ProviderReader reader{query.provider.address + 0x28};
    const auto write_empty = [&] {
        const uint64_t empty = base + 0x5486008;
        const auto lower = memory.template read<uint32_t>(empty);
        const auto upper = memory.template read<uint32_t>(empty + 4);
        memory.template write<uint32_t>(query.output.address, lower);
        memory.template write<uint32_t>(query.output.address + 4, upper);
    };
    const auto point = [&] {
        return ProviderCell{memory.template read<uint32_t>(query.point.address + 8),
                            memory.template read<uint32_t>(query.point.address + 12)};
    };
    const auto relative_to_provider = [&](ProviderCell position) {
        return ProviderCell{position.x - memory.template read<uint32_t>(query.provider.address + 0x10),
                            position.z - memory.template read<uint32_t>(query.provider.address + 0x14)};
    };
    const auto outside = [&](ProviderCell cell, bool anchor) {
        const auto x_table = services.table(reader, anchor ? ProviderTableUse::anchor_x_bound : ProviderTableUse::x_bound);
        const auto use = anchor ? ProviderDimensionUse::anchor : ProviderDimensionUse::initial;
        if (cell.x >= services.width(x_table, use)) return true;
        const auto z_table = services.table(reader, anchor ? ProviderTableUse::anchor_z_bound : ProviderTableUse::z_bound);
        return cell.z >= services.height(z_table, use);
    };
    const auto masked = [&](ProviderCell cell) {
        const uint64_t provider = query.provider.address;
        const uint64_t mask = memory.template read<uint64_t>(provider + 0x1b8);
        if (!mask) return false;
        const uint32_t width = memory.template read<uint32_t>(provider + 0x1c8);
        if (cell.x >= width || cell.z >= memory.template read<uint32_t>(provider + 0x1cc)) return false;
        const uint32_t index = width * cell.z + cell.x;
        const uint32_t counter = memory.template read<uint32_t>(provider + 0x1c4);
        memory.template write<uint32_t>(provider + 0x1c4, counter + 1);
        uint32_t word = 0;
        const int32_t signed_index = std::bit_cast<int32_t>(index);
        if (signed_index >= 0 && signed_index < memory.template read<int32_t>(provider + 0x1c0))
            word = memory.template read<uint32_t>(mask + 4ull * (index >> 5));
        memory.template write<uint32_t>(provider + 0x1c4, counter);
        return ((word >> (index & 31)) & 1u) != 0;
    };

    auto cell = relative_to_provider(point());
    if (outside(cell, false) || !services.ready(reader) ||
        !services.table(reader, ProviderTableUse::available).address || masked(cell)) {
        write_empty();
        return;
    }

    const auto table = services.table(reader, ProviderTableUse::result);
    const auto width = services.width(table, ProviderDimensionUse::result);
    const auto height = services.height(table, ProviderDimensionUse::result);
    if (cell.x >= width) cell.x = 0;
    if (cell.z >= height) cell.z = 0;
    const uint64_t context = memory.template read<uint64_t>(table.address + 0x28);
    const uint64_t read_context = context ? memory.template read<uint64_t>(context) : 0;
    const bool release = read_context && services.release_required({read_context});

    const auto offset = services.offset(table, cell, false);
    const uint64_t record = memory.template read<uint64_t>(table.address + 8) + offset;
    const uint16_t id = memory.template read<uint16_t>(record);
    const uint64_t output = query.output.address;
    if (id == 0xfffd) {
        const uint32_t displacement = memory.template read<uint32_t>(record + 4);
        auto absolute = point();
        absolute.x -= (displacement >> 16) & 0xff;
        absolute.z -= displacement >> 24;
        const auto relative = relative_to_provider(absolute);
        if (outside(relative, true)) {
            write_empty();
        } else {
            const auto anchor = services.resolve_anchor(query.provider, relative);
            const auto anchor_offset = services.offset(anchor.table, anchor.position, true);
            const uint64_t item = memory.template read<uint64_t>(anchor.table.address + 8) + anchor_offset;
            if (query.root.address) {
                memory.template write<uint32_t>(query.root.address + 8, absolute.x);
                memory.template write<uint32_t>(query.root.address + 12, absolute.z);
            }
            const auto anchor_id = memory.template read<uint16_t>(item);
            const auto flags = memory.template read<uint16_t>(item + 2);
            memory.template write<uint16_t>(output + 2, flags);
            memory.template write<uint32_t>(output + 4, memory.template read<uint32_t>(item + 4));
            memory.template write<uint16_t>(output, anchor_id);
            services.cleanup_anchor();
        }
    } else {
        auto selected_id = id;
        if (query.root.address) {
            memory.template write<uint32_t>(query.root.address + 8,
                memory.template read<uint32_t>(query.point.address + 8));
            memory.template write<uint32_t>(query.root.address + 12,
                memory.template read<uint32_t>(query.point.address + 12));
            selected_id = memory.template read<uint16_t>(record);
        }
        memory.template write<uint16_t>(output, selected_id);
        const auto flags = memory.template read<uint16_t>(record + 2);
        const auto data = memory.template read<uint32_t>(record + 4);
        memory.template write<uint16_t>(output + 2, flags);
        memory.template write<uint32_t>(output + 4, data);
    }

    if (release) {
        const uint64_t end_context = memory.template read<uint64_t>(table.address + 0x28);
        const uint64_t release_context = end_context ? memory.template read<uint64_t>(end_context + 8) : 0;
        if (release_context) services.release({release_context});
    }
}

template<class Memory, class Resolve>
ProviderTable provider_table(ProviderReader reader, const Memory& memory, Resolve resolve) {
    const uint64_t target = memory.template read<uint64_t>(reader.address + 8);
    return target ? resolve(target) : ProviderTable{};
}

constexpr bool provider_ready() { return true; }
constexpr uint32_t provider_width() { return 0x120; }
constexpr uint32_t provider_height() { return 0xc0; }

template<class Memory>
uint64_t provider_cell_offset(ProviderTable table, ProviderCell cell, const Memory& memory) {
    const uint64_t first = memory.template read<uint64_t>(table.address + 0x10);
    const int32_t index = std::bit_cast<int32_t>(cell.x * provider_height() + cell.z);
    return first + 8ll * index;
}

}  // namespace nx::acnh
