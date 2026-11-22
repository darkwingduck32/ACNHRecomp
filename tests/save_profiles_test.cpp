#include "save_archive.h"
#include "save_profiles.h"
#include "save_storage.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
using Files = std::map<std::string, std::string>;

void check(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}

void put(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), std::streamsize(data.size()));
    check(bool(out), "writing fixture failed");
}

std::string get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "reading fixture failed");
    return {std::istreambuf_iterator<char>(in), {}};
}

Files tree(const fs::path& root) {
    Files result;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) result[e.path().lexically_relative(root).generic_string()] = get(e.path());
    return result;
}

void save(const fs::path& root, const std::string& label) {
    put(root / "main.dat", label);
    put(root / "mainHeader.dat", "header");
    put(root / "Villager0/personal.dat", "resident " + label);
    put(root / "Villager0/personalHeader.dat", "personal header");
}

int main(int argc, char** argv) {
    auto root = fs::temp_directory_path() /
                ("acnh-save-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto data = nx::save_storage::path_utf8(root / "game");
        auto active = nx::save_storage::device_dir(data);
        save(active, "current island");
        const auto original = tree(active);
        std::string error;
        check(nx::save_profiles::ensure_migrated(data, &error), error);
        auto id = nx::save_profiles::create_empty_profile(data, &error);
        check(!id.empty(), error);
        auto profile = root / "game/user/profiles" / id / "device_save";
        auto folder = root / "source";
        save(folder, "imported island");
        const auto expected = tree(folder);
        check(nx::save_profiles::import_into_profile(data, id, nx::save_storage::path_utf8(folder), &error), error);
        check(tree(profile) == expected, "folder import lost files");
        check(tree(active) == original, "offline import changed active island");

        auto zip = root / "save.zip";
        check(nx::save_archive::write(folder, zip, &error), error);
        check(nx::save_archive::validate(zip, &error), error);
        check(nx::save_profiles::import_into_profile(data, id, nx::save_storage::path_utf8(zip), &error), error);
        check(tree(profile) == expected, "ZIP import lost files");
        check(nx::save_profiles::activate_profile(data, id, &error), error);
        check(tree(active) == expected, "imported ZIP could not be activated");
        check(tree(root / "game/user/profiles/default/device_save") == original,
              "activation did not preserve original island");

        auto wrapped = root / "wrapped";
        save(wrapped / "backup/island", "wrapped island");
        check(nx::save_archive::write(wrapped, zip, &error), error);
        check(nx::save_profiles::import_into_profile(data, id, nx::save_storage::path_utf8(zip), &error), error);
        const auto wrapped_files = tree(wrapped / "backup/island");
        check(tree(profile) == wrapped_files && tree(active) == wrapped_files,
              "wrapped ZIP import into active profile failed");

        auto reject = [&](const fs::path& bad) {
            error.clear();
            check(!nx::save_profiles::import_into_profile(data, id, nx::save_storage::path_utf8(bad), &error),
                  "invalid archive accepted");
            check(!error.empty(), "missing rejection reason");
            check(tree(profile) == wrapped_files && tree(active) == wrapped_files,
                  "failed import changed the island");
            check(!fs::exists(profile.parent_path() / "device_save.new"), "failed import left staged files");
        };
        auto invalid = root / "invalid";
        put(invalid / "main.dat", "missing headers");
        check(nx::save_archive::write(invalid, zip, &error), error);
        reject(zip);
        auto multiple = root / "multiple";
        save(multiple / "one", "one");
        save(multiple / "two", "two");
        check(nx::save_archive::write(multiple, zip, &error), error);
        reject(zip);

        check(nx::save_archive::write(folder, zip, &error), error);
        auto bytes = get(zip);
        auto at = bytes.find("imported island");
        check(at != std::string::npos, "fixture content missing");
        bytes[at] ^= 1;
        put(zip, bytes);
        reject(zip);  // CRC failure after extraction has begun.

        check(nx::save_archive::write(folder, zip, &error), error);
        bytes = get(zip);
        for (size_t pos = 0; (pos = bytes.find("main.dat", pos)) != std::string::npos; pos += 8)
            bytes.replace(pos, 8, "../x.dat");
        put(zip, bytes);
        reject(zip);
        check(!fs::exists(root / "game/user/profiles" / id / "x.dat"), "archive escaped staging");

        if (argc > 1) {
            auto example_id = nx::save_profiles::create_empty_profile(data, &error);
            check(!example_id.empty(), error);
            check(nx::save_profiles::import_into_profile(data, example_id, argv[1], &error), error);
            check(nx::save_profiles::activate_profile(data, example_id, &error), error);
            check(nx::save_storage::flat_save(active, &error), error);
        }
        fs::remove_all(root);
        std::cout << "Save profile folder/ZIP import, activation, and rejection checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
