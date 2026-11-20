#include "../games/acnh/field_queries.h"
#include "fixture_memory.h"

#include <iostream>
#include <tuple>
#include <vector>

struct CellMemory : FixtureMemory {
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;
    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = static_cast<uint8_t>(static_cast<uint64_t>(value) >> (8 * i));
        writes.emplace_back(address, sizeof(T), value);
    }
};

struct CellServices {
    CellMemory& memory;
    const nx::acnh::FieldQueries<CellMemory>& fields;
    std::vector<std::pair<uint32_t, uint64_t>> calls;

    bool contains(nx::acnh::FieldCellQuery query) {
        calls.emplace_back(0x194f798, query.layer.address);
        const nx::acnh::FieldLayer layer{memory.read<uint32_t>(query.layer.address + 8)};
        const auto bounds = fields.bounds_for(layer, query.channel);
        return bounds && fields.contains(bounds, query.position);
    }

    void initialize(nx::acnh::FieldItem item) {
        calls.emplace_back(0x267d4ac, item.address);
        memory.write<uint32_t>(item.address + 4, 0x5678);
    }

    uint64_t read(nx::acnh::FieldCellProvider provider, nx::acnh::HalfTileRef point,
                  nx::acnh::HalfTileRef root) {
        calls.emplace_back(0x11e4f58, provider.address);
        const uint64_t value = memory.read<uint64_t>(provider.address);
        if (root.address) {
            memory.write<uint32_t>(root.address + 8, memory.read<uint32_t>(point.address + 8));
            memory.write<uint32_t>(root.address + 12, memory.read<uint32_t>(point.address + 12));
        }
        return value;
    }
};

int main() {
    uint64_t base, layer, position, root, output, address;
    uint32_t operation, channel, count, value;
    while (std::cin >> operation >> base >> layer >> channel >> position >> root >> output >> count) {
        CellMemory memory;
        for (uint32_t i = 0; i < count; ++i) {
            if (!(std::cin >> address >> value)) return 1;
            memory.bytes.emplace(address, value);
        }
        const nx::acnh::FieldQueries fields{base, memory};
        CellServices services{memory, fields, {}};
        if (operation == 0)
            fields.read_cell({{layer}, {channel}, {position}}, {output}, {root}, services);
        else
            fields.initialize_survey_items({output}, services);
        std::cout << services.calls.size();
        for (const auto& [entry, input] : services.calls) std::cout << ' ' << entry << ' ' << input;
        for (const auto& [place, size, bits] : memory.writes) std::cout << ' ' << place << ' ' << size << ' ' << bits;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
