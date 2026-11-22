#include "config.h"
#include "features/acnh_settings.h"
#include "mod_package.h"
#include "native.h"
#include "platform.h"
#include <filesystem>
#include <nx/game.h>
#include <nx/runtime.h>
#include <set>
#include <sstream>
namespace nx {
namespace {
std::set<std::string, std::less<>> legacy_ids{"freecamera", "camera303"};
}
void camera_migrate_legacy_settings() {
    if (std::string_view(game::kInfo.id) != "acnh")
        return;
    std::istringstream csv(config_get("game.mods"));
    for (std::string id; std::getline(csv, id, ',');) {
        const auto first = id.find_first_not_of(" \t"), last = id.find_last_not_of(" \t");
        if (first == id.npos)
            continue;
        id = id.substr(first, last - first + 1);
        if (!mod_package::safe_path(id) || id.find('/') != id.npos)
            continue;
        mod_package::Manifest manifest;
        if (mod_package::read(std::filesystem::u8path(plat::data_root()) / "mods" / id, manifest, nullptr) &&
            manifest.runtime == mod_package::camera_capability)
            legacy_ids.insert(id);
    }
    std::vector<std::string_view> aliases;
    for (const auto &id : legacy_ids)
        aliases.push_back(id);
    if (features::migrate_acnh_settings(
            [](const std::string &key) { return config_get(key); },
            [](const std::string &key, const std::string &value) { config_set(key, value); }, aliases)) {
        config_save();
        log("native camera: migrated legacy choices; external native mods retained");
    }
}
bool camera_owns_legacy_mod(std::string_view id) {
    return legacy_ids.find(id) != legacy_ids.end();
}
} // namespace nx
