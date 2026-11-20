#include "../games/acnh/field_provider_anchor.h"

#include <iostream>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint64_t base = 0x4000000000;
constexpr uint64_t table = 0x23000;
constexpr uint64_t output = 0x26000;

struct Memory {
    std::unordered_map<uint64_t, unsigned> bytes;
    std::vector<std::tuple<uint64_t, unsigned, uint64_t>> writes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t result = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            result |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(result);
    }

    template<class T>
    void write(uint64_t address, T value) {
        writes.emplace_back(address, sizeof(T), uint64_t(value));
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
    }
};

struct Services {
    bool ready;
    std::vector<uint64_t> calls;

    void note(uint64_t address, uint64_t argument) {
        calls.push_back(address);
        calls.push_back(argument);
    }

    nx::acnh::ProviderTable table(nx::acnh::ProviderReader reader) {
        note(0x120000, reader.address);
        return {::table};
    }

    uint32_t width(nx::acnh::ProviderTable table) {
        note(0x120020, table.address);
        return 8;
    }

    uint32_t height(nx::acnh::ProviderTable table) {
        note(0x120040, table.address);
        return 4;
    }

    bool begin(nx::acnh::ProviderContext context) {
        note(0x120060, context.address);
        return ready;
    }
};

}

int main() {
    unsigned method, mode;
    uint64_t provider;
    nx::acnh::ProviderCell position{};
    size_t count;
    while (std::cin >> method >> mode >> provider >> position.x >> position.z >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, byte);
        }
        Services services{mode % 2 != 0, {}};
        if (method == 0) {
            nx::acnh::acquire_provider_anchor(base, {provider}, position, {output}, memory, services);
        } else {
            nx::acnh::release_provider_anchor(base, {output}, memory, [&](nx::acnh::ProviderContext context) {
                services.note(0x120080, context.address);
            });
        }
        std::cout << services.calls.size() / 2;
        for (const auto value : services.calls) std::cout << ' ' << value;
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
}
