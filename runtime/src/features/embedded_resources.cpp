#include "embedded_resources.h"
#include "common/resource_delta.h"
#include "common/sha256.h"
#include <cstdlib>
#include <map>
namespace nx::features {
const EmbeddedResource *find_resource(std::string_view feature, std::string_view path) {
    using Key = std::pair<std::string_view, std::string_view>;
    static const auto index = [] {
        std::map<Key, const EmbeddedResource *> result;
        for (const auto &resource : embedded_resources())
            result.emplace(Key{resource.feature, resource.path}, &resource);
        return result;
    }();
    const auto it = index.find({feature, path});
    return it == index.end() ? nullptr : it->second;
}
bool resource_matches(const EmbeddedResource &resource, std::string_view bytes) {
    if (resource.encoding != ResourceEncoding::ZstdDelta)
        return hash::digest(bytes) == resource.output_sha256;
    std::string decoded, error;
    return resource_delta::decompress(bytes, decoded, error, resource.content_size) &&
           decoded.size() == resource.content_size && hash::digest(decoded) == resource.content_sha256;
}
ResourcePatch checked_replacement(const EmbeddedResource &resource, const ResourceRequest &request,
                                  ArtifactCache *cache) {
    const auto actual = hash::digest(request.bytes);
    if (actual == resource.output_sha256 ||
        (actual != resource.input_sha256 && resource_matches(resource, request.bytes)))
        return {};
    if (actual != resource.input_sha256)
        return {{}, "replacement skipped; resource differs from reviewed input: " + std::string(request.path)};
    if (resource.encoding == ResourceEncoding::Literal)
        return {std::string(resource.bytes), {}};
    std::string error;
    auto valid = [&](std::string_view bytes) { return resource_matches(resource, bytes); };
    auto build = [&]() -> std::optional<std::string> {
        std::string decoded, output, packed;
        std::string_view original = request.bytes;
        if (resource.encoding == ResourceEncoding::ZstdDelta) {
            if (!resource_delta::decompress(original, decoded, error))
                return {};
            original = decoded;
        }
        if (!resource_delta::apply(original, resource.bytes, output, error) || output.size() != resource.content_size ||
            hash::digest(output) != resource.content_sha256) {
            if (error.empty())
                error = "Reconstructed content differs from reviewed output";
            return {};
        }
        if (resource.encoding == ResourceEncoding::ZstdDelta) {
            if (!resource_delta::compress(output, packed, error))
                return {};
            return packed;
        }
        return output;
    };
    if (!cache) {
        auto output = build();
        return {std::move(output), error};
    }
    // Include codec/patch bytes and original input. Compression changes must
    // bump this policy version even if decoded geometry remains unchanged.
    const auto key = hash::digest(std::string("native-resource-v1-zstd-1.5.7-level3\n") +
                                  std::to_string(int(resource.encoding)) + "\n" + std::string(resource.input_sha256) +
                                  "\n" + std::string(resource.content_sha256) + "\n" + hash::digest(resource.bytes));
    auto result = cache->get(key, valid, build);
    if (!result.bytes)
        return {{}, error.empty() ? result.diagnostic : error};
    std::string diagnostic = result.diagnostic;
    if (!result.hit || std::getenv("NX_NATIVE_RESOURCE_TRACE"))
        diagnostic = (result.hit ? "verified cached delta: " : "reconstructed delta: ") + std::string(request.path) +
                     (diagnostic.empty() ? "" : "; " + diagnostic);
    return {std::move(result.bytes), std::move(diagnostic)};
}
} // namespace nx::features
