#pragma once
#include <cmath>

namespace nx::freecamera {
// Replace the native forward tilt with the same depression around the horizon.
// ParkVR's native radius is 2000; vertical_scale comes from its model matrix.
inline bool sky_horizon_height(float tilt, float height, float vertical_scale, float& out) {
    const float radius = 2000.f * vertical_scale;
    if (!std::isfinite(tilt) || std::fabs(tilt) > .5f || !std::isfinite(height) ||
        !std::isfinite(radius) || radius < 100.f || radius > 4000.f) return false;
    const float result = height + radius * std::fabs(std::sin(tilt));
    if (!std::isfinite(result)) return false;
    out = result;
    return true;
}

// ParkVR is a full sphere. Its model placement follows the unmodified camera,
// while its view matrix receives the orbit. Recover the current eye from the
// rigid world-to-view matrix so the background encloses that eye again.
inline bool sky_camera_center(const float* view, float* eye) {
    for (int i = 0; i < 12; ++i) if (!std::isfinite(view[i])) return false;
    for (int row = 0; row < 3; ++row)
        for (int other = row; other < 3; ++other) {
            float dot = 0;
            for (int k = 0; k < 3; ++k) dot += view[4*row+k]*view[4*other+k];
            if (std::fabs(dot - (row == other ? 1.f : 0.f)) > .01f) return false;
        }
    for (int k = 0; k < 3; ++k) {
        eye[k] = -(view[k]*view[3] + view[4+k]*view[7] + view[8+k]*view[11]);
        if (!std::isfinite(eye[k])) return false;
    }
    return true;
}
}
