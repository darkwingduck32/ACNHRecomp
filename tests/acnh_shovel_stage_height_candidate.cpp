#include "../games/acnh/shovel_stage_height.h"
#include "fixture_memory.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

struct Memory : FixtureMemory {
    uint64_t frame;
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        const uint64_t bits = value;
        if (address < frame || address >= frame + 0xb0) writes.push_back({address, sizeof(T), bits});
        for (size_t i = 0; i < sizeof(T); ++i) bytes.at(address + i) = (bits >> (8*i)) & 0xff;
    }
};

struct Action {
    uint64_t site, result, output;
    uint32_t height;
    std::array<uint64_t, 5> args;
    std::vector<uint32_t> effects;
};

struct Services {
    uint64_t base;
    Memory& memory;
    const std::vector<Action>& actions;
    size_t next = 0;

    uint64_t stack(uint64_t offset) const { return memory.frame + offset; }

    const Action& invoke(uint64_t site, std::initializer_list<uint64_t> args = {}) {
        if (next >= actions.size() || actions[next].site != site
            || !std::equal(args.begin(), args.end(), actions[next].args.begin())) {
            std::cerr << "unexpected child " << std::hex << site << std::dec << " in step " << next + 1 << '\n';
            std::exit(1);
        }
        const auto& action = actions[next++];
        for (size_t i = 0; i < action.effects.size(); ++i)
            memory.write<uint32_t>(action.output + 4*i, action.effects[i]);
        return action;
    }

    float height(uint64_t site, std::initializer_list<uint64_t> args) {
        return std::bit_cast<float>(invoke(site,args).height);
    }

    uint64_t local_material() { return stack(0x68); }

    float base_height(uint64_t point, uint64_t material, uint64_t stage) {
        return height(0x15e0720, {point, material, stage});
    }

    FieldItem field_item(HalfTile, uint64_t stage, StageHeightLayer layer) {
        const bool first = layer == StageHeightLayer::primary;
        return {invoke(first ? 0x15e0794 : 0x15e0c50, {stack(8), stack(0x20), !first, stage}).result};
    }

    uint32_t item_id(FieldItem item, StageHeightItemId use) {
        constexpr std::array sites{0x15e07ecu, 0x15e0824u, 0x15e0944u, 0x15e09d8u, 0x15e0a24u, 0x15e0c68u};
        return uint32_t(invoke(sites[static_cast<size_t>(use)], {item.address}).result);
    }

    ItemDescriptor describe_item(FieldItem item) {
        invoke(0x15e081c, {stack(8), item.address});
        return {stack(8)};
    }

    uint32_t height_variant(FieldItem item) { return uint32_t(invoke(0x15e0830, {item.address}).result); }

    float surface_height(uint64_t point) { return height(0x15e08c0, {point, 0, 0}); }

    float descriptor_height(ItemDescriptor descriptor) {
        const auto metadata = memory.read<uint64_t>(descriptor.address + 0x10);
        if (!metadata) return 0.f;
        const auto owner = memory.read<uint64_t>(memory.read<uint64_t>(base + 0x4abbca8));
        const auto ptr = invoke(0x15e0920, {metadata, 0, 0x0c315945, owner}).result;
        return ptr ? std::bit_cast<float>(memory.read<uint32_t>(ptr)) : 0.f;
    }

    bool alternate_blocked(FieldItem item) { return invoke(0x15e0984, {item.address}).result & 1; }

    bool item_supported(FieldItem item, StageHeightLayer layer) {
        return invoke(layer == StageHeightLayer::primary ? 0x15e0990 : 0x15e0ca8, {item.address}).result & 1;
    }

    ItemDescriptor copy_item(FieldItem item) {
        const auto descriptor = stack(8);
        memory.write<uint64_t>(descriptor, base + 0x4936b50);
        memory.write<uint64_t>(descriptor + 8, memory.read<uint64_t>(item.address));
        memory.write<uint64_t>(descriptor + 0x10, 0);
        return {descriptor};
    }

    bool alternate_enabled(ItemDescriptor descriptor, StageHeightLayer layer) {
        return invoke(layer == StageHeightLayer::primary ? 0x15e0a54 : 0x15e0cc0, {descriptor.address}).result & 1;
    }

    uint32_t stage_hash(uint64_t owner, uint32_t index) {
        const auto address = invoke(0x15e0ae4, {owner, index}).result;
        return uint32_t(invoke(0x15e0af0, {memory.read<uint64_t>(address), 0}).result);
    }

    bool special_item(FieldItem item, StageHeightLayer layer) {
        return invoke(layer == StageHeightLayer::primary ? 0x15e0b18 : 0x15e0d74, {item.address}).result & 1;
    }

    float alternate_height(ItemDescriptor descriptor, StageHeightLayer layer, bool stage_matched) {
        return height(layer == StageHeightLayer::secondary ? 0x15e0ddc : stage_matched ? 0x15e0be0 : 0x15e0b24,
                      {descriptor.address});
    }

    bool terrain_here(HalfTile, StageHeightLayer layer) {
        return invoke(layer == StageHeightLayer::primary ? 0x15e0ba8 : 0x15e0da4, {stack(0x20)}).result & 1;
    }

    uint32_t mapped_stage(uint16_t index, StageHeightLayer layer) {
        return uint32_t(invoke(layer == StageHeightLayer::primary ? 0x15e0bb8 : 0x15e0db4, {index}).result);
    }

    bool terrain_elsewhere(uint32_t mapped, HalfTile, StageHeightLayer layer) {
        return invoke(layer == StageHeightLayer::primary ? 0x15e0bcc : 0x15e0dc8, {mapped, stack(0x20)}).result & 1;
    }

    ItemDescriptor describe_secondary(FieldItem item) {
        invoke(0x15e0cb8, {stack(8), item.address});
        return {stack(8)};
    }

    uint64_t secondary_stage() { return invoke(0x15e0d18).result; }
};
} // namespace

int main(int argc, char**) {
    uint64_t base, frame, point, material, stage;
    uint32_t flags, specials;
    size_t memory_size, action_count;
    while (std::cin >> base >> frame >> point >> material >> stage >> flags >> specials >> memory_size >> action_count) {
        Memory memory;
        memory.frame = frame;
        for (size_t i = 0; i < 0xb0; ++i) memory.bytes.emplace(frame+i,0);
        for (size_t i = 0; i < memory_size; ++i) {
            uint64_t addr;
            unsigned byte;
            std::cin >> addr >> byte;
            memory.bytes.emplace(addr,byte);
        }
        std::vector<Action> actions(action_count);
        for (auto& action : actions) {
            size_t length;
            std::cin >> action.site >> action.result >> action.height >> action.output >> length;
            for (auto& argument : action.args) std::cin >> argument;
            action.effects.resize(length);
            for (auto& effect : action.effects) std::cin >> effect;
        }
        Services services{base,memory,actions};
        const ShovelStageHeightQuery query{point,material,stage,flags,(specials & 1) != 0};
        const float result = argc > 1 ? 0.f : ShovelStageHeight{base,memory,services}.sample(query);
        std::cout << std::bit_cast<uint32_t>(result) << ' ' << services.next;
        if (material) std::cout << ' ' << memory.read<uint32_t>(material);
        for (const auto& write : memory.writes)
            for (const auto value : write) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
