#pragma once
#include "nvn/nvn_pipeline.h"
#include <optional>
namespace nx::nvn::replay {
void camera_uniform_stats();
bool camera_uniform_host_patch(const ShaderInfo *shader, int bank);
std::optional<VkDescriptorBufferInfo>
camera_uniform_patch(const ShaderInfo *shader, const DescriptorBinding &binding,
                     const uint8_t *data, uint64_t size, uint64_t padded_size);
} // namespace nx::nvn::replay
