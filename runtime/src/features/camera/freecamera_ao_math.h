#pragma once
#include <cmath>

namespace nx::freecamera {
struct AoSphere {
    float p[3]{}, radius=-1;
};

inline AoSphere ao_transform_sphere(const float* sphere, const float* matrix, float margin) {
    AoSphere result;
    for(unsigned i=0;i<4;++i) if(!std::isfinite(sphere[i])) return result;
    if(sphere[3]<0 || !std::isfinite(margin) || margin<0) return result;
    float norm2=0;
    for(unsigned row=0;row<3;++row) {
        result.p[row]=matrix[row*4+3];
        for(unsigned col=0;col<3;++col) {
            const float m=matrix[row*4+col];
            result.p[row]+=m*sphere[col];
            norm2+=m*m;
        }
        if(!std::isfinite(result.p[row])) return AoSphere{};
    }
    // The Frobenius norm bounds the largest affine scale, including shear.
    const float radius=sphere[3]*std::sqrt(norm2)+margin;
    if(std::isfinite(radius) && radius>=margin && radius<=2000) result.radius=radius;
    return result;
}

struct AoClip {
    float plane[4][4]{}, norm[4]{};
    bool valid=false;

    bool set(const float* view, const float* projection) {
        valid=false;
        for(unsigned i=0;i<12;++i) if(!std::isfinite(view[i])) return false;
        for(unsigned i=0;i<16;++i) if(!std::isfinite(projection[i])) return false;
        if(projection[12]!=0 || projection[13]!=0 || projection[14]!=0 || projection[15]!=1) return false;
        float combined[4][4]{};
        for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) {
            combined[r][c]=c==3?projection[r*4+3]:0;
            for(unsigned k=0;k<3;++k) combined[r][c]+=projection[r*4+k]*view[k*4+c];
            if(!std::isfinite(combined[r][c])) return false;
        }
        for(unsigned side=0;side<4;++side) {
            float norm2=0;
            for(unsigned c=0;c<4;++c) {
                plane[side][c]=combined[3][c]+(side%2?-1:1)*combined[side/2][c];
                if(c<3) norm2+=plane[side][c]*plane[side][c];
            }
            norm[side]=std::sqrt(norm2);
            if(!std::isfinite(norm[side]) || !(norm[side]>1e-7f)) return false;
        }
        return valid=true;
    }

    bool outside(const AoSphere& sphere, float z_shift) const {
        if(!valid || !std::isfinite(sphere.radius) || sphere.radius<0 || !std::isfinite(z_shift)) return false;
        for(float x:sphere.p) if(!std::isfinite(x)) return false;
        for(unsigned side=0;side<4;++side) {
            const float* p=plane[side];
            const float distance=p[0]*sphere.p[0]+p[1]*sphere.p[1]+p[2]*(sphere.p[2]+z_shift)+p[3];
            // Retain touching spheres, allowing for roundoff at the clip edge.
            if(distance+(sphere.radius+.001f)*norm[side]<0) return true;
        }
        return false;
    }
};
}
