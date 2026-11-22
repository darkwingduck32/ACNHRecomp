#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nx {
struct ModuleInfo;
struct FuncEntry;
namespace plat {
struct PadState;
}
namespace features {
// Source-level extension contracts, not a stable binary/plugin ABI. Register
// before freeze(); callbacks then run synchronously on the caller's thread.
struct ResourceRequest {
    std::string_view path;  // normalized path relative to the read-only RomFS
    std::string_view bytes; // immutable output of preceding accepted transforms
};
struct ResourcePatch {
    std::optional<std::string> bytes;
    std::string diagnostic;
};
struct ResourceEvent {
    std::string_view path;
    std::span<const std::string> applied;
    std::string_view bytes; // final prepared bytes; borrowed only during callback
    bool changed_by(std::string_view id) const;
};
struct Definition {
    std::string id;
    int priority = 0; // lower first; equal priorities sort by id
    std::function<void()> configure;
    std::function<void(const ModuleInfo &, FuncEntry *)> module_loaded;
    std::function<void(plat::PadState &)> input;
    std::function<void()> game_ui_input;
    std::function<void(uint64_t, uint64_t)> camera_update;
    std::function<bool(float, float, float)> world_projection;
    std::function<bool(std::string_view)> background_depth;
    std::function<bool(std::string_view)> wants_resource;
    std::function<ResourcePatch(const ResourceRequest &)> patch_resource;
    std::function<void(const ResourceEvent &)> resource_prepared;
    std::function<void(std::string_view)> resource_opened;
    std::function<bool(std::string_view)> owns_legacy_mod;
    std::function<bool(std::string_view)> restart_required;
};
struct ResourceResult {
    std::optional<std::string> bytes;
    std::vector<std::string> applied;
    std::vector<std::string> diagnostics;
};
class Registry {
  public:
    void add(Definition definition);
    void freeze();
    std::span<const Definition> definitions() const;
    bool wants_resource(std::string_view path) const;
    // Each transform is atomic: rejection preserves earlier accepted output.
    // Replacements can validate input hashes; transforms may compose explicitly.
    ResourceResult patch_resource(std::string_view path, std::string_view bytes) const;
    void resource_prepared(const ResourceEvent &) const;
    static constexpr size_t max_resource_size = 128 * 1024 * 1024;

  private:
    bool frozen_ = false;
    std::vector<Definition> definitions_;
};
} // namespace features
} // namespace nx
