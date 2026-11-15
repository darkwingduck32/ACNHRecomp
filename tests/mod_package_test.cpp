#include "features/camera/parameter_overlay.h"
#include "mod_manager.h"
#include "mod_package.h"
#include "package_sha256.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace nx;
void check(bool b, const std::string &message) {
  if (!b)
    throw std::runtime_error(message);
}
void put(fs::path path, const std::string &s) {
  fs::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary);
  f << s;
}
std::string get(fs::path p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::string manifest(const std::string &id, const std::string &data) {
  return "{\"format\":1,\"id\":\"" + id +
         "\",\"version\":\"1\",\"runtime\":\"romfs.v1\",\"build_ids\":["
         "\"FF1D1C05670DB6021C85B624A710B963\"],\"conflicts\":[\"camera303\"],"
         "\"files\":{\"romfs/test\":\"" +
         mod_package::digest(data) + "\"}}";
}

void zip_stored(const fs::path &path,
                const std::map<std::string, std::string> &files) {
  std::string local, central;
  unsigned count = 0;
  auto number = [](std::string &s, uint32_t n, int bytes) {
    for (int i = 0; i < bytes; ++i)
      s += char(n >> (i * 8));
  };
  for (auto &[name, data] : files) {
    uint32_t crc = 0xffffffff;
    for (unsigned char c : data) {
      crc ^= c;
      for (int i = 0; i < 8; ++i)
        crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int(crc & 1)));
    }
    crc ^= 0xffffffff;
    auto offset = local.size();
    number(local, 0x04034b50, 4);
    number(local, 20, 2);
    number(local, 0, 2);
    number(local, 0, 2);
    number(local, 0, 4);
    number(local, crc, 4);
    number(local, data.size(), 4);
    number(local, data.size(), 4);
    number(local, name.size(), 2);
    number(local, 0, 2);
    local += name;
    local += data;
    number(central, 0x02014b50, 4);
    number(central, 20, 2);
    number(central, 20, 2);
    number(central, 0, 2);
    number(central, 0, 2);
    number(central, 0, 4);
    number(central, crc, 4);
    number(central, data.size(), 4);
    number(central, data.size(), 4);
    number(central, name.size(), 2);
    number(central, 0, 2);
    number(central, 0, 2);
    number(central, 0, 2);
    number(central, 0, 2);
    number(central, 0, 4);
    number(central, offset, 4);
    central += name;
    ++count;
  }
  auto end = local.size();
  local += central;
  number(local, 0x06054b50, 4);
  number(local, 0, 4);
  number(local, count, 2);
  number(local, count, 2);
  number(local, central.size(), 4);
  number(local, end, 4);
  number(local, 0, 2);
  put(path, local);
}
int main(int argc, char **argv) {
  auto root =
      fs::temp_directory_path() /
      ("acnh-package-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    check(
        mod_package::digest("") ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "empty SHA-256 vector");
    check(
        mod_package::digest("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "abc SHA-256 vector");
    for (auto s : {"../bad", "a/../bad", "/absolute", "C:/bad", "a//b", "a/./b",
                   "a/b.", "a/b ", "a\\b"})
      check(!mod_package::safe_path(s), "unsafe path accepted");
    auto game = root / "game", pack = root / "package";
    auto data = game.generic_string();
    std::string error, id;
    put(game / "game.ini", "build_id = FF1D1C05670DB6021C85B624A710B963\n");
    put(game / "config.ini", "[game]\nmods = ptbr,speedfix60,camera303\n");
    put(pack / "romfs/test", "first");
    put(pack / "manifest.json", manifest("feature", "first"));
    check(mod_package::validate(pack, data, nullptr, &error), error);
    check(mod_manager::import_mod(data, pack.generic_string(), &id, nullptr,
                                  &error),
          error);
    check(id == "feature", "manifest ID not used");
    auto cfg = get(game / "config.ini");
    check(cfg.find("feature,ptbr,speedfix60") != cfg.npos,
          "native bundled mods were disabled/reordered");
    check(cfg.find("camera303") == cfg.npos, "legacy feature not migrated");
    check(fs::exists(game / "mods/feature/manifest.json"), "manifest dropped");
    put(pack / "romfs/test", "second");
    put(pack / "manifest.json", manifest("feature", "second"));
    check(mod_manager::import_mod(data, pack.generic_string(), &id, nullptr,
                                  &error),
          error);
    check(get(game / "mods/feature/romfs/test") == "second",
          "package not updated in place");
    check(!fs::exists(game / "mods/feature.previous"),
          "backup retained after commit");
    put(pack / "romfs/test", "corrupt");
    check(!mod_manager::import_mod(data, pack.generic_string(), &id, nullptr,
                                   &error),
          "corrupt package accepted");
    check(get(game / "mods/feature/romfs/test") == "second",
          "failed update destroyed installation");
    put(pack / "romfs/test", "second");
    put(pack / "profiles/unlisted.txt", "secret");
    check(!mod_package::validate(pack, data, nullptr, &error),
          "unlisted file accepted");
    fs::remove(pack / "profiles/unlisted.txt");
    put(game / "game.ini", "build_id = 00000000000000000000000000000000\n");
    check(!mod_package::validate(pack, data, nullptr, &error),
          "unsupported game accepted");
    // Parameter patches must preserve independent changes, and fail without
    // partial edits on conflict.
    std::string archive = "SARC" + std::string(60, 'x'), original = archive;
    auto header = mod_package::digest(archive.substr(0, 20));
    std::string profile = "ACNH_CAMERA_PARAMS_V1 64 20 " + header +
                          "\n24 78787878 01020304\n40 78787878 05060708\n";
    archive[32] = 'S';
    std::istringstream input(profile);
    check(camera::apply_parameters(archive, input, error), error);
    check(archive[32] == 'S', "independent mod data overwritten");
    check(archive.substr(24, 4) == std::string("\1\2\3\4", 4),
          "camera edit not applied");
    std::istringstream again(profile);
    auto patched = archive;
    check(camera::apply_parameters(archive, again, error) && archive == patched,
          "patch not idempotent");
    archive = original;
    archive[40] = '!';
    auto conflict = archive;
    std::istringstream bad(profile);
    check(!camera::apply_parameters(archive, bad, error) && archive == conflict,
          "conflicting edit was partly applied");
    put(game / "game.ini", "build_id = FF1D1C05670DB6021C85B624A710B963\n");
    auto json = manifest("zip-feature", "zip data");
    json.insert(json.rfind("}}"), ",\"profiles/test.txt\":\"" +
                                      mod_package::digest("profile") + "\"");
    zip_stored(root / "feature.zip",
               {{"wrapper/manifest.json", json},
                {"wrapper/romfs/test", "zip data"},
                {"wrapper/profiles/test.txt", "profile"}});
    check(mod_manager::import_mod(data, (root / "feature.zip").generic_string(),
                                  &id, nullptr, &error),
          error);
    check(get(game / "mods/zip-feature/profiles/test.txt") == "profile",
          "ZIP import dropped profile");
    zip_stored(root / "unsafe.zip",
               {{"wrapper/romfs/test", "x"}, {"wrapper/../escape", "no"}});
    check(!mod_manager::import_mod(data, (root / "unsafe.zip").generic_string(),
                                   &id, nullptr, &error),
          "ZIP traversal accepted");
    put(pack / "manifest.json", manifest("speedfix60", "second"));
    check(!mod_package::validate(pack, data, nullptr, &error),
          "reserved native package ID accepted");
    if (argc == 4) {
      // Exercise the real generated camera ZIP and the independently supplied
      // native archive.
      check(mod_manager::import_mod(data, argv[1], &id, nullptr, &error),
            error);
      auto source = get(argv[2]), before = source;
      std::ifstream profile(game /
                            "mods/freecamera/profiles/parameter_edits.txt");
      check(camera::apply_parameters(source, profile, error), error);
      auto stock = get(argv[3]);
      for (size_t i = 0; i < before.size(); ++i)
        if (before[i] != stock[i])
          check(source[i] == before[i], "native mod byte overwritten");
      check(!fs::exists(game / "mods/freecamera/romfs/Pack/StaticParam.pack"),
            "camera shipped a replacement parameter archive");
    }
    fs::remove_all(root);
    std::cout << "package import, update, validation, native-mod coexistence "
                 "and parameter conflict tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    fs::remove_all(root);
    return 1;
  }
}
