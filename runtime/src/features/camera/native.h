#pragma once
#include <string_view>
namespace nx {
namespace features {
class Registry;
}
void register_camera_feature(features::Registry &);
bool camera_enabled();
int camera_lens_degrees(); // applied startup lens, excluding pending settings
void camera_disable();
bool camera_models_active(); // immutable resource choice for this process
bool camera_models_need_reload();
bool camera_models_compatible();
std::string_view camera_profile(std::string_view name);
void camera_migrate_legacy_settings();
bool camera_owns_legacy_mod(std::string_view id);
} // namespace nx
