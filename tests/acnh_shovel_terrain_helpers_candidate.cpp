#include "../games/acnh/shovel_terrain_helpers.h"

#include <iostream>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint64_t scratch = 0x29000;
constexpr uint64_t tap = 0x2a000;
constexpr uint64_t base = 0x4000000000;
constexpr uint64_t grid = 0x20000;
constexpr uint64_t output = 0x28000;

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;
    std::vector<std::tuple<uint64_t, unsigned, uint64_t>> writes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t result = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            result |= uint64_t(bytes.at(address + i)) << (8 * i);
        if constexpr (std::is_same_v<T, float>) return std::bit_cast<float>(uint32_t(result));
        else return static_cast<T>(result);
    }

    template<class T>
    void write(uint64_t address, T value) {
        if (address < scratch || address >= scratch + 0x34)
            writes.emplace_back(address, sizeof(T), uint64_t(value));
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
    }
};

struct Services {
    Memory& memory;
    unsigned mode;
    std::vector<uint64_t> calls;

    void sample(nx::acnh::TerrainGrid grid, nx::acnh::HalfTile tile) {
        calls.push_back(0x862530);
        nx::acnh::sample_terrain(grid, tile, {scratch}, memory);
    }

    float sample_height(nx::acnh::TerrainGrid grid, nx::acnh::HalfTile tile) {
        sample(grid, tile);
        return memory.read<float>(scratch);
    }

    std::array<uint8_t, 4> sample_kinds(nx::acnh::TerrainGrid grid, nx::acnh::HalfTile tile) {
        sample(grid, tile);
        return {memory.read<uint8_t>(scratch + 0x30), memory.read<uint8_t>(scratch + 0x31),
                memory.read<uint8_t>(scratch + 0x32), memory.read<uint8_t>(scratch + 0x33)};
    }

    uint32_t primary_kind(nx::acnh::TerrainWorldPoint point, size_t) {
        calls.push_back(0x87a980);
        memory.write(tap, std::bit_cast<uint32_t>(point.x));
        memory.write(tap + 4, std::bit_cast<uint32_t>(point.z));
        return mode & 1 ? 7 : 8;
    }

    uint32_t secondary_kind(nx::acnh::TerrainWorldPoint point, size_t) {
        calls.push_back(0x87aa70);
        memory.write(tap, std::bit_cast<uint32_t>(point.x));
        memory.write(tap + 4, std::bit_cast<uint32_t>(point.z));
        return mode & 2 ? 7 : 9;
    }
};

}

int main() {
    unsigned entry, mode;
    nx::acnh::HalfTile minimum{}, maximum{};
    size_t byte_count;
    while (std::cin >> entry >> mode >> minimum.x >> minimum.z >> maximum.x >> maximum.z >> byte_count) {
        Memory memory;
        for (size_t i = 0; i < byte_count; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        Services services{memory, mode, {}};
        const bool result = entry == 0 ? nx::acnh::terrain_is_level({grid}, {minimum, maximum},
            mode & 1 ? nx::acnh::TerrainHeightMode::base_only : nx::acnh::TerrainHeightMode::composed,
            memory, services) : nx::acnh::terrain_quadrant_materials(base, {grid}, minimum,
            {{output, output + 4, output + 8, output + 12}}, memory, services);
        std::cout << result << ' ' << services.calls.size();
        for (const auto call : services.calls) std::cout << ' ' << call;
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
}
