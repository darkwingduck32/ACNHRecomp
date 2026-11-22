#pragma once
#include "common/byte_patch.h"
#include "nvn_pipeline.h"
#include <optional>
#include <string>
namespace nx::nvn::replay {
struct UniformExtension {
    std::string id;
    int priority = 0;
    bool (*host_owned)(const ShaderInfo *, int) = nullptr;
    bool (*matches)(const ShaderInfo *, const DescriptorBinding &) = nullptr;
    bool (*cacheable)(const ShaderInfo *, const DescriptorBinding &) = nullptr;
    BytePatch (*transform)(const ShaderInfo *, const DescriptorBinding &, const uint8_t *, uint64_t) = nullptr;
    bool ordered_overwrite = false; // explicit composition on already-patched fields
};
void register_uniform_extension(UniformExtension);
void freeze_uniform_extensions();
void invalidate_uniform_extensions(); // custom state changed outside config/epoch
bool extension_uniform_host_patch(const ShaderInfo *, int bank);
std::optional<VkDescriptorBufferInfo> extension_uniform_patch(const ShaderInfo *, const DescriptorBinding &,
                                                              const uint8_t *, uint64_t size, uint64_t padded);
void extension_uniform_stats();
} // namespace nx::nvn::replay
