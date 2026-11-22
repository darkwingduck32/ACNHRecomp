#pragma once
#include "legacy_settings.h"
namespace nx::features {
// Shared by the launcher and runtime; migration retains unrelated/native mods.
template <class Get, class Set>
bool migrate_acnh_settings(Get get, Set set, std::span<const std::string_view> extra_camera_ids = {}) {
    bool changed = false;
    auto put = [&](const std::string &key, const std::string &value) {
        if (get(key) != value) {
            set(key, value);
            changed = true;
        }
    };
    auto choice = [&](const char *key) -> std::optional<bool> {
        const auto value = get(key);
        if (value.empty())
            return {};
        return value == "1" || value == "true" || value == "on" || value == "yes";
    };
    std::vector<std::string_view> camera_ids{"freecamera", "camera303"};
    camera_ids.insert(camera_ids.end(), extra_camera_ids.begin(), extra_camera_ids.end());
    auto camera = migrate_legacy(get("game.mods"), camera_ids, choice("camera.enabled"),
                                 get("features.default_freecamera") == "applied", true);
    const std::string_view tool_ids[]{"tool_durability"};
    auto tools = migrate_legacy(camera.mods, tool_ids, choice("gameplay.extended_tool_durability"),
                                get("features.default_tool_durability") == "applied", true);
    put("game.mods", tools.mods);
    put("camera.enabled", camera.enabled ? "1" : "0");
    put("gameplay.extended_tool_durability", tools.enabled ? "1" : "0");
    // Earlier previews combined haze/dark-fog reduction under softer lighting.
    // Preserve a saved preference; new installations default to reduced fog.
    put("camera.reduce_fog",
        choice("camera.reduce_fog").value_or(choice("camera.soft_lighting").value_or(true)) ? "1" : "0");
    return changed;
}
} // namespace nx::features
