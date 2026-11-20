#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace nx::acnh {

struct FieldActionState { uint64_t address; };

template<class Memory>
bool field_action_advance(FieldActionState action, uint32_t event, Memory& memory) {
    const auto state = action.address;
    if (event != 1 && (event != 2
        || !(memory.template read<uint8_t>(state + 9) | memory.template read<uint8_t>(state + 10)))) {
        return false;
    }
    const auto selected = memory.template read<uint64_t>(state + 0x4d0);
    memory.template write<uint16_t>(state + 9, 0);
    const auto next = std::bit_cast<int32_t>(memory.template read<uint32_t>(state + 0x30) + 1u);
    const auto cycle = memory.template read<uint64_t>(selected);
    const auto size = memory.template read<int32_t>(cycle);
    const uint32_t index = size ? uint32_t(int64_t(next) % size) : uint32_t(next);
    memory.template write<uint32_t>(state + 0x30, index);
    memory.template write<uint64_t>(state + 0xc0, state + 0xc8 + 0x58ull * index);
    return true;
}

enum class FieldActionSlot { first, second };

template<class Memory>
bool field_action_has_slot(FieldActionState action, FieldActionSlot slot, const Memory& memory) {
    const auto group = memory.template read<uint64_t>(action.address + 0x240);
    const auto column = slot == FieldActionSlot::first ? 0 : 1;
    bool result = false;
    for (unsigned offset = 0x350; offset <= 0x360; offset += 8) {
        const auto record = memory.template read<uint64_t>(group + offset);
        if (record && (memory.template read<uint8_t>(record + 0x50 + column) != 0xff
                       || memory.template read<uint8_t>(record + 0x13 + column) != 0xff)) {
            result = true;
        }
    }
    return result;
}

template<class Memory>
bool field_action_ready(FieldActionState action, const Memory& memory) {
    const auto state = action.address;
    const auto timer = memory.template read<uint64_t>(state + 0x78);
    if (!memory.template read<uint8_t>(timer + 0x1e)) return false;
    const auto amount = memory.template read<int32_t>(timer + 0x270);
    if (amount >= 0) {
        const float interval = float(memory.template read<int32_t>(timer + 0x278));
        const float offset = std::bit_cast<float>(memory.template read<uint32_t>(state + 0x64));
        const float cursor = std::bit_cast<float>(memory.template read<uint32_t>(state + 0x44));
        if (cursor < (offset + interval) + float(amount)) return false;
    }
    const auto settings = memory.template read<uint64_t>(state + 0xa0);
    const bool forced = memory.template read<uint8_t>(settings + 0x792) != 0;
    const auto mode = memory.template read<int32_t>(state + 0x2c);
    return forced || mode < 1;
}

enum class FieldActionCurve { linear_blend, power };

template<class Memory>
float field_action_weight(FieldActionState action, FieldActionCurve curve, const Memory& memory) {
    const auto state = action.address;
    const auto owner = memory.template read<uint64_t>(state + 0x240);
    const auto enabled = memory.template read<uint64_t>(owner + 0x10);
    const bool blend = curve == FieldActionCurve::linear_blend;
    const auto weight = [&](float input, unsigned mode, unsigned baseline) {
        if (!(input >= 0.f && input < 1.f) || !memory.template read<uint8_t>(enabled + mode))
            return 1.f;
        const auto settings = memory.template read<uint64_t>(state + 0xa0);
        if (blend) {
            const float base = std::bit_cast<float>(memory.template read<uint32_t>(settings + baseline));
            return std::fma(input, 1.f - base, base);
        }
        switch (memory.template read<uint8_t>(settings + mode)) {
        case 1: return input;
        case 2: return input * input;
        case 3: {
            const float squared = input * input;
            return squared * squared;
        }
        default: return 1.f;
        }
    };
    const auto input = [&](unsigned offset) { return std::bit_cast<float>(memory.template read<uint32_t>(state + offset)); };
    const float right = weight(input(0x70), blend ? 0x79a : 0x799, 0x818);
    const float left = weight(input(0x6c), blend ? 0x79c : 0x79b, 0x81c);
    return right * left;
}

template<class Memory>
uint64_t field_action_position(FieldActionState action, const Memory& memory) {
    const auto state = action.address;
    const auto group = memory.template read<uint64_t>(state + 0x240);
    const auto base = memory.template read<uint64_t>(state + 0x788);
    if ((memory.template read<uint32_t>(group + 0x340) & ~1u) != 2) return base;
    const auto offsets = memory.template read<uint64_t>(state + 0x4a8);
    return base + memory.template read<uint64_t>(offsets + 0x18);
}

template<class Memory>
bool field_action_can_start(FieldActionState action, const Memory& memory) {
    const auto owner = memory.template read<uint64_t>(action.address + 0xa0);
    const auto mode = memory.template read<uint8_t>(owner + 0x880);
    return mode <= 13 && ((1u << mode) & 0x2064) && !memory.template read<uint32_t>(owner + 0x8bc);
}

} // namespace nx::acnh
