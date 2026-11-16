#pragma once
#include "freecamera_math.h"
#include <string_view>
namespace nx::freecamera {
// Shared UI ranges and defaults. Zoom changes distance; FOV is the vertical lens angle.
struct IntOption {
    const char *key;
    const char *label;
    const char *description;
    int min, max, fallback;
    const char *format;
};
inline constexpr IntOption kCameraOptions[] = {
    {"camera.fov_degrees", "camera.fov", "camera.fov.desc", 20, 80, 31, "%d deg"},
    {"camera.reset_zoom_pct", "camera.reset_zoom", "camera.reset_zoom.desc", 25, 300, 100, "%d%%"},
};
inline constexpr IntOption kOrbitOptions[] = {
    {"camera.orbit_speed_pct", "camera.orbit_speed", "camera.orbit_speed.desc", 25, 300, 100, "%d%%"},
    {"camera.zoom_speed_pct", "camera.zoom_speed", "camera.zoom_speed.desc", 25, 300, 100, "%d%%"},
};
inline bool resource_override_allowed(bool camera_source, bool replace_models, std::string_view path) {
    return !camera_source || replace_models ||
        (path.rfind("Model/", 0) != 0 && path != "System/Resource/ResourceSizeTable.srsizetable");
}
struct Preferences {
    float fov = 31 * pi / 180;
    float reset_zoom = 1, orbit_speed = 1, zoom_speed = 1;
    bool invert_vertical = false;
};
inline Preferences preferences(int fov, int zoom, int orbit_speed, int zoom_speed, bool invert) {
    return {std::clamp(fov, 20, 80) * pi / 180, std::clamp(zoom, 25, 300) / 100.f,
            std::clamp(orbit_speed, 25, 300) / 100.f, std::clamp(zoom_speed, 25, 300) / 100.f, invert};
}
} // namespace nx::freecamera
