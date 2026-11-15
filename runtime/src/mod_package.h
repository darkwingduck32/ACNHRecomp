#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace nx::mod_package {
inline constexpr const char *camera_capability = "acnh.camera.v1";
struct Manifest {
  std::string id, version, runtime;
  std::vector<std::string> builds, conflicts;
  std::map<std::string, std::string> files;
};
// Strict v1 schema. Missing manifests are legacy RomFS mods, not feature
// packages.
bool read(const std::filesystem::path &root, Manifest &out, std::string *error);
bool validate(const std::filesystem::path &root, const std::string &data_root,
              Manifest *out, std::string *error);
bool safe_path(const std::string &path);
std::string build_id(const std::string &data_root);
} // namespace nx::mod_package
