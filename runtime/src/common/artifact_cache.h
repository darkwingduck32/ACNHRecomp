#pragma once
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
namespace nx {
// Content-addressed disk artifacts. Every read is validated; failed persistence
// still returns a verified in-memory result. No source files are ever rewritten.
class ArtifactCache {
  public:
    struct Result {
        std::optional<std::string> bytes;
        bool hit = false;
        std::string diagnostic;
    };
    explicit ArtifactCache(std::filesystem::path root) : root_(std::move(root)) {}
    Result get(std::string_view key, const std::function<bool(std::string_view)> &valid,
               const std::function<std::optional<std::string>()> &build);

  private:
    std::filesystem::path root_;
    std::mutex mutex_;
};
} // namespace nx
