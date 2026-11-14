#pragma once
#include <cstdint>
#include <vector>

namespace nx::nvn {
inline constexpr uint32_t kInstanceMaterialLocation=29; // 30/31 belong to temporal motion
inline constexpr uint32_t kInstanceMaterialFlag=0x80000000u;
bool patch_instance_block(const std::vector<uint32_t>& in, uint32_t binding, uint32_t step_vec4,
                          std::vector<uint32_t>& out);
bool patch_instance_blocks(const std::vector<uint32_t>& in, const uint32_t* bindings, const uint32_t* steps,
                           uint32_t nb, std::vector<uint32_t>& out);
// Vertex: gather its blocks and export the instance index as a flat varying.
// Fragment: gather its blocks using that varying. Unsupported interfaces fail.
bool patch_instance_material_blocks(const std::vector<uint32_t>& in, const uint32_t* bindings,
                                    const uint32_t* steps, uint32_t nb, bool fragment,
                                    std::vector<uint32_t>& out);
}
