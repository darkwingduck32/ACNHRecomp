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

// CloudVR's equator is about 525 model units from its axis. Its native
// model pitches the horizon down only in front. Keep that depression all
// around the eye by leveling the basis and lowering its center instead.
inline bool cloud_horizon_model(float* model) {
    for (int i=0;i<12;++i) if (!std::isfinite(model[i])) return false;
    const float scale=std::sqrt(model[0]*model[0]+model[4]*model[4]+model[8]*model[8]);
    if (scale<.1f || scale>4.f) return false;
    const float square=scale*scale;
    for (int a=0;a<3;++a) for (int b=a;b<3;++b) {
        float dot=0;
        for (int row=0;row<3;++row) dot+=model[row*4+a]*model[row*4+b];
        if (std::fabs(dot-(a==b?square:0))>.01f*square) return false;
    }
    const float determinant=model[0]*(model[5]*model[10]-model[6]*model[9])-
        model[1]*(model[4]*model[10]-model[6]*model[8])+model[2]*(model[4]*model[9]-model[5]*model[8]);
    if (determinant<.99f*scale*square || model[5]<.866f*scale) return false;
    const float horizontal=std::hypot(model[0],model[8]);
    if (horizontal<.5f*scale) return false;
    const float x=model[0]/horizontal*scale, z=model[8]/horizontal*scale;
    const float depression=525.18f*std::hypot(model[4],model[6]);
    model[0]=x; model[1]=0; model[2]=-z;
    model[4]=0; model[5]=scale; model[6]=0; model[7]-=depression;
    model[8]=z; model[9]=0; model[10]=x;
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
