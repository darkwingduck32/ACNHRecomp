#include "common/artifact_cache.h"
#include "common/resource_delta.h"
#include "common/sha256.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace nx;
void check(bool value) {
    if (!value)
        throw std::runtime_error("Resource delta/cache contract failed");
}
void word(std::string &bytes, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        bytes += char(value >> (8 * i));
}
std::string patch(std::string body) {
    std::string compressed, error;
    check(resource_delta::compress(body, compressed, error));
    return "NXDP0001" + compressed;
}
int main() {
    std::string body;
    for (auto value : {6ull, 48ull, 4ull, 2ull, 2ull, 1ull, 1ull, 2ull, 1ull, 0ull})
        word(body, value);
    body.append("\x01\x01\0\0", 4);
    body += "X!";
    const auto delta = patch(body);
    std::string output = "unchanged", error;
    check(resource_delta::apply("abcdef", delta, output, error) && output == "bcXde!");
    // Validation failure must never publish a partial output.
    for (const auto &invalid :
         {std::string("wrong"), delta.substr(0, delta.size() - 2), delta + "hidden", patch("tiny")}) {
        output = "unchanged";
        check(!resource_delta::apply("abcdef", invalid, output, error) && output == "unchanged");
    }
    std::string bad = body;
    bad[0] = 127;
    output = "unchanged";
    check(!resource_delta::apply("abcdef", patch(bad), output, error) && output == "unchanged");
    bad = body;
    for (int i = 0; i < 8; ++i)
        bad[32 + i] = char(255);
    check(!resource_delta::apply("abcdef", patch(bad), output, error));
    std::string compressed, decoded;
    check(resource_delta::compress("test", compressed, error));
    check(resource_delta::decompress(compressed, decoded, error) && decoded == "test");
    check(!resource_delta::decompress(compressed, decoded, error, 3));
    const auto temporary_parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto root = temporary_parent /
                      ("nx-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(root));
    ArtifactCache cache(root);
    const auto key = hash::digest("resource-version-1"), next_key = hash::digest("resource-version-2");
    std::filesystem::remove(root / (key + ".bin"));
    std::filesystem::remove(root / (next_key + ".bin"));
    int builds = 0;
    auto valid = [](std::string_view bytes) { return bytes == "verified"; };
    auto build = [&]() -> std::optional<std::string> {
        ++builds;
        return "verified";
    };
    auto first = cache.get(key, valid, build);
    check(first.bytes == "verified" && !first.hit && builds == 1);
    auto second = cache.get(key, valid, build);
    check(second.bytes == "verified" && second.hit && builds == 1);
    {
        std::ofstream broken(root / (key + ".bin"), std::ios::binary);
        broken << "truncated";
    }
    auto repaired = cache.get(key, valid, build);
    check(repaired.bytes == "verified" && !repaired.hit && builds == 2);
    check(cache.get(next_key, valid, build).bytes == "verified" && builds == 3);
    check(!cache.get("../bad", valid, build).bytes && builds == 3);
    const auto blocked = root / "blocked";
    {
        std::ofstream file(blocked);
        file << "file";
    }
    ArtifactCache memory_only(blocked);
    auto fallback = memory_only.get(key, valid, build);
    check(fallback.bytes == "verified" && !fallback.diagnostic.empty());
    check(
        !cache.get(hash::digest("invalid"), valid, []() -> std::optional<std::string> { return "unverified"; }).bytes);
    for (const auto &entry : std::filesystem::directory_iterator(root))
        check(entry.path().extension() != ".tmp");
    // Only this test's deterministic, checked temporary directory is removed.
    check(std::filesystem::weakly_canonical(root).parent_path() == temporary_parent &&
          root.filename().string().starts_with("nx-cache-test-"));
    std::filesystem::remove_all(root);
    std::cout << "Delta bounds, corruption, cache reuse/repair/invalidation and unwritable-cache fallback passed\n";
}
