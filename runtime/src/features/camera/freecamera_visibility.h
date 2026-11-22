#pragma once
#include "freecamera_math.h"
#include <array>

namespace nx::freecamera {
// Expand a native inclusive 10-unit tile rectangle to cover the camera view.
// Retain native coverage: its jobs can include off-screen dependencies, and a
// finite camera distance budget must never trim those away at screen corners.
inline std::array<int32_t,4> expand_surface_tiles(const std::array<int32_t,4>& native,
                                                float x, float z, const Bounds& view) {
    return {std::min(native[0],int32_t(std::floor((x-view.left)/10))),
            std::min(native[1],int32_t(std::floor((z-view.far)/10))),
            std::max(native[2],int32_t(std::ceil((x+view.right)/10))),
            std::max(native[3],int32_t(std::ceil((z+view.near)/10)))};
}
inline bool outdoor_projection(float near_plane, float far_plane, float fov) {
    // Live 3.0.3 field: far=100000, fov=31 degrees with camera303. Do not
    // inherit the temporal renderer's <0.5-radian stock-camera assumption.
    // UI cameras use far=10000; the minimap has a near plane in the thousands.
    return std::isfinite(near_plane) && std::isfinite(far_plane) && std::isfinite(fov) &&
        near_plane>0 && near_plane<1000 && far_plane>=50000 && fov>0 && fov<1.5f;
}
// Conservative broad-phase rectangle of the finite perspective frustum through
// an outdoor height band. The engine's per-model culling remains responsible
// for rejecting geometry inside the rectangle but outside the actual frustum.
inline bool view_bounds(const float* eye, const float* target, float tan_half,
                        float aspect, float offset_x, float offset_y, float reach,
                        float bottom, float top, float margin, Bounds& result) {
    for (int i=0;i<3;++i) if (!std::isfinite(eye[i]) || !std::isfinite(target[i])) return false;
    for (float v : {tan_half,aspect,offset_x,offset_y,reach,bottom,top,margin})
        if (!std::isfinite(v)) return false;
    if (tan_half<=0 || tan_half>2 || aspect<.25f || aspect>8 || reach<20 || reach>2000 ||
        bottom>top || margin<0 || margin>100 || std::fabs(offset_x)>2 || std::fabs(offset_y)>2) return false;
    using V=std::array<float,3>;
    V f{target[0]-eye[0],target[1]-eye[1],target[2]-eye[2]};
    const float distance=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (!(distance>1 && distance<10000)) return false;
    for (float& v:f) v/=distance;
    const float horizontal=std::hypot(f[0],f[2]);
    if (horizontal<.001f) return false;
    const V right{-f[2]/horizontal,0,f[0]/horizontal};
    const V up{-f[1]*right[2], f[0]*right[2]-f[2]*right[0], f[1]*right[0]};
    std::array<V,8> corners;
    for (int depth=0;depth<2;++depth) for (int y=0;y<2;++y) for (int x=0;x<2;++x) {
        const float z=depth?distance+reach:1.f;
        // sead's offsets are fractions of the full frustum width/height.
        const float sx=(2*x-1+2*offset_x)*tan_half*aspect;
        const float sy=(2*y-1+2*offset_y)*tan_half;
        V& p=corners[depth*4+y*2+x];
        for (int axis=0;axis<3;++axis) p[axis]=eye[axis]+z*(f[axis]+sx*right[axis]+sy*up[axis]);
    }
    // Keep the player's immediate neighborhood resident for interactions and
    // a little headroom at screen edges for crowns/animation and streaming.
    float xmin=-20, xmax=20, zmin=-20, zmax=20;
    auto add=[&](const V& p) {
        xmin=std::min(xmin,p[0]-target[0]-margin); xmax=std::max(xmax,p[0]-target[0]+margin);
        zmin=std::min(zmin,p[2]-target[2]-margin); zmax=std::max(zmax,p[2]-target[2]+margin);
    };
    for (const V& p:corners) if (p[1]>=bottom && p[1]<=top) add(p);
    // The clipped convex polyhedron's vertices lie on original edges or at
    // original vertices. Projecting those gives its exact X/Z bounding box.
    for (int i=0;i<8;++i) for (int bit:{1,2,4}) if (!(i&bit)) {
        const V& a=corners[i]; const V& b=corners[i|bit];
        if (std::fabs(b[1]-a[1])<1e-6f) continue;
        for (float plane:{bottom,top}) {
            const float t=(plane-a[1])/(b[1]-a[1]);
            if (t<0 || t>1) continue;
            V p; for(int axis=0;axis<3;++axis) p[axis]=a[axis]+t*(b[axis]-a[axis]);
            add(p);
        }
    }
    result={std::min(-xmin,reach),std::min(xmax,reach),std::min(zmax,reach),std::min(-zmin,reach)};
    return true;
}
}
