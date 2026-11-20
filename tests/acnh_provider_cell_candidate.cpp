#include "../games/acnh/field_provider.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <vector>

struct ProviderMemory : FixtureMemory {
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = static_cast<uint64_t>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint8_t>(bits >> (8 * i));
        writes.emplace_back(address, sizeof(T), bits);
    }
};

struct ProviderServices {
    uint32_t mode;
    std::vector<uint64_t> calls;

    nx::acnh::ProviderTable table(nx::acnh::ProviderReader, nx::acnh::ProviderTableUse) {
        calls.push_back(0x120000);
        return {0x23000};
    }
    uint32_t width(nx::acnh::ProviderTable, nx::acnh::ProviderDimensionUse) {
        calls.push_back(0x120020);
        return 4;
    }
    uint32_t height(nx::acnh::ProviderTable, nx::acnh::ProviderDimensionUse) {
        calls.push_back(0x120040);
        return 4;
    }
    bool ready(nx::acnh::ProviderReader) {
        calls.push_back(0x120060);
        return mode != 2;
    }
    bool release_required(nx::acnh::ProviderContext) {
        calls.push_back(0x1200a0);
        return mode != 9;
    }
    void release(nx::acnh::ProviderContext) { calls.push_back(0x1200c0); }
    uint64_t offset(nx::acnh::ProviderTable, nx::acnh::ProviderCell pos, bool) {
        calls.push_back(0x120080);
        return 8ull * (pos.x + 4 * pos.z);
    }
    nx::acnh::ProviderAnchor resolve_anchor(nx::acnh::FieldCellProvider, nx::acnh::ProviderCell pos) {
        calls.push_back(0x11e4578);
        return {{0x23000}, pos};
    }
    void cleanup_anchor() { calls.push_back(0x2540184); }
};

int main() {
    uint64_t base, provider, point, root, out, address;
    uint32_t mode, count, value;
    while (std::cin >> base >> provider >> point >> root >> out >> mode >> count) {
        ProviderMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        ProviderServices services{mode, {}};
        nx::acnh::read_provider_cell(base, {{provider}, {point}, {root}, {out}}, memory, services);
        std::cout << services.calls.size();
        for (uint64_t entry : services.calls) std::cout << ' ' << entry;
        for (const auto& [place, width, bits] : memory.writes)
            std::cout << ' ' << place << ' ' << width << ' ' << bits;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
