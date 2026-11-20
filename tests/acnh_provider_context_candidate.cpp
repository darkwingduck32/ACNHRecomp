#include "../games/acnh/field_provider_context.h"

#include <iostream>
#include <map>
#include <tuple>
#include <vector>

namespace {

struct Memory {
    std::map<uint64_t, uint8_t> bytes;
    std::vector<std::tuple<uint64_t, unsigned, uint64_t>> writes;

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
    uint64_t flag;
    bool allowed;
    uint64_t selected;
    std::vector<std::pair<uint64_t, uint64_t>> calls;

    void call(uint64_t target, uint64_t argument) { calls.emplace_back(target, argument); }

    uint64_t disabled_flag(nx::acnh::ProviderContextOwner owner) {
        call(0x2601110, owner.address);
        return flag;
    }

    void enter_guard(uint64_t guard) { call(0x3f03880, guard); }
    bool try_guard(uint64_t guard) { call(0x3f04c70, guard); return allowed; }
    void leave_guard(uint64_t guard) { call(0x3f038c0, guard); }
    void enter_pair(uint64_t pair) { call(0x262e9bc, pair); }
    uint64_t counter(uint64_t handle, bool) { call(0x37567d0, handle); return selected; }
    void prepare_counter(uint64_t counters, bool) { call(0x37e2e80, counters); }
    void enter_first(uint64_t handle) { call(0x37569c0, handle); }
    void leave_second(uint64_t handle, bool) { call(0x37568c0, handle); }
    void leave_first(uint64_t handle, bool nested) { call(nested ? 0x3756a80 : 0x37568c0, handle); }
};

}

int main() {
    uint64_t owner, flag, selected;
    bool acquire, allowed;
    size_t count;
    while (std::cin >> acquire >> owner >> flag >> allowed >> selected >> count) {
        Memory memory;
        for (size_t i = 0; i < count; ++i) {
            uint64_t address;
            unsigned byte;
            std::cin >> address >> byte;
            memory.bytes.emplace(address, static_cast<uint8_t>(byte));
        }
        Services services{flag, allowed, selected, {}};
        if (acquire) std::cout << nx::acnh::acquire_provider_context({owner}, memory, services);
        else {
            nx::acnh::release_provider_context({owner}, memory, services);
            std::cout << 0;
        }
        std::cout << ' ' << services.calls.size();
        for (const auto& [target, argument] : services.calls) std::cout << ' ' << target << ' ' << argument;
        for (const auto& [address, width, value] : memory.writes)
            std::cout << ' ' << address << ' ' << width << ' ' << value;
        std::cout << '\n';
    }
}
