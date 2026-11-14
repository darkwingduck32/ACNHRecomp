#include "freecamera.h"
#include "freecamera_ao.h"
#include "platform.h"
#include <nx/runtime.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace nx {
namespace {
bool installed=false;
uint64_t main_base=0;
thread_local bool skip_instance=false;
bool requested() {
    static const bool value=[] { const char* e=std::getenv("NX_FREECAMERA_AO_CULL"); return e && std::strcmp(e,"1")==0; }();
    return value;
}

struct Bound { float p[3]{},radius=-1; uint64_t frame=~uint64_t(0); };
struct Pool { std::atomic<uint64_t> manager{0}; std::array<Bound,40000> bounds; };
inline std::array<Pool,4> pools;
struct Map { bool valid=false,on=false; float plane[4][4]{},norm[4]{},shift=0; uint64_t frame=~uint64_t(0); };
inline thread_local Map map;
inline bool heap(uint64_t p) { return p>=0x5000000000ull && p<0x5200000000ull; }
inline Pool* pool(uint64_t manager,bool create) {
    for(auto& p:pools) {
        auto key=p.manager.load(std::memory_order_relaxed);
        if(key==manager) return &p;
        if(create && !key) {
            if(p.manager.compare_exchange_strong(key,manager)) return &p;
            // Another enqueue worker may have just registered this same manager.
            if(key==manager) return &p;
        }
    }
    return nullptr;
}
inline int slot(uint64_t manager,uint64_t record) {
    const auto base=nx::ld<uint64_t>(manager+0x518);
    if(record<base || (record-base)%0x38) return -1;
    const auto i=(record-base)/0x38;
    return i<40000 && i<nx::ld<uint32_t>(manager+0x510)?int(i):-1;
}
inline void record(uint64_t manager,uint64_t instance,uint64_t model,uint64_t matrix) {
    const int index=slot(manager,instance);
    if(index<0) return;
    Pool* cache=pool(manager,true); if(!cache) return;
    Bound b; b.frame=g_frame.load(std::memory_order_relaxed);
    if(heap(model) && matrix) {
        const auto resource=nx::ld<uint64_t>(model+0x18);
        const auto sphere=heap(resource)?nx::ld<uint64_t>(resource+0x38):0;
        if(heap(sphere)) {
            float norm2=0;
            for(unsigned row=0;row<3;++row) {
                b.p[row]=nx::ld<float>(matrix+row*16+12);
                for(unsigned col=0;col<3;++col) {
                    const float m=nx::ld<float>(matrix+row*16+col*4);
                    b.p[row]+=m*nx::ld<float>(sphere+col*4); norm2+=m*m;
                }
            }
            // Frobenius norm conservatively bounds any affine scale/shear.
            b.radius=nx::ld<float>(sphere+12)*std::sqrt(norm2)+20;
            for(float x:b.p) if(!std::isfinite(x)) b.radius=-1;
            if(!std::isfinite(b.radius) || b.radius<20 || b.radius>2000) b.radius=-1;
        }
    }
    cache->bounds[index]=b;
}
inline void begin(uint64_t context) {
    map.valid=false;
    map.frame=g_frame.load(std::memory_order_relaxed);
    if(!freecamera_field_active()) return;
    static const bool ab=std::getenv("NX_FREECAMERA_AO_AB")!=nullptr;
    map.on=!ab || (map.frame/600)%2!=0;
    float view[3][4],proj[4][4],combined[4][4]{};
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<4;++c) view[r][c]=nx::ld<float>(context+0x6db0+16*r+4*c);
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) {
        const float f=nx::ld<float>(context+0x6e70+16*r+4*c);
        if(!std::isfinite(f)) return;
        proj[r][c]=f;
    }
    if(proj[3][0]!=0 || proj[3][1]!=0 || proj[3][2]!=0 || proj[3][3]!=1) return;
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) {
        combined[r][c]=c==3?proj[r][3]:0;
        for(unsigned k=0;k<3;++k) combined[r][c]+=proj[r][k]*view[k][c];
        if(!std::isfinite(combined[r][c])) return;
    }
    for(unsigned p=0;p<4;++p) {
        float norm2=0;
        for(unsigned c=0;c<4;++c) {
            map.plane[p][c]=combined[3][c]+(p%2?-1:1)*combined[p/2][c];
            if(c<3) norm2+=map.plane[p][c]*map.plane[p][c];
        }
        map.norm[p]=std::sqrt(norm2);
        if(!(map.norm[p]>1e-7f)) return;
    }
    const auto scene=nx::ld<uint64_t>(nx::ld<uint64_t>(main_base+0x4ab2200));
    const auto drum=scene?nx::ld<uint64_t>(scene+0x78):0;
    map.shift=drum?nx::ld<float>(drum+0x38)*nx::ld<float>(drum+0x3c):0;
    map.valid=std::isfinite(map.shift);
}
inline bool outside(uint64_t manager,uint64_t instance) {
    if(!map.valid || !map.on || map.frame!=g_frame.load(std::memory_order_relaxed)) return false;
    const auto* cache=pool(manager,false); const int index=slot(manager,instance);
    if(!cache || index<0) return false;
    const auto& b=cache->bounds[index];
    if(b.radius<0 || b.frame>map.frame || map.frame-b.frame>1) return false;
    for(unsigned i=0;i<4;++i) {
        const float* p=map.plane[i];
        const float distance=p[0]*b.p[0]+p[1]*b.p[1]+p[2]*(b.p[2]+map.shift)+p[3];
        if(distance+b.radius*map.norm[i]<0) return true;
    }
    return false;
}

// Both hooks only change temporary x8: the native branch advances to the next
// instance without touching shared flags, list links, or other render passes.
void prepare_skip(CPU& c) {
    skip_instance=false;
    if(!installed || ld<uint64_t>(c.x[29]-0x18)!=12) return;
    skip_instance=outside(ld<uint64_t>(c.x[29]-0x10),c.x[19]);
    if(skip_instance) c.x[8]=1;
}
void apply_skip(CPU& c) { if(skip_instance) { c.x[8]=0; skip_instance=false; } }
void capture_bound(CPU& c) {
    if(installed) record(c.x[24],c.x[8],c.x[1],ld<uint64_t>(c.x[29]-8));
}
void capture_map(CPU& c) { if(installed) begin(c.x[25]); }
NX_HOOK(0x146ac2c,capture_bound);
NX_HOOK(0x1571a70,capture_map);
NX_HOOK(0x146b1d4,prepare_skip);
NX_HOOK(0x146b1dc,apply_skip);
}
void freecamera_ao_install(const ModuleInfo* module,bool enabled) {
    if(!enabled || !requested() || module->text_size<=0x1571a74) return;
    const std::pair<uint64_t,uint32_t> words[]={
        {0x146ac2c,0xf90007e8},{0x1571a70,0x3ddb6f04},
        {0x146b1d4,0x34000068},{0x146b1dc,0x34fffd28}};
    for(const auto& [offset,word]:words)
        if(ld<uint32_t>(module->base+offset)!=word) return;
    main_base=module->base;
    installed=true;
    log("freecamera: experimental world AO map culling enabled");
}
}
