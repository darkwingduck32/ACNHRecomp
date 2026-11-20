#include "../games/acnh/action_mask.h"

#include <array>
#include <cstring>
#include <iostream>
#include <tuple>
#include <vector>

struct ActionObject {
    std::array<uint8_t, 0x29> bytes{};
    std::vector<std::tuple<uint64_t, size_t, uint64_t>> writes;

    template<class T>
    T read(uint64_t offset) const {
        T value;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    template<class T>
    void write(uint64_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
        writes.emplace_back(offset, sizeof(T), static_cast<uint64_t>(value));
    }
};

int main() {
    uint64_t original, working;
    unsigned initialized;
    while (std::cin >> original >> working >> initialized) {
        ActionObject object;
        object.write(0x10, original);
        object.write(0x20, working);
        object.write(0x28, static_cast<uint8_t>(initialized));
        object.writes.clear();
        nx::acnh::initialize_action_mask(0, object);
        std::cout << object.read<uint64_t>(0x10) << ' ' << object.read<uint64_t>(0x20)
                  << ' ' << unsigned(object.read<uint8_t>(0x28));
        for (const auto& [offset, size, value] : object.writes)
            std::cout << ' ' << offset << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
