#include "runtime.h"
#include "platform.h"
#include <nx/runtime.h>
namespace nx::features {
ArtifactCache& resource_cache() {
    static ArtifactCache cache(std::filesystem::u8path(plat::data_root())/"cache"/"native-resources"/"v1");
    return cache;
}
void register_builtins(Registry &);
Registry &registry() {
    static Registry value = [] {
        Registry r;
        register_builtins(r);
        r.freeze();
        return r;
    }();
    return value;
}
void configure() {
    for (const auto &f : registry().definitions())
        if (f.configure)
            f.configure();
}
void module_loaded(const ModuleInfo &m, FuncEntry *entries) {
    for (const auto &f : registry().definitions())
        if (f.module_loaded)
            f.module_loaded(m, entries);
}
void input(plat::PadState &pad) {
    for (const auto &f : registry().definitions())
        if (f.input)
            f.input(pad);
}
void game_ui_input() {
    for (const auto &f : registry().definitions())
        if (f.game_ui_input)
            f.game_ui_input();
}
void camera_update(uint64_t object, uint64_t caller) {
    for (const auto &f : registry().definitions())
        if (f.camera_update)
            f.camera_update(object, caller);
}
bool world_projection(float n, float z, float fov) {
    for (const auto &f : registry().definitions())
        if (f.world_projection && f.world_projection(n, z, fov))
            return true;
    return false;
}
bool background_depth(std::string_view shader) {
    for (const auto &f : registry().definitions())
        if (f.background_depth && f.background_depth(shader))
            return true;
    return false;
}
bool owns_legacy_mod(std::string_view id) {
    for (const auto &f : registry().definitions())
        if (f.owns_legacy_mod && f.owns_legacy_mod(id))
            return true;
    return false;
}
bool restart_required(std::string_view key) {
    for (const auto &f : registry().definitions())
        if (f.restart_required && f.restart_required(key))
            return true;
    return false;
}
FILE *transform_resource(std::string_view path, FILE *source, int64_t offset, int64_t size,
                         std::vector<std::string> &applied) {
    if (!registry().wants_resource(path))
        return nullptr;
    if (size < 0) {
        const auto position = plat::ftell64(source);
        if (position < 0 || plat::fseek64(source, 0, SEEK_END))
            return nullptr;
        size = plat::ftell64(source);
        plat::fseek64(source, position, SEEK_SET);
    }
    if (offset < 0 || size < 0 || uint64_t(size) > Registry::max_resource_size)
        return nullptr;
    std::string bytes(size, '\0');
    if (plat::read_at(source, offset, bytes.data(), bytes.size()) != bytes.size())
        return nullptr;
    auto result = registry().patch_resource(path, bytes);
    for (const auto &diagnostic : result.diagnostics)
        log("native resource: %s", diagnostic.c_str());
    if (!result.bytes) {
        registry().resource_prepared({path, {}, bytes});
        return nullptr;
    }
    FILE *output = std::tmpfile();
    if (!output)
        return nullptr;
    if (std::fwrite(result.bytes->data(), 1, result.bytes->size(), output) != result.bytes->size() ||
        std::fflush(output)) {
        std::fclose(output);
        return nullptr;
    }
    applied = std::move(result.applied);
    registry().resource_prepared({path, applied, *result.bytes});
    return output;
}
void resource_opened(std::string_view path) {
    for (const auto &f : registry().definitions())
        if (f.resource_opened)
            f.resource_opened(path);
}
} // namespace nx::features
