#include "../games/acnh/shovel_step.h"
#include "fixture_memory.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

constexpr uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    void write_word(uint64_t address, uint32_t value) {
        writes.push_back({address, 4, value});
        for (size_t i = 0; i < 4; ++i) bytes.at(address + i) = (value >> (8*i)) & 0xff;
    }
    template<class T>
    void write(uint64_t address, T value) {
        static_assert(sizeof(T) == 4);
        write_word(address, value);
    }
};

struct Action {
    uint64_t site, result, output;
    std::vector<uint32_t> effects;
};

struct Services {
    Memory& memory;
    const std::vector<Action>& actions;
    size_t next = 0;
    ShovelStepMove move{};
    std::vector<uint32_t> shapes;

    const Action& invoke(uint64_t site) {
        if (next >= actions.size() || actions[next].site != site) {
            std::cerr << "unexpected step callback " << std::hex << site << std::dec << " in slot " << next << '\n';
            std::exit(1);
        }
        const auto& action = actions[next++];
        for (size_t i = 0; i < action.effects.size(); ++i)
            memory.write_word(action.output + 4*i, action.effects[i]);
        return action;
    }

    void shape(std::initializer_list<uint32_t> words) {
        shapes.insert(shapes.end(), words.begin(), words.end());
    }

    uint32_t heading(float dx, float dz) {
        shape({bits(dx), bits(dz)});
        return uint32_t(invoke(0x15f0d14).result);
    }

    void prepare(ShovelStepMove step) { move = step; }

    float collision_radius() {
        invoke(0x15f0ebc);
        return std::bit_cast<float>(memory.read<uint32_t>(invoke(0x15f0ed0).result));
    }

    bool cast(uint64_t site, ShovelTargetPoint point, float radius, uint32_t mask) {
        shape({bits(point.x), bits(point.y), bits(point.z), bits(radius), bits(1.f), mask, 1});
        return uint32_t(invoke(site).result) != 0;
    }

    bool try_intermediate_probes(ShovelTargetPoint point, float radius) {
        const bool outer = cast(0x15f0f10, point, radius, 0x10);
        const bool inner = cast(0x15f0f48, point, radius, 4);
        return outer || !inner;
    }

    bool accept_intermediate(ShovelTargetPoint point, float radius, unsigned index) {
        constexpr std::array casts{0x15f0fc4u, 0x15f1024u, 0x15f1088u};
        constexpr std::array validate{0x15f0fe0u, 0x15f1040u, 0x15f10a4u};
        if (cast(casts[index], point, radius, 0x67)) return false;
        shape({bits(point.x), bits(point.y), bits(point.z), bits(radius), 1, 1, 0});
        return invoke(validate[index]).result & 1;
    }

    bool fallback(uint64_t) {
        const auto options = (move.options & ~(0xffffull << 32)) | (1ull << 32);
        shape({bits(move.origin.x), bits(move.origin.y), bits(move.origin.z),
               bits(move.end.x), bits(move.end.y), bits(move.end.z), uint32_t(options), uint32_t(options >> 32)});
        return invoke(0x15f10bc).result & 1;
    }
};
} // namespace

int main(int argc, char**) {
    uint64_t input, output;
    size_t memory_size, action_count;
    while (std::cin >> input >> output >> memory_size >> action_count) {
        Memory memory;
        for (size_t i = 0; i < memory_size; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Action> actions(action_count);
        for (auto& action : actions) {
            size_t length;
            std::cin >> action.site >> action.result >> action.output >> length;
            action.effects.resize(length);
            for (auto& value : action.effects) std::cin >> value;
        }
        Services services{memory, actions};
        const auto result = argc == 1 && shovel_check_step({input,output}, memory,services);
        std::cout << result << ' ' << services.next;
        if (output) {
            for (size_t i = 0; i < 3; ++i) std::cout << ' ' << memory.read<uint32_t>(output + 4*i);
        }
        for (const auto value : services.shapes) std::cout << ' ' << value;
        for (const auto& write : memory.writes)
            for (const auto value : write) std::cout << ' ' << value;
        std::cout << '\n';
    }
}
