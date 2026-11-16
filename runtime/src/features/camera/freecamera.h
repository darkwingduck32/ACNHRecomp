#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace nx {
struct ModuleInfo;
struct FuncEntry;
namespace plat { struct PadState; }
// Opt-in ACNH runtime mod. Called before stick-direction synthesis and the temporal snapshot.
void freecamera_input(plat::PadState& pad);
// Active game menus/dialogue keep their normal, unrotated input.
void freecamera_ui_input();
bool freecamera_field_active();
bool camera_view_active();
bool camera_sky_active();
bool freecamera_world_projection(float near_plane, float far_plane, float fov);
bool camera_background_depth(std::string_view shader);
float freecamera_zoom_depth_offset();
float freecamera_directional_glare_scale(const float* direction, float exponent);
void freecamera_lookat(uint64_t camera, uint64_t caller);
void camera_install_terrain(const ModuleInfo* module, FuncEntry* functions);
void freecamera_install(const ModuleInfo* module, FuncEntry* functions);
}
