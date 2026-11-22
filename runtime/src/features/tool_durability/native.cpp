#include "native.h"
#include "config.h"
#include "features/embedded_resources.h"
#include "features/registry.h"
#include "features/runtime.h"
namespace nx::features {
namespace {
bool enabled = false;
}
void register_tool_durability_feature(Registry &registry) {
    Definition feature;
    feature.id = "acnh.tool-durability";
    feature.priority = 10;
    // Latch after legacy settings migration and before game resource loading.
    // The game caches these resources; a live change would mix old/new data.
    feature.configure = [] { enabled = config_get_bool("gameplay.extended_tool_durability", true); };
    feature.restart_required = [](std::string_view key) { return key == "gameplay.extended_tool_durability"; };
    feature.owns_legacy_mod = [](std::string_view id) { return id == "tool_durability"; };
    feature.wants_resource = [](std::string_view path) {
        return enabled && find_resource("acnh.tool-durability", path);
    };
    feature.patch_resource = [](const ResourceRequest &request) {
        return checked_replacement(*find_resource("acnh.tool-durability", request.path), request,&resource_cache());
    };
    registry.add(std::move(feature));
}
} // namespace nx::features
