#include "common/byte_patch.h"
#include "common/sha256.h"
#include "features/acnh_settings.h"
#include "features/embedded_resources.h"
#include "features/legacy_settings.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace nx;
using namespace nx::features;
namespace nx::features {
std::span<const EmbeddedResource> embedded_resources() { return {}; }
} // namespace nx::features
void check(bool value) {
    if (!value)
        throw std::runtime_error("native feature contract failed");
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected);
}
void original(CPU &) {}
void first(CPU &) {}
void second(CPU &) {}
int main() {
    Registry registry;
    rejects([&] { registry.definitions(); });
    auto transform = [](std::string id, int priority, auto patch) {
        Definition feature;
        feature.id = id;
        feature.priority = priority;
        feature.wants_resource = [](std::string_view path) { return path == "Test/data"; };
        feature.patch_resource = patch;
        return feature;
    };
    registry.add(transform("test.second", 20, [](const ResourceRequest &r) {
        check(r.bytes == "stock+A");
        return ResourcePatch{std::string(r.bytes) + "+B", {}};
    }));
    registry.add(transform("test.first", 0,
                           [](const ResourceRequest &r) { return ResourcePatch{std::string(r.bytes) + "+A", {}}; }));
    registry.add(transform("test.rejected", 10, [](const ResourceRequest &) {
        return ResourcePatch{{}, "input belongs to another feature"};
    }));
    rejects([&] {
        Definition duplicate;
        duplicate.id = "test.first";
        registry.add(duplicate);
    });
    rejects([&] {
        Definition bad;
        bad.id = "Bad ID";
        registry.add(bad);
    });
    rejects([&] {
        Definition bad;
        bad.id = "bad";
        bad.wants_resource = [](auto) { return true; };
        registry.add(bad);
    });
    bool observed = false;
    Definition observer;
    observer.id = "test.observer";
    observer.resource_prepared = [&](const ResourceEvent &e) {
        observed = true;
        check(e.bytes == "stock+A+B" && e.changed_by("test.first") && !e.changed_by("test.rejected"));
    };
    registry.add(observer);
    registry.freeze();
    registry.freeze();
    rejects([&] {
        Definition late;
        late.id = "late";
        registry.add(late);
    });
    const std::string source = "stock";
    auto result = registry.patch_resource("Test/data", source);
    check(source == "stock" && result.bytes == "stock+A+B" && result.applied.size() == 2 &&
          result.diagnostics.size() == 1);
    registry.resource_prepared({"Test/data", result.applied, *result.bytes});
    check(observed);
    check(!registry.wants_resource("save/private") && !registry.patch_resource("save/private", source).bytes);
    const auto input_hash = hash::digest("stock"), output_hash = hash::digest("replacement");
    EmbeddedResource replacement{"test", "Test/data", input_hash, output_hash, "replacement"};
    check(checked_replacement(replacement, {"Test/data", "stock"}).bytes == "replacement");
    check(!checked_replacement(replacement, {"Test/data", "replacement"}).bytes);
    auto conflict = checked_replacement(replacement, {"Test/data", "another mod"});
    check(!conflict.bytes && !conflict.diagnostic.empty());
    BytePatch edits(8), disjoint(8), conflicting(8), bad(8);
    check(edits.write<uint16_t>(0, 0x1122) && disjoint.write<uint16_t>(4, 0x3344));
    check(edits.merge(disjoint));
    check(conflicting.write<uint16_t>(0, 0x5566) && conflicting.write<uint16_t>(6, 0x7788));
    check(!edits.merge(conflicting));
    std::array<uint8_t, 8> bytes{};
    check(edits.apply(bytes) && bytes[6] == 0);
    check(edits.merge(conflicting, true) && edits.apply(bytes) && bytes[0] == 0x66 && bytes[6] == 0x88);
    check(!bad.write<uint32_t>(6, 1) && !edits.merge(bad) && !bad.apply(bytes));
    std::array<uint32_t, 8> code{};
    code[3] = 0x12345678;
    ModuleInfo module{};
    module.base = reinterpret_cast<uint64_t>(code.data());
    module.text = 8;
    module.text_size = 16;
    const InstructionWord good[] = {{12, 0x12345678}}, below[] = {{4, 0}}, past[] = {{24, 0}}, wrong[] = {{12, 0}};
    check(instructions_match(module, good) && !instructions_match(module, below) && !instructions_match(module, past) &&
          !instructions_match(module, wrong));
    FuncEntry entries[] = {{12, original}};
    Fn next1 = nullptr, next2 = nullptr;
    check(wrap_function(entries, 12, first, next1) && next1 == original);
    check(wrap_function(entries, 12, second, next2) && next2 == first && entries[0].fn == second);
    check(!wrap_function(entries, 12, first, next1) && entries[0].fn == second);
    constexpr std::string_view legacy[] = {"freecamera", "camera303"};
    auto migrated = migrate_legacy(" speedfix60,freecamera,ptbr,speedfix60,camera303 ", legacy, {}, true, true);
    check(migrated.enabled && migrated.mods == "speedfix60,ptbr");
    check(!migrate_legacy("speedfix60", legacy, {}, true, true).enabled);
    check(!migrate_legacy("freecamera", legacy, false, false, true).enabled);
    check(migrate_legacy("", legacy, {}, false, true).enabled);
    std::map<std::string, std::string> settings{{"camera.soft_lighting", "0"}};
    auto get = [&](const std::string &key) { return settings[key]; };
    auto set = [&](const std::string &key, const std::string &value) { settings[key] = value; };
    check(migrate_acnh_settings(get, set) && settings["camera.reduce_fog"] == "0");
    settings["camera.reduce_fog"] = "1";
    migrate_acnh_settings(get, set);
    check(settings["camera.reduce_fog"] == "1");
    settings.clear();
    migrate_acnh_settings(get, set);
    check(settings["camera.reduce_fog"] == "1");
    std::cout << "Native feature composition, conflicts, migration and hook chaining passed\n";
}
