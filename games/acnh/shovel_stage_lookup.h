#pragma once

#include <bit>
#include <cstdint>

namespace nx::acnh {

struct ShovelStageIndex { uint32_t value; };
struct ShovelStageNameRecord { uint64_t address; };

constexpr ShovelStageNameRecord shovel_stage_name_record(uint64_t base, ShovelStageIndex index) {
    return {base + 0x47f9ec0 + 24ll * std::bit_cast<int32_t>(index.value)};
}

template<class Memory>
uint16_t shovel_stage_ordinal(uint64_t base, uint64_t stage, const Memory& memory) {
    const auto head = base + 0x54c1b58;
    auto node = memory.template read<uint64_t>(head + 8);
    if (node == head) return 0;
    const auto key = memory.template read<uint32_t>(stage + 0x38);
    uint16_t index = 0;
    for (; node != head; node = memory.template read<uint64_t>(node + 8), ++index) {
        if (memory.template read<uint32_t>(node - 8) == key) return index == 0xffff ? 0 : index;
    }
    return 0;
}

}  // namespace nx::acnh
