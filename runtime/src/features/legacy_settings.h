#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace nx::features {
struct LegacyMigration {
    bool enabled;
    std::string mods;
};
inline LegacyMigration migrate_legacy(std::string_view csv, std::span<const std::string_view> owned,
                                      std::optional<bool> choice, bool previously_applied, bool fallback) {
    bool selected = false;
    std::vector<std::string> retained;
    while (!csv.empty()) {
        const auto end = csv.find(',');
        auto id = csv.substr(0, end);
        const auto first = id.find_first_not_of(" \t\r\n"), last = id.find_last_not_of(" \t\r\n");
        if (first != id.npos) {
            id = id.substr(first, last - first + 1);
            if (std::find(owned.begin(), owned.end(), id) != owned.end())
                selected = true;
            else if (std::find(retained.begin(), retained.end(), id) == retained.end())
                retained.emplace_back(id);
        }
        if (end == csv.npos)
            break;
        csv.remove_prefix(end + 1);
    }
    LegacyMigration result{choice.value_or(selected || (!previously_applied && fallback)), {}};
    for (const auto &id : retained)
        result.mods += (result.mods.empty() ? "" : ",") + id;
    return result;
}
} // namespace nx::features
