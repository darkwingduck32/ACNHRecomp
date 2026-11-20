#pragma once

#include <bit>
#include <cstdint>

namespace nx::acnh {

struct ShovelStageIndex { uint32_t value; };
struct ShovelStageNameRecord { uint64_t address; };

constexpr ShovelStageNameRecord shovel_stage_name_record(uint64_t base, ShovelStageIndex index) {
    return {base + 0x47f9ec0 + 24ll * std::bit_cast<int32_t>(index.value)};
}

}  // namespace nx::acnh
