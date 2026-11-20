#include "../games/acnh/shovel_survey.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <vector>

struct GatherMemory : FixtureMemory {
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const auto bits = static_cast<uint64_t>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint8_t>(bits >> (8 * i));
        writes.emplace_back(address, sizeof(T), bits);
    }
};

struct GatherServices {
    uint32_t mode;
    std::vector<uint64_t> calls;

    uint32_t stage_key = 0;

    uint32_t stage_hash(uint32_t index) {
        calls.push_back(0x200000);
        calls.push_back(0x5bd550);
        return mode == 8 && index == 23 ? 0x758f7eaf : 0;
    }

    bool stage_rule(uint32_t key) {
        calls.push_back(0x16baf44);
        stage_key = key;
        return false;
    }

    void set_stage(uint32_t, bool) {}

    nx::acnh::SurveyCell read(nx::acnh::HalfTile point, bool) {
        calls.push_back(0x195033c);
        const uint64_t packed = uint32_t(point.x) | (uint64_t{uint32_t(point.z)} << 32);
        return {packed, {point.x + int(mode == 6), point.z}};
    }

    uint32_t item_id(uint64_t, nx::acnh::SurveyItemUse) {
        calls.push_back(0x267d6c4);
        return mode == 4 || mode == 5 ? 0xf619 : 0;
    }

    bool metadata_occupied(uint64_t, uint64_t) {
        calls.push_back(0x19ee3b4);
        return mode == 2;
    }

    bool single_tile_extent(uint64_t) {
        calls.push_back(0x253fe7c);
        return mode == 4;
    }
};

int main() {
    uint64_t base, scratch, address;
    uint32_t mode, count, value;
    while (std::cin >> base >> scratch >> mode >> count) {
        GatherMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        GatherServices services{mode, {}};
        nx::acnh::gather_shovel_neighborhood(base, {scratch}, memory, services);
        std::cout << services.calls.size();
        for (const auto entry : services.calls) std::cout << ' ' << entry;
        std::cout << ' ' << services.stage_key;
        for (const auto& [address, width, bits] : memory.writes)
            std::cout << ' ' << address << ' ' << width << ' ' << bits;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
