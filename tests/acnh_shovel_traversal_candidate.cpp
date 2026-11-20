#include "../games/acnh/shovel_traversal.h"
#include "../games/acnh/shovel_stage_lookup.h"
#include "fixture_memory.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace nx::acnh;

constexpr uint64_t scratch_start = 0x6000000000ull + 0x10000 - 0x120;

uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
float real(uint32_t value) { return std::bit_cast<float>(value); }

struct TraversalMemory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    T read(uint64_t address) const {
        if constexpr (std::is_same_v<T, float>) return real(read<uint32_t>(address));
        else {
            if (address >= scratch_start && address + sizeof(T) <= scratch_start + 0x120) {
                uint64_t value = 0;
                for (size_t offset = 0; offset < sizeof(T); ++offset) {
                    const auto item = bytes.find(address + offset);
                    if (item != bytes.end()) value |= uint64_t(item->second) << (8 * offset);
                }
                return static_cast<T>(value);
            }
            return FixtureMemory::read<T>(address);
        }
    }

    template<class T>
    void place(uint64_t address, T value) {
        uint64_t raw = 0;
        if constexpr (std::is_same_v<T, float>) raw = bits(value);
        else raw = static_cast<std::make_unsigned_t<T>>(value);
        for (size_t offset = 0; offset < sizeof(T); ++offset)
            bytes[address + offset] = uint8_t(raw >> (8 * offset));
    }

    template<class T>
    void write(uint64_t address, T value) {
        place(address, value);
        if (address < scratch_start || address >= scratch_start + 0x120) {
            if constexpr (std::is_same_v<T, float>) writes.push_back({address, sizeof(T), bits(value)});
            else writes.push_back({address, sizeof(T), uint64_t(value)});
        }
    }
};

struct Reply {
    uint32_t site;
    uint64_t result;
    bool touches_output;
    std::array<uint32_t, 3> vector;
};

struct Services {
    uint64_t base;
    TraversalMemory& memory;
    const std::vector<Reply>& replies;
    size_t next = 0;
    std::vector<uint64_t> arguments;

    const Reply& call(uint32_t site) {
        if (next == replies.size() || replies[next].site != site) {
            std::cerr << "Unexpected callback at " << std::hex << site << std::dec << " in slot " << next << '\n';
            std::exit(1);
        }
        arguments.push_back(site);
        return replies[next++];
    }

    void point(uint64_t address) {
        arguments.insert(arguments.end(), {memory.read<uint32_t>(address), memory.read<uint32_t>(address + 4),
                                           memory.read<uint32_t>(address + 8)});
    }

    void apply(const Reply& reply, uint64_t output) {
        if (!reply.touches_output) return;
        for (size_t index = 0; index < reply.vector.size(); ++index)
            memory.place(output + 4 * index, reply.vector[index]);
    }

    void heading(float dx, float dz) {
        call(0x15f1158);
        arguments.insert(arguments.end(), {bits(dx), bits(dz)});
    }

