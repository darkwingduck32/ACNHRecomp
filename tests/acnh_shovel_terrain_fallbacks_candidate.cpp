#include "../games/acnh/shovel_terrain_helpers.h"
#include "fixture_memory.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

using namespace nx::acnh;

constexpr uint64_t base = 0x4000000000;
constexpr uint64_t point_address = 0x20000;
constexpr uint64_t stage_context = base + 0x59ed670;

struct Services {
    FixtureMemory& memory;
    unsigned mode;
    std::vector<uint64_t> calls;
    unsigned call_count = 0;
    bool stage_changed = false;
    bool point_changed = false;

    bool has_field_material(HalfTile point) {
        ++call_count;
        calls.push_back(0x87aae4);
        return terrain_has_field_material(base, point, memory);
    }

    uint32_t stage_hash(uint64_t object, uint32_t index, TerrainFallback fallback) {
        assert(index == 23);
        call_count += 2;
        if (fallback == TerrainFallback::primary)
            calls.insert(calls.end(), {0x87a9d4, object, 0x87a9e0, 0x2a000});
        else
            calls.insert(calls.end(), {0x87ab38, object, 0x87ab44, 0x2a000});
        if (mode == 9) {
            for (unsigned i = 0; i < 8; ++i)
                memory.bytes.at(stage_context + i) = 0;
            stage_changed = true;
        }
        return mode == 3 ? 0 : 0x758f7eaf;
    }

    uint32_t resolve_kind(TerrainOwner owner, TerrainWorldPoint& point, TerrainFallback fallback) {
        ++call_count;
        calls.insert(calls.end(), {fallback == TerrainFallback::primary ? 0x87aa64u : 0x87abc8u,
                                   owner.address + 0x28});
        point_changed = true;
        point.x = 0;
        return mode == 8 ? 7 : 9;
    }
};

} // namespace

int main() {
    unsigned entry, mode, count;
    while (std::cin >> entry >> mode >> count) {
        FixtureMemory memory;
        for (unsigned n = 0; n < count; ++n) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        if (entry == 2) {
            const HalfTile point{memory.read<int32_t>(point_address + 8), memory.read<int32_t>(point_address + 12)};
            std::cout << terrain_has_field_material(base, point, memory) << " 0\n";
            continue;
        }
        TerrainWorldPoint point{std::bit_cast<float>(memory.read<uint32_t>(point_address)),
                                std::bit_cast<float>(memory.read<uint32_t>(point_address + 8))};
        Services services{memory, mode};
        const auto fallback = entry == 0 ? TerrainFallback::primary : TerrainFallback::secondary;
        const auto value = terrain_fallback_kind(base, point, fallback, memory, services);
        std::cout << value << ' ' << services.call_count;
        for (const auto site : services.calls) std::cout << ' ' << site;
        if (services.stage_changed) std::cout << ' ' << stage_context << " 8 0";
        if (services.point_changed) std::cout << ' ' << point_address << " 4 " << std::bit_cast<uint32_t>(point.x);
        std::cout << '\n';
    }
}
