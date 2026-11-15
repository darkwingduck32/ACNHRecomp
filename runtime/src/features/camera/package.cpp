#include "package.h"
#include "config.h"
#include "mod_manager.h"
#include "mod_package.h"
#include "platform.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nx/game.h>
#include <nx/runtime.h>
#include <sstream>
namespace nx {
namespace {
std::atomic<bool> rejected{false};
struct Package {
  bool enabled = false;
  std::string root;
  bool legacy = false;
};
const Package &package() {
  static const Package p = [] {
    Package result;
    if (std::string(game::kInfo.id) != "acnh")
      return result;
    const char *env = std::getenv("NX_MODS");
    std::istringstream input(env ? env : config_get("game.mods"));
    for (std::string id; std::getline(input, id, ',');) {
      auto a = id.find_first_not_of(" \t"), b = id.find_last_not_of(" \t");
      if (a == id.npos)
        continue;
      id = id.substr(a, b - a + 1);
      if (!mod_package::safe_path(id) || id.find('/') != id.npos)
        continue;
      auto root = std::filesystem::u8path(plat::data_root()) / "mods" / id;
      if (std::filesystem::exists(root / "manifest.json")) {
        mod_package::Manifest m;
        std::string error;
        if (!mod_package::validate(root, plat::data_root(), &m, &error)) {
          log("camera package: %s", error.c_str());
          continue;
        }
        if (m.runtime != mod_package::camera_capability)
          continue;
        result = {true, root.generic_string(), false};
        return result;
      }
      if (id == "freecamera")
        result = {true, root.generic_string(), true};
    }
    return result;
  }();
  return p;
}
} // namespace
// Applied once per feature, so a later release never re-enables a user's
// disabled mod.
void camera_package_defaults() {
  if (std::getenv("NX_MODS"))
    return;
  std::ifstream defaults(std::filesystem::u8path(plat::data_root()) /
                         "mods/defaults/enabled.txt");
  for (std::string id; std::getline(defaults, id);) {
    if (!id.empty() && id.back() == '\r')
      id.pop_back();
    if (id != "freecamera" && id != "tool_durability")
      continue;
    const std::string key = "features.default_" + id;
    if (config_get(key) == "applied")
      continue;
    mod_package::Manifest m;
    std::string error;
    auto root = std::filesystem::u8path(plat::data_root()) / "mods" / id;
    if (!mod_package::validate(root, plat::data_root(), &m, &error) ||
        m.id != id)
      continue;
    std::vector<std::string> enabled;
    std::istringstream csv(config_get("game.mods"));
    for (std::string name; std::getline(csv, name, ',');) {
      auto a = name.find_first_not_of(" \t"), b = name.find_last_not_of(" \t");
      if (a == name.npos)
        continue;
      name = name.substr(a, b - a + 1);
      if (name != id && std::find(m.conflicts.begin(), m.conflicts.end(),
                                  name) == m.conflicts.end())
        enabled.push_back(name);
    }
    enabled.insert(enabled.begin(), id);
    if (!mod_manager::write_enabled(plat::data_root(), enabled, &error)) {
      log("feature defaults: %s", error.c_str());
      continue;
    }
    config_load();
    config_set(key, "applied");
    config_save();
  }
}
bool camera_package_enabled() {
  return !rejected.load(std::memory_order_relaxed) && package().enabled;
}
void camera_package_disable() {
  rejected.store(true, std::memory_order_relaxed);
}
bool camera_parameter_file(const std::string &guest) {
  auto colon = guest.find(':');
  return colon != guest.npos &&
         guest.substr(colon + 1) == "/Pack/StaticParam.pack" &&
         camera_package_enabled();
}
std::string camera_profile_path(const char *name) {
  const auto &p = package();
  return p.root + (p.legacy ? "/" : "/profiles/") + name;
}
} // namespace nx
