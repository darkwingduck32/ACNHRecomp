#pragma once
#include <string>
namespace nx {
struct ModuleInfo;
void freecamera_fruit_install(const ModuleInfo* module, bool enabled);
void freecamera_fruit_resource(const std::string& guest_path);
}
