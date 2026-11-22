#pragma once
#include <cmath>
#include <string>

namespace nx {
void freecamera_fog_install(bool enabled);
bool freecamera_fog_shader(const std::string& hash, bool fragment=false);
namespace freecamera {
// Park UBER's directional glint is pow(dot(viewRay,direction),exponent).
// Below 45 degrees, cap its centre-view response to the same azimuth at
// 45 degrees. This avoids the low-angle white wash without disabling glints.
inline float directional_glare_scale(const float* ray, const float* direction, float exponent) {
    float norm=0, dot=0;
    for (int i=0;i<3;++i) {
        if (!std::isfinite(ray[i]) || !std::isfinite(direction[i])) return 1;
        norm+=ray[i]*ray[i]; dot+=ray[i]*direction[i];
    }
    if (std::abs(norm-1)> .001f || ray[1]>0 || ray[1]<=-.70710678f || dot<=0 ||
        !std::isfinite(exponent) || exponent<=0 || exponent>128) return 1;
    const float horizontal=std::sqrt(.5f/(1-ray[1]*ray[1]));
    const float reference=(ray[0]*direction[0]+ray[2]*direction[2])*horizontal-.70710678f*direction[1];
    const float current=std::fmin(dot,1.f);
    if (reference>=current) return 1;
    return std::pow(std::fmax(reference,0.f)/current,exponent);
}
// Park UBER's fragment scattering uses clamp(viewDepth-start,0,range).
// Translate its onset by the added eye-to-player distance, retaining the
// native range, weather colour and scattering intensity.
inline bool scattering_start(float start, float range, float offset, float& adapted) {
    if (!std::isfinite(start) || !std::isfinite(range) || range<=0 ||
        !std::isfinite(offset) || std::abs(offset)>10000) return false;
    adapted=start+offset;
    return std::isfinite(adapted) && adapted!=start;
}
// The native dark-fog equation is saturate(dot(viewPosition, plane.xyz)
// * inverseRange + plane.w)^2 * intensity. Retain its ramp and weather;
// replace the world-facing plane normal with camera-forward depth. Anchor the
// ramp to the player when zoom changes the camera-to-player distance.
inline bool view_fog_plane(const float* plane, float intensity, float inverse_range,
                           float zoom_depth_offset, float* adapted) {
    for (int i=0;i<4;++i) if (!std::isfinite(plane[i])) return false;
    if (!std::isfinite(intensity) || intensity<=0 || !std::isfinite(inverse_range) || inverse_range<=0) return false;
    if (!std::isfinite(zoom_depth_offset) || std::abs(zoom_depth_offset)>10000) return false;
    const float length=std::sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
    if (length<.001f || length>100) return false;
    const float bias=plane[3]-zoom_depth_offset*length*inverse_range;
    if (!std::isfinite(bias)) return false;
    adapted[0]=adapted[1]=0; adapted[2]=-length; adapted[3]=bias;
    return plane[0]!=adapted[0] || plane[1]!=adapted[1] || plane[2]!=adapted[2] || plane[3]!=bias;
}
}
}
