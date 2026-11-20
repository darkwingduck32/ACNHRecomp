#include "../games/acnh/shovel_point_classifier.h"
#include "fixture_memory.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <cstdlib>
#include <vector>

namespace {
using namespace nx::acnh;

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;
    uint64_t stack_start = 0;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = value;
        if (address < stack_start || address >= stack_start + 0xf0)
            writes.push_back({address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i) bytes.at(address + i) = (bits >> (8 * i)) & 0xff;
    }
};

struct Call {
    uint64_t site, target, result;
    std::array<uint64_t, 5> arguments;
    uint32_t float_bits;
    uint64_t buffer_address;
    std::vector<unsigned> buffer;
};

struct ReplayServices {
    uint64_t base;
    Memory& memory;
    const std::vector<Call>& calls;
    size_t next = 0;

    const Call& invoke(uint64_t site, std::initializer_list<uint64_t> arguments = {}) {
        if (next == calls.size() || calls[next].site != site
            || !std::equal(arguments.begin(), arguments.end(), calls[next].arguments.begin())) {
            std::cerr << "unexpected child " << std::hex << site << std::dec << " in step " << next + 1 << '\n';
            std::exit(1);
        }
        const auto& call = calls[next++];
        for (size_t i = 0; i < call.buffer.size(); i += 4) {
            const uint32_t word = call.buffer[i] | (call.buffer[i + 1] << 8)
                                | (call.buffer[i + 2] << 16) | (call.buffer[i + 3] << 24);
            memory.write<uint32_t>(call.buffer_address + i, word);
        }
        return call;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index, ShovelPointStage phase) {
        constexpr std::array sites{0x15f3f48u, 0x15f4108u, 0x15f4298u};
        const auto site = sites[static_cast<size_t>(phase)];
        const auto address = invoke(site, {owner, index}).result;
        return uint32_t(invoke(site + 0xc, {memory.read<uint64_t>(address), 0}).result);
    }

    float terrain_height(uint64_t point, uint16_t) {
        return std::bit_cast<float>(invoke(0x15f3ff8, {point, 0, 1, memory.stack_start + 8, 0}).float_bits);
    }

    float surface_height(uint64_t point) {
        return std::bit_cast<float>(invoke(0x15f4010, {point, 0, 0}).float_bits);
    }

    bool point_blocked(uint64_t point) { return invoke(0x15f4024, {point}).result & 1; }

    bool can_step(ShovelTargetPoint, ShovelTargetPoint, ShovelTargetPoint& output) {
        const auto& call = invoke(0x15f418c, {memory.stack_start + 0x50, memory.stack_start + 0x70});
        const auto buffer = memory.stack_start + 0x70;
        output = {std::bit_cast<float>(memory.read<uint32_t>(buffer)), std::bit_cast<float>(memory.read<uint32_t>(buffer + 4)), std::bit_cast<float>(memory.read<uint32_t>(buffer + 8))};
        return call.result & 1;
    }

    uint32_t try_alternate(ShovelTargetPoint, ShovelTargetPoint, float) {
        return uint32_t(invoke(0x15f41dc, {memory.stack_start + 8}).result);
    }

    FieldItem field_record(HalfTile, uint32_t stage, ShovelPointStage phase) {
        const bool raised = phase == ShovelPointStage::raised;
        invoke(raised ? 0x15f42d4 : 0x15f4610, {stage});
        const auto point = memory.stack_start + (raised ? 0x50 : 0x90);
        const auto alternate = memory.stack_start + (raised ? 0x90 : 0x80);
        return {invoke(raised ? 0x15f42f0 : 0x15f462c, {memory.stack_start + 8, 0, point, alternate}).result};
    }

    bool terrain_excluded(TerrainOwner owner) {
        return invoke(0x15f436c, {owner.address + 0x28}).result & 1;
    }

    bool blocked_step(ShovelTargetPoint) { return invoke(0x15f43bc, {memory.stack_start + 8, 0, 0, 0}).result != 0; }

