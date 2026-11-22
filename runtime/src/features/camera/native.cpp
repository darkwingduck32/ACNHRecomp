#include "native.h"
#include "common/sha256.h"
#include "config.h"
#include "features/embedded_resources.h"
#include "features/runtime.h"
#include "freecamera.h"
#include "freecamera_fruit.h"
#include "freecamera_fruit_layout.h"
#include "parameter_overlay.h"
#include "uniform_adapter.h"
#include <atomic>
#include <mutex>
#include <nx/game.h>
#include <nx/runtime.h>
#include <sstream>
namespace nx {
namespace {
std::atomic<bool> rejected{false};
int initial_fov_degrees = 31;
std::atomic<int> resource_compatibility{0}; // unknown, compatible, incompatible
constexpr std::string_view owner = "acnh.camera";
constexpr std::string_view size_table = "System/Resource/ResourceSizeTable.srsizetable";
} // namespace
bool camera_enabled() {
    if (std::string_view(game::kInfo.id) != "acnh" || rejected.load(std::memory_order_relaxed))
        return false;
    // Generation caches are per thread: no lock on draw/input hot paths.
    thread_local uint64_t generation = ~uint64_t(0);
    thread_local bool enabled = true;
    const auto current = config_generation();
    if (current != generation) {
        enabled = config_get_bool("camera.enabled", true);
        generation = current;
    }
    return enabled;
}
void camera_disable() { rejected.store(true, std::memory_order_relaxed); }
int camera_lens_degrees() { return initial_fov_degrees; }
bool camera_models_active() {
    static const bool active = camera_enabled() && config_get_bool("camera.replace_models", true) &&
                               features::find_resource(owner, size_table);
    return active;
}
bool camera_models_need_reload() {
    const bool desired = camera_enabled() && config_get_bool("camera.replace_models", true) &&
                         features::find_resource(owner, size_table);
    return desired != camera_models_active();
}
bool camera_models_compatible() { return resource_compatibility.load(std::memory_order_relaxed) != -1; }
std::string_view camera_profile(std::string_view name) {
    const auto *resource = features::find_resource("acnh.camera.profile", name);
    return resource ? resource->bytes : std::string_view{};
}
void register_camera_feature(features::Registry &registry) {
    nvn::replay::register_camera_uniform_extension();
    features::Definition feature;
    feature.id = owner;
    feature.configure = [] {
        camera_migrate_legacy_settings();
        initial_fov_degrees = config_get_int("camera.fov_degrees", 31);
        (void)camera_models_active();
    };
    feature.module_loaded = [](const ModuleInfo &module, FuncEntry *entries) {
        if (std::string_view(game::kInfo.id) != "acnh")
            return;
        camera_install_terrain(&module, entries);
        freecamera_install(&module, entries);
    };
    feature.input = freecamera_input;
    feature.game_ui_input = freecamera_ui_input;
    feature.camera_update = freecamera_lookat;
    feature.world_projection = freecamera_world_projection;
    feature.background_depth = camera_background_depth;
    feature.owns_legacy_mod = camera_owns_legacy_mod;
    feature.restart_required = [](std::string_view key) {
        return key == "camera.fov_degrees" ||
               ((key == "camera.enabled" || key == "camera.replace_models") && camera_models_need_reload());
    };
    feature.wants_resource = [](std::string_view path) {
        return std::string_view(game::kInfo.id) == "acnh" &&
               (path == "Pack/StaticParam.pack" || (camera_models_active() && features::find_resource(owner, path)));
    };
    feature.patch_resource = [](const features::ResourceRequest &request) -> features::ResourcePatch {
        if (request.path == "Pack/StaticParam.pack") {
            std::string bytes(request.bytes), error;
            std::istringstream edits{std::string(camera_profile("parameter_edits.txt"))};
            // Native camera defaults own these values; no replacement archive
            // or camera303 folder participates in LayeredFS anymore.
            if ((edits.peek() != std::char_traits<char>::eof() && !camera::apply_parameters(bytes, edits, error)) ||
                !camera::apply_lens(bytes, initial_fov_degrees, error)) {
                camera_disable();
                return {{}, error};
            }
            return {std::move(bytes), {}};
        }
        if (request.path != size_table && resource_compatibility.load(std::memory_order_relaxed) != 1)
            return {};
        // Tree geometry and its attachment table must change together. A
        // different native table owner/version keeps its original resources.
        if ((freecamera::fruit_variant(std::string(request.path)) >= 0 ||
             request.path.ends_with("/PltTreePalm.Nin_NX_NVN.zs")) &&
            !freecamera_fruit_ready())
            return {};
        const auto *resource = features::find_resource(owner, request.path);
        if (!resource)
            return {};
        return features::checked_replacement(*resource, request,&features::resource_cache());
    };
    feature.resource_prepared = [](const features::ResourceEvent &event) {
        const auto *resource = features::find_resource(owner, event.path);
        if (!resource)
            return;
        const bool native = features::resource_matches(*resource,event.bytes);
        if (event.path == size_table) {
            resource_compatibility.store(native ? 1 : -1, std::memory_order_relaxed);
            log("native camera: model allocation table %s",
                native ? "accepted" : "owned by another resource override; native replacements bypassed");
        } else {
            freecamera_fruit_resource(std::string(event.path), native);
        }
    };
    registry.add(std::move(feature));
}
} // namespace nx
