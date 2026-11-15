#include "parameter_overlay.h"
#include "package.h"
#include "platform.h"
#include <fstream>
#include <nx/runtime.h>
namespace nx {
FILE *camera_parameter_overlay(FILE *source, int64_t offset, int64_t size) {
  std::ifstream input(camera_profile_path("parameter_edits.txt"));
  if (!input)
    return nullptr; // legacy packages supply their own RomFS replacement
  if (size < 20 || size > 16 * 1024 * 1024)
    return nullptr;
  std::string data(size, '\0'), error;
  if (plat::read_at(source, offset, data.data(), data.size()) != data.size() ||
      !camera::apply_parameters(data, input, error)) {
    log("camera: parameter adaptation refused (%s); source left unchanged",
        error.c_str());
    camera_package_disable();
    return nullptr;
  }
  FILE *patched = std::tmpfile();
  if (!patched ||
      std::fwrite(data.data(), 1, data.size(), patched) != data.size() ||
      std::fflush(patched)) {
    if (patched)
      std::fclose(patched);
    camera_package_disable();
    return nullptr;
  }
  return patched;
}
} // namespace nx