    bool rectangle(TraversalRectangleCheck query) {
        constexpr std::array calls{
            std::array{0x15f12f8u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
            std::array{0x15f13fcu, 0x15f1424u, 0x15f1470u, 0x15f1498u, 0x15f1500u, 0x15f1528u, 0x15f1570u, 0x15f1598u},
            std::array{0x15f162cu, 0x15f1654u, 0x15f16b0u, 0x15f16d8u, 0x15f1744u, 0x15f176cu, 0x15f1b60u, 0x15f1b88u},
            std::array{0x15f1804u, 0x15f182cu, 0x15f1878u, 0x15f18a0u, 0x15f1910u, 0x15f1938u, 0x15f1b60u, 0x15f1b88u},
            std::array{0x15f19e0u, 0x15f1a08u, 0x15f1a54u, 0x15f1a7cu, 0x15f1aecu, 0x15f1b14u, 0x15f1b60u, 0x15f1b88u},
        };
        const auto owner = memory.read<uint64_t>(memory.read<uint64_t>(base + 0x4ab27c8));
        const auto terrain = owner ? memory.read<uint64_t>(owner + 0xa8) : 0;
        if (!terrain) return false;
        const auto site = calls[static_cast<size_t>(query.side)][query.corner * 2 + query.wide];
        const auto& reply = call(site);
        arguments.insert(arguments.end(), {terrain, uint32_t(query.bounds.first.x), uint32_t(query.bounds.first.z),
                                           uint32_t(query.bounds.last.x), uint32_t(query.bounds.last.z), 0});
        return reply.result & 1;
    }

    float surface(uint64_t address, TraversalSurface site) {
        constexpr std::array calls{0x15f11d0u, 0x15f1370u, 0x15f1bccu, 0x15f1d1cu, 0x15f1d34u,
                                   0x15f1e3cu, 0x15f20d0u, 0x15f20e8u, 0x15f2044u, 0x15f2338u, 0x15f2350u};
        const auto& reply = call(calls[static_cast<size_t>(site)]);
        point(address);
        arguments.insert(arguments.end(), {0, 0, 0});
        return real(reply.vector[0]);
    }

    ShovelTargetPoint peak(uint64_t address, TraversalPeak site) {
        constexpr std::array calls{0x15f11e0u, 0x15f1380u, 0x15f2250u, 0x15f2260u};
        const auto& reply = call(calls[static_cast<size_t>(site)]);
        point(address);
        if (!reply.touches_output)
            return {memory.read<float>(address), memory.read<float>(address + 4), memory.read<float>(address + 8)};
        return {real(reply.vector[0]), real(reply.vector[1]), real(reply.vector[2])};
    }

    float stage(uint64_t address, TraversalStage site, float bias) {
        constexpr std::array calls{0x15f1cb8u, 0x15f1f28u, 0x15f21e8u};
        constexpr std::array getters{0x15f1c0cu, 0x15f1e7cu, 0x15f2084u};
        const auto index = static_cast<size_t>(site);
        const auto scene = memory.read<uint64_t>(memory.read<uint64_t>(base + 0x4ab2720));
        auto record = base + 0x54c04a0;
        if (scene && memory.read<uint64_t>(scene + 0x330)) {
            const auto owner = memory.read<uint64_t>(memory.read<uint64_t>(base + 0x4ab2640));
            const auto& name = call(getters[index]);
            arguments.insert(arguments.end(), {owner, memory.read<uint32_t>(scene + 0x338), bits(bias)});
            const auto& hash = call(getters[index] + 0xc);
            arguments.insert(arguments.end(), {memory.read<uint64_t>(name.result), 0, name.vector[0]});
            bias = real(hash.vector[0]);
            const auto alternate = memory.read<uint64_t>(base + 0x4ac3a80);
            if (uint32_t(hash.result) == 0x758f7eaf) record = alternate;
        }
        const auto ordinal = shovel_stage_ordinal(base, record, memory);
        const auto& reply = call(calls[index]);
        point(address);
        arguments.insert(arguments.end(), {0, 0, 0, ordinal, 0, 1, bits(bias)});
        return real(reply.vector[0]);
    }

    bool recurse(uint64_t descriptor, uint64_t output, TraversalLeg leg) {
        constexpr std::array calls{0x15f1dd8u, 0x15f1e24u, 0x15f1fe4u, 0x15f202cu};
        const auto& reply = call(calls[static_cast<size_t>(leg)]);
        record_descriptor(descriptor);
        point(output);
        apply(reply, output);
        return reply.result & 1;
    }

    bool sweep(uint64_t descriptor, uint64_t output, bool second) {
        const auto& reply = call(second ? 0x15f215c : 0x15f2140);
        record_descriptor(descriptor);
        point(output);
        arguments.push_back(second);
        apply(reply, output);
        return reply.result & 1;
    }

    void record_descriptor(uint64_t address) {
        for (unsigned index = 0; index < 31; ++index)
            arguments.push_back(memory.read<uint8_t>(address + index));
    }
};
} // namespace

int main() {
    uint64_t base, descriptor, output;
    size_t byte_count, reply_count;
    while (std::cin >> base >> descriptor >> output >> byte_count >> reply_count) {
        TraversalMemory memory;
        for (size_t index = 0; index < byte_count; ++index) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        std::vector<Reply> replies(reply_count);
        for (auto& reply : replies)
            std::cin >> reply.site >> reply.result >> reply.touches_output
                     >> reply.vector[0] >> reply.vector[1] >> reply.vector[2];
        Services services{base, memory, replies};
        const ShovelTraversalScratch scratch{scratch_start + 0x1c, scratch_start + 0x28,
                                            scratch_start + 0x38, scratch_start + 0x60, scratch_start + 0x88};
        const bool result = ShovelTraversal{{descriptor, output}, scratch, memory, services}.run();
        std::cout << result << ' ' << services.next << ' ' << memory.writes.size();
        for (const auto& write : memory.writes)
            for (auto word : write) std::cout << ' ' << word;
        if (output)
            for (unsigned index = 0; index < 3; ++index)
                std::cout << ' ' << memory.read<uint32_t>(output + 4 * index);
        for (auto value : services.arguments) std::cout << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
