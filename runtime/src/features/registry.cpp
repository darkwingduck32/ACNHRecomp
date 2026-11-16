#include "registry.h"
#include <algorithm>
#include <stdexcept>
namespace nx::features {
bool ResourceEvent::changed_by(std::string_view id) const {
    return std::find(applied.begin(), applied.end(), id) != applied.end();
}
void Registry::add(Definition definition) {
    if (frozen_)
        throw std::logic_error("Feature registration is closed");
    if (definition.id.empty() ||
        definition.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != definition.id.npos)
        throw std::invalid_argument("Invalid native feature id");
    if (std::any_of(definitions_.begin(), definitions_.end(),
                    [&](const auto &item) { return item.id == definition.id; }))
        throw std::invalid_argument("Duplicate native feature id: " + definition.id);
    if (bool(definition.wants_resource) != bool(definition.patch_resource))
        throw std::invalid_argument("Resource interest and transform must be registered together");
    definitions_.push_back(std::move(definition));
}
void Registry::freeze() {
    if (frozen_)
        return;
    std::sort(definitions_.begin(), definitions_.end(), [](const auto &a, const auto &b) {
        return a.priority != b.priority ? a.priority < b.priority : a.id < b.id;
    });
    frozen_ = true;
}
std::span<const Definition> Registry::definitions() const {
    if (!frozen_)
        throw std::logic_error("Freeze native features before dispatch");
    return definitions_;
}
bool Registry::wants_resource(std::string_view path) const {
    for (const auto &item : definitions())
        if (item.wants_resource && item.wants_resource(path))
            return true;
    return false;
}
ResourceResult Registry::patch_resource(std::string_view path, std::string_view input) const {
    ResourceResult result;
    if (input.size() > max_resource_size) {
        result.diagnostics.push_back("Resource exceeds native transform limit");
        return result;
    }
    for (const auto &item : definitions()) {
        if (!item.wants_resource || !item.wants_resource(path))
            continue;
        const std::string_view current = result.bytes ? std::string_view(*result.bytes) : input;
        auto patch = item.patch_resource({path, current});
        if (!patch.diagnostic.empty())
            result.diagnostics.push_back(item.id + ": " + patch.diagnostic);
        if (!patch.bytes)
            continue;
        if (patch.bytes->size() > max_resource_size) {
            result.diagnostics.push_back(item.id + ": output exceeds native transform limit");
            continue;
        }
        if (*patch.bytes == current)
            continue;
        result.bytes = std::move(patch.bytes);
        result.applied.push_back(item.id);
    }
    return result;
}
void Registry::resource_prepared(const ResourceEvent &event) const {
    for (const auto &item : definitions())
        if (item.resource_prepared)
            item.resource_prepared(event);
}
} // namespace nx::features
