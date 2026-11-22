#include "common/resource_delta.h"
#include "common/sha256.h"
#include "features/embedded_resources.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace nx;
using namespace nx::features;
std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Missing checked source resource");
    return {std::istreambuf_iterator<char>(file), {}};
}
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    const std::filesystem::path game = std::filesystem::u8path(argv[1]), cache_root = std::filesystem::u8path(argv[2]);
    ArtifactCache cache(cache_root);
    int models = 0, resources = 0;
    uint64_t payload = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto &resource : embedded_resources()) {
        if (resource.encoding == ResourceEncoding::Literal) {
            if (resource.path.starts_with("Model/"))
                throw std::runtime_error("Full model embedded");
            continue;
        }
        if (resource.encoding == ResourceEncoding::ZstdDelta)
            ++models;
        ++resources;
        payload += resource.bytes.size();
        const auto source = read(game / "romfs" / std::filesystem::u8path(resource.path));
        const auto original_hash = hash::digest(source);
        auto result = checked_replacement(resource, {resource.path, source}, &cache);
        if (!result.bytes || !resource_matches(resource, *result.bytes))
            throw std::runtime_error("Native reconstruction failed: " + std::string(resource.path) + ": " +
                                     result.diagnostic);
        if (hash::digest(source) != original_hash)
            throw std::runtime_error("Source modified");
        const auto folder = resource.feature == "acnh.camera" ? "freecamera" : "tool_durability";
        auto expected = read(game / "mods" / folder / "romfs" / std::filesystem::u8path(resource.path));
        if (resource.encoding == ResourceEncoding::ZstdDelta) {
            std::string decoded, error;
            if (!resource_delta::decompress(expected, decoded, error))
                throw std::runtime_error(error);
            expected = std::move(decoded);
        }
        if (hash::digest(expected) != resource.content_sha256)
            throw std::runtime_error("Reviewed content mismatch");
        // A conflicting third-party input must not be replaced, even on a hit.
        auto conflict = checked_replacement(resource, {resource.path, "another resource owner"}, &cache);
        if (conflict.bytes)
            throw std::runtime_error("Conflicting mod was overwritten");
    }
    if (models != 138 || resources != 140)
        throw std::runtime_error("Incomplete native delta catalog");
    std::cout << resources << " verified deltas, " << models << " models, " << payload << " embedded patch bytes; "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " seconds\n";
}
