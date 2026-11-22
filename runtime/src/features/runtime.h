#pragma once
#include "registry.h"
#include <cstdio>
#include "common/artifact_cache.h"
namespace nx::features {
Registry &registry();
ArtifactCache& resource_cache();
void configure();
void module_loaded(const ModuleInfo &, FuncEntry *);
void input(plat::PadState &);
void game_ui_input();
void camera_update(uint64_t object, uint64_t caller);
bool world_projection(float near_plane, float far_plane, float fov);
bool background_depth(std::string_view shader);
bool owns_legacy_mod(std::string_view id);
bool restart_required(std::string_view key);
// The filesystem only calls this for read-only RomFS files, after LayeredFS.
// A returned temporary FILE is owned by the caller; input slices stay untouched.
FILE *transform_resource(std::string_view path, FILE *source, int64_t offset, int64_t size,
                         std::vector<std::string> &applied);
void resource_opened(std::string_view path);
} // namespace nx::features
