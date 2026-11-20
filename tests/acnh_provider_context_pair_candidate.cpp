#include "../games/acnh/field_provider_context_pair.h"

#include <iostream>
#include <map>
#include <tuple>
#include <vector>

namespace {

struct Memory {
    std::map<uint64_t, uint8_t> bytes;
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    T read(uint64_t address) const {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value |= uint64_t(bytes.at(address + i)) << (8 * i);
        return static_cast<T>(value);
    }

    template<class T>
    void write(uint64_t address, T value) {
        writes.emplace_back(address, sizeof(T), value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes.at(address + i) = (value >> (8 * i)) & 255;
    }
};

struct Services {
    Memory& memory;
    uint64_t pair, counter_address, replacement;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> calls;

    uint64_t counter(uint64_t handle, bool first) {
        calls.emplace_back(first ? 0x262ea34 : 0x262e9f4, 0x37567d0, handle);
        if (replacement) memory.write<uint64_t>(pair, replacement);
        return counter_address;
    }

    void prepare_counter(uint64_t counters, bool first) {
        calls.emplace_back(first ? 0x262ea4c : 0x262ea0c, 0x37e2e80, counters);
    }
};

}

int main() {
    uint64_t pair, counter, replacement;
    size_t count;
    while (std::cin >> pair >> counter >> replacement >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, static_cast<uint8_t>(byte));
        }

        Services services{memory, pair, counter, replacement, {}};
        nx::acnh::acquire_provider_pair({pair}, memory, services);
        std::cout << services.calls.size();
        for (const auto& [site, target, receiver] : services.calls)
            std::cout << ' ' << site << ' ' << target << ' ' << receiver;
        for (const auto& [address, width, value] : memory.writes)
            std::cout << ' ' << address << ' ' << width << ' ' << value;
        std::cout << '\n';
    }
}
