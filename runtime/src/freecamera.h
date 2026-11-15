#pragma once
#include <cstdint>
namespace nx {
struct ModuleInfo;
struct FuncEntry;
namespace plat { struct PadState; }
// Opt-in ACNH runtime mod. Called before stick-direction synthesis and the temporal snapshot.
void freecamera_input(plat::PadState& pad);
// Active game menus/dialogue keep their normal, unrotated input.
void freecamera_ui_input();
bool freecamera_field_active();
float freecamera_zoom_depth_offset();
float freecamera_directional_glare_scale(const float* direction, float exponent);
void freecamera_lookat(uint64_t camera, uint64_t caller);
void freecamera_install(const ModuleInfo* module, FuncEntry* functions);
}
