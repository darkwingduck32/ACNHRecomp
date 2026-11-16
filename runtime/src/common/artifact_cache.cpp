#include "artifact_cache.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
namespace nx {
namespace {
constexpr uint64_t maximum = 128 * 1024 * 1024;
std::optional<std::string> read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    if (size < 0 || uint64_t(size) > maximum)
        return {};
    std::string bytes(size_t(size), '\0');
    file.seekg(0);
    if (!file.read(bytes.data(), std::streamsize(bytes.size())))
        return {};
    return bytes;
}
} // namespace
ArtifactCache::Result ArtifactCache::get(std::string_view key, const std::function<bool(std::string_view)> &valid,
                                         const std::function<std::optional<std::string>()> &build) {
    if (key.size() != 64 || key.find_first_not_of("0123456789abcdef") != key.npos)
        return {{}, false, "Invalid artifact cache key"};
    std::lock_guard guard(mutex_);
    const auto target = root_ / (std::string(key) + ".bin");
    if (auto bytes = read(target); bytes && valid(*bytes))
        return {std::move(bytes), true, {}};
    auto bytes = build();
    if (!bytes || bytes->size() > maximum || !valid(*bytes))
        return {{}, false, "Artifact reconstruction failed validation"};
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec)
        return {std::move(bytes), false, "Artifact cache is not writable; using memory"};
    static std::atomic<uint64_t> counter{0};
    static const auto nonce = std::to_string(std::random_device{}()) + "-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temporary = root_ / (std::string(key) + ".tmp-" + nonce + "-" + std::to_string(counter++));
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(bytes->data(), std::streamsize(bytes->size()));
        file.close();
        if (!file) {
            std::filesystem::remove(temporary, ec);
            return {std::move(bytes), false, "Artifact cache write failed; using memory"};
        }
    }
    // Same-directory rename publishes the complete artifact atomically. Other
    // processes may produce the same content independently; both are verified.
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return {std::move(bytes), false, "Artifact cache publish failed; using memory"};
    }
    return {std::move(bytes), false, {}};
}
} // namespace nx
