#include "../games/acnh/field_action_state.h"
#include "fixture_memory.h"

#include <array>
#include <iostream>
#include <vector>

namespace {

struct Memory : FixtureMemory {
    std::vector<std::array<uint64_t, 3>> writes;

    template<class T>
    void write(uint64_t address, T value) {
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[address + i] = (uint64_t(value) >> (8 * i)) & 0xff;
        writes.push_back({address, sizeof(T), uint64_t(value)});
    }
};

} // namespace

int main() {
    using namespace nx::acnh;
    unsigned mode;
    uint64_t object;
    uint32_t event;
    size_t size;
    while (std::cin >> mode >> object >> event >> size) {
        Memory memory;
        for (size_t index = 0; index < size; ++index) {
            uint64_t address;
            uint32_t value;
            std::cin >> address >> value;
            memory.bytes.emplace(address, value);
        }
        const FieldActionState action{object};
        uint64_t result;
        switch (mode) {
        case 0: result = field_action_advance(action, event, memory); break;
        case 1: result = field_action_has_slot(action, FieldActionSlot::first, memory); break;
        case 2: result = field_action_has_slot(action, FieldActionSlot::second, memory); break;
        case 3: result = field_action_ready(action, memory); break;
        case 4: result = std::bit_cast<uint32_t>(field_action_weight(action, FieldActionCurve::linear_blend, memory)); break;
        case 5: result = std::bit_cast<uint32_t>(field_action_weight(action, FieldActionCurve::power, memory)); break;
        case 6: result = field_action_position(action, memory); break;
        case 7: result = field_action_can_start(action, memory); break;
        default: return 2;
        }
        std::cout << result;
        for (const auto& [address, size, value] : memory.writes)
            std::cout << ' ' << address << ' ' << size << ' ' << value;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 1;
}
