#include "../games/acnh/shovel_stage_context.h"
#include "fixture_memory.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;
    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = value;
        writes.push_back({address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i) bytes.at(address + i) = (bits >> (8 * i)) & 0xff;
    }
};

struct Call {
    uint64_t site, outcome, target_buffer;
    uint32_t height;
    std::array<uint64_t, 5> args;
    std::vector<uint32_t> writes;
};

struct Services {
    Memory& memory;
    const std::vector<Call>& calls;
    uint64_t stack;
    size_t next = 0;

    const Call& invoke(uint64_t site, std::initializer_list<uint64_t> args = {}) {
        if (next >= calls.size() || calls[next].site != site
            || !std::equal(args.begin(), args.end(), calls[next].args.begin())) {
            std::cerr << "unexpected child " << std::hex << site << std::dec << " in step " << next + 1 << '\n';
            std::exit(1);
        }
        const auto& c = calls[next++];
        for (size_t i = 0; i < c.writes.size(); ++i)
            memory.write<uint32_t>(c.target_buffer + 4 * i, c.writes[i]);
        return c;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, ShovelStageSelection which) {
        constexpr std::array sites{0x15e8158u, 0x15e79f0u, 0x15e7ad8u};
        const auto site = sites[static_cast<size_t>(which)];
        const auto str = invoke(site, {owner, index}).outcome;
        return uint32_t(invoke(site + 0xc, {memory.read<uint64_t>(str), 0}).outcome);
    }

    float sample_current(TerrainOwner owner, ShovelStageBaseHeightQuery query) {
        return std::bit_cast<float>(invoke(0x15e8224, {owner.address + 0x28, query.point, query.material}).height);
    }

    float sample_special(TerrainOwner owner, ShovelStageBaseHeightQuery query) {
        return std::bit_cast<float>(invoke(0x15e82ec, {owner.address + 0x28, query.point, query.material}).height);
    }

    float sample_other(ShovelStageBaseHeightQuery query) {
        return std::bit_cast<float>(invoke(0x15e831c, {query.point, query.material, query.context.address}).height);
    }

    FieldItem lookup(ShovelStageFieldQuery query, ShovelStageFieldKey stage) {
        invoke(0x15e7bd4, {stage.key.hash});
        return {invoke(0x15e7bf0, {stack, query.layer, query.point.address, query.root.address}).outcome};
    }
    bool accept_stage(uint64_t stage, uint32_t layer) {
        return invoke(0x1951934, {stage, layer}).outcome & 1;
    }

    FieldItem cell_at(uint64_t provider, HalfTileRef point, HalfTileRef root) {
        return {invoke(0x19519ec, {provider, point.address, root.address}).outcome};
    }

};
} // namespace

int main(int argc, char**) {
    uint64_t base, entry, stack, arg0, arg1, arg2, arg3, output;
    size_t memory_size, call_count;
    while (std::cin >> base >> entry >> stack >> arg0 >> arg1 >> arg2 >> arg3 >> output >> memory_size >> call_count) {
        Memory memory;
        for (size_t i = 0; i < memory_size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Call> calls(call_count);
        for (auto& call : calls) {
            size_t length;
            std::cin >> call.site >> call.outcome >> call.height >> call.target_buffer >> length;
            for (auto& arg : call.args) std::cin >> arg;
            call.writes.resize(length);
            for (auto& value : call.writes) std::cin >> value;
        }
        Services services{memory, calls, stack};
        const ShovelStageContextQueries queries{base, memory};
        uint64_t result = 0;
        if (argc == 1 && entry == 0x15e80f0) {
            result = std::bit_cast<uint32_t>(queries.base_height({arg0, arg1, {arg2}}, services));
        } else if (argc == 1 && entry == 0x15e7980) {
            result = queries.field_item({{arg0}, {arg1}, uint32_t(arg2), {arg3}}, services).address;
        } else if (argc == 1) {
            result = shovel_stage_cell(base, {arg0, uint32_t(arg1), {arg2}, {arg3}}, memory, services).address;
        }
        std::cout << result << ' ' << services.next;
        if (output) {
            const size_t size = entry == 0x15e80f0 ? 4 : 16;
            for (size_t i = 0; i < size / 4; ++i) std::cout << ' ' << memory.read<uint32_t>(output + i*4);
        }
        for (const auto& write : memory.writes)
            for (const auto value : write) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
