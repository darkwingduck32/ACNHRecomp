#include "camera/native.h"
#include "nvn/nvn_extensions.h"
#include "registry.h"
#include "tool_durability/native.h"
#include <nx/game.h>
namespace nx::features {
void register_builtins(Registry &registry) {
    if (std::string_view(game::kInfo.id) == "acnh") {
        register_camera_feature(registry);
        register_tool_durability_feature(registry);
    }
    // Register every feature's render adapters before replay threads start.
    nvn::replay::freeze_uniform_extensions();
}
} // namespace nx::features
