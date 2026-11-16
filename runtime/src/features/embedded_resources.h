#pragma once
#include "common/artifact_cache.h"
#include "registry.h"
#include <span>
#include <string_view>
namespace nx::features {
enum class ResourceEncoding { Literal, Delta, ZstdDelta };
struct EmbeddedResource {
    std::string_view feature;
    std::string_view path;
    std::string_view input_sha256;
    std::string_view output_sha256;
    std::string_view bytes;
    ResourceEncoding encoding = ResourceEncoding::Literal;
    std::string_view content_sha256; // decoded output for ZstdDelta; output otherwise
    size_t content_size = 0;
};
// Generated private build data. Stock/replacement paths never enter the binary.
std::span<const EmbeddedResource> embedded_resources();
const EmbeddedResource *find_resource(std::string_view feature, std::string_view path);
ResourcePatch checked_replacement(const EmbeddedResource &, const ResourceRequest &, ArtifactCache *cache = nullptr);
bool resource_matches(const EmbeddedResource &, std::string_view bytes);
} // namespace nx::features