    int32_t adjust_step(ShovelPointQuery query) {
        return int32_t(invoke(0x15f43d0, {query.request.address, query.point}).result);
    }

    bool terrain_barrier(HalfTile) { return invoke(0x15f47e4, {memory.stack_start + 8}).result & 1; }

    uint32_t item_id(FieldItem item, ShovelPointItemId use) {
        constexpr std::array sites{0x15f4440u, 0x15f44b8u, 0x15f4504u, 0x15f4594u, 0x15f45c0u,
                                   0x15f4678u, 0x15f46a4u, 0x15f46c4u, 0x15f473cu, 0x15f4788u};
        return uint32_t(invoke(sites[static_cast<size_t>(use)], {item.address}).result);
    }

    ItemDescriptor copy_item(FieldItem item) {
        const auto descriptor = memory.stack_start + 8;
        memory.write<uint64_t>(descriptor, memory.read<uint64_t>(base + 0x4ac0510) + 0x10);
        memory.write<uint64_t>(descriptor + 8, memory.read<uint64_t>(item.address));
        memory.write<uint64_t>(descriptor + 0x10, 0);
        return {descriptor};
    }

    ItemDescriptor describe_item(FieldItem item, ShovelPointStage phase) {
        const auto descriptor = memory.stack_start + 8;
        invoke(phase == ShovelPointStage::raised ? 0x15f45dc : 0x15f483c, {descriptor, item.address});
        return {descriptor};
    }

    bool item_supported(FieldItem item, ShovelPointStage phase) {
        return invoke(phase == ShovelPointStage::raised ? 0x15f4534 : 0x15f480c, {item.address}).result & 1;
    }

    bool metadata_enabled(ItemDescriptor descriptor) {
        return invoke(0x15f47b8, {descriptor.address}).result & 1;
    }

    bool alternate_enabled(ItemDescriptor descriptor, ShovelPointStage phase) {
        return invoke(phase == ShovelPointStage::raised ? 0x15f4540 : 0x15f4818, {descriptor.address}).result & 1;
    }

    uint32_t category(FieldItem item, CatalogEntry metadata, ShovelPointCategory use) {
        constexpr std::array sites{0x15f45ecu, 0x15f48d4u, 0x15f48f4u, 0x15f484cu, 0x15f486cu, 0x15f48a4u};
        return uint32_t(invoke(sites[static_cast<size_t>(use)], {item.address, metadata.address}).result);
    }

    bool category_override(ItemDescriptor descriptor) {
        return invoke(0x15f488c, {descriptor.address, 0}).result & 1;
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t base, stack, point, request;
    size_t memory_size, call_count;
    while (std::cin >> base >> stack >> point >> request >> memory_size >> call_count) {
        Memory memory;
        memory.stack_start = stack;
        for (size_t i = 0; i < 0xf0; ++i) memory.bytes.emplace(stack + i, 0);
        for (size_t i = 0; i < memory_size; ++i) {
            uint64_t address;
            unsigned value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        std::vector<Call> calls(call_count);
        for (auto& call : calls) {
            size_t length;
            std::cin >> call.site >> call.target >> call.result >> call.float_bits >> call.buffer_address >> length;
            for (auto& argument : call.arguments) std::cin >> argument;
            call.buffer.resize(length);
            for (auto& byte : call.buffer) std::cin >> byte;
        }
        ReplayServices services{base, memory, calls};
        const ShovelPointQuery query{point, {request}};
        const auto result = argc > 1 ? 0 : ShovelPointClassifier{base, memory, services}.classify(query);
        std::cout << result << ' ' << services.next;
        for (size_t i = 0; i < 0x44; ++i) std::cout << ' ' << unsigned(memory.read<uint8_t>(request + i));
        for (const auto& write : memory.writes) {
            for (const auto value : write) std::cout << ' ' << value;
        }
        std::cout << '\n';
    }
}
