#pragma once
#include "package_sha256.h"
#include <cstring>
#include <set>
#include <sstream>
namespace nx::camera {
// Operates on the already-resolved archive, never on the independently managed
// mod.
inline bool apply_parameters(std::string &archive, std::istream &profile,
                             std::string &error) {
  std::string magic, sum;
  uint64_t size = 0, header = 0;
  if (!(profile >> magic >> size >> header >> sum) ||
      magic != "ACNH_CAMERA_PARAMS_V1" || size != archive.size() ||
      header < 20 || header > size || archive.compare(0, 4, "SARC") ||
      mod_package::digest(archive.substr(0, header)) != sum) {
    error = "parameter archive layout mismatch";
    return false;
  }
  struct Edit {
    size_t offset;
    std::string bytes;
  };
  std::vector<Edit> edits;
  std::set<uint64_t> occupied;
  auto hex = [](const std::string &s, std::string &out) {
    if (s.size() != 8)
      return false;
    for (size_t i = 0; i < 8; i += 2) {
      auto a = std::string("0123456789abcdef").find(s[i]),
           b = std::string("0123456789abcdef").find(s[i + 1]);
      if (a == std::string::npos || b == std::string::npos)
        return false;
      out += char((a << 4) | b);
    }
    return true;
  };
  for (;;) {
    profile >> std::ws;
    if (profile.eof())
      break;
    uint64_t at;
    std::string before, after, old, new_bytes;
    if (!(profile >> at >> before >> after) || !hex(before, old) ||
        !hex(after, new_bytes) || at < header || at > size - 4) {
      error = "invalid camera parameter edit";
      return false;
    }
    for (uint64_t i = at; i < at + 4; ++i)
      if (!occupied.insert(i).second) {
        error = "overlapping camera parameter edits";
        return false;
      }
    auto actual = archive.substr(at, 4);
    if (actual != old && actual != new_bytes) {
      error = "another mod changed a camera parameter";
      return false;
    }
    edits.push_back({size_t(at), new_bytes});
    if (edits.size() > 10000) {
      error = "too many parameter edits";
      return false;
    }
  }
  if (edits.empty()) {
    error = "empty camera parameter profile";
    return false;
  }
  for (auto &e : edits)
    archive.replace(e.offset, 4, e.bytes);
  return true;
}
} // namespace nx::camera
