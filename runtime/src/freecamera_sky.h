#pragma once
#include <cmath>

namespace nx::freecamera {
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
