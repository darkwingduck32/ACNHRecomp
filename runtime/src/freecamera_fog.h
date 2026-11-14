#pragma once
#include <cmath>
#include <string>

namespace nx {
void freecamera_fog_install(bool enabled);
bool freecamera_fog_shader(const std::string& hash);
namespace freecamera {
// The native dark-fog equation is saturate(dot(viewPosition, plane.xyz)
// * inverseRange + plane.w)^2 * intensity. Retain its ramp and weather;
// replace the world-facing plane normal with camera-forward depth.
inline bool view_fog_normal(const float* plane, float intensity, float inverse_range, float* normal) {
    for (int i=0;i<4;++i) if (!std::isfinite(plane[i])) return false;
    if (!std::isfinite(intensity) || intensity<=0 || !std::isfinite(inverse_range) || inverse_range<=0) return false;
    const float length=std::sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
    if (length<.001f || length>100) return false;
    normal[0]=normal[1]=0; normal[2]=-length;
    return plane[0]!=normal[0] || plane[1]!=normal[1] || plane[2]!=normal[2];
}
}
}
