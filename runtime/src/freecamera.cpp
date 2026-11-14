#include "freecamera.h"
#include "freecamera_math.h"
#include "freecamera_visibility.h"
#include "freecamera_fruit.h"
#include "freecamera_fog.h"
#include "config.h"
#include "kernel.h"
#include "platform.h"
#include <nx/game.h>
#include <nx/runtime.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <array>
#include <vector>
#include <unordered_map>

namespace nx {
namespace {
std::mutex lock;
freecamera::Orbit orbit;
freecamera::Controls controls;
Fn original_lookat = nullptr;
Fn original_float_storage = nullptr, original_terrain = nullptr;
Fn original_projection = nullptr;
thread_local bool wrapped_lookat = false;
// Direct compiled calls bypass lookat_scoped's dispatch wrapper. The leaf
// LookAt routine's return hook restores their inputs after writing its matrix.
thread_local uint64_t direct_lookat_camera = 0;
thread_local std::array<float,9> direct_lookat_source{};
std::atomic<uint64_t> field_frame{~uint64_t(0)};
std::atomic<bool> field_enabled{false};
std::atomic<float> zoom_depth_offset{0};
// The look-at call sites are shared with interiors. Only the island's
// populated terrain window supplies this heartbeat; it stops inside rooms.
// Resource loads cannot substitute for this: island models remain cached
// across room transitions, so a file-open gate fails to re-enable on exit.
std::atomic<uint64_t> terrain_frame{~uint64_t(0)};
bool terrain_active() {
    const auto seen=terrain_frame.load(std::memory_order_relaxed), now=g_frame.load(std::memory_order_relaxed);
    return seen!=~uint64_t(0) && now>=seen && now-seen<=2;
}
std::atomic<uint64_t> ui_frame{~uint64_t(0)};
float field_x = 0, field_z = 0;
std::array<float,3> field_eye{}, field_target{};
bool field_view_valid = false;
struct Projection {
    float tan_half, aspect, offset_x, offset_y;
    uint64_t frame, updates;
};
std::unordered_map<uint64_t,Projection> projections;
bool perspective_bounds(float reach, freecamera::Bounds& bounds) {
    if (!field_view_valid) return false;
    const uint64_t now=g_frame.load(std::memory_order_relaxed);
    const Projection* best=nullptr;
    for (const auto& [address,p]:projections)
        if (p.frame<=now && now-p.frame<=2 && (!best || p.updates>best->updates)) best=&p;
    if (!best) return false;
    const bool valid=freecamera::view_bounds(field_eye.data(),field_target.data(),best->tan_half,best->aspect,
        best->offset_x,best->offset_y,reach,field_target[1]-48,field_target[1]+72,20,bounds);
    static const bool trace=std::getenv("NX_FREECAMERA_TRACE")!=nullptr;
    static uint64_t next_trace=0;
    if (valid && trace && now>=next_trace) {
        next_trace=now+120;
        log("freecamera: perspective tan/aspect %.3f %.3f reach %.1f eye %.1f %.1f %.1f target %.1f %.1f %.1f bounds %.1f %.1f %.1f %.1f",
            double(best->tan_half),double(best->aspect),double(reach),double(field_eye[0]),double(field_eye[1]),double(field_eye[2]),
            double(field_target[0]),double(field_target[1]),double(field_target[2]),double(bounds.left),double(bounds.right),double(bounds.near),double(bounds.far));
    }
    return valid;
}
void projection_update(CPU& cpu) {
    const uint64_t object=cpu.x[0], caller=cpu.x[30]-kModuleBase;
    original_projection(cpu); // includes the existing aspect-ratio/jitter hook
    if (caller==0x376d214 || caller==0x376d224) return; // minimap
    const VmInfo vm=vm_query(object);
    if (!(vm.perm&1) || object<vm.addr || vm.size<0xbc || object-vm.addr>vm.size-0xbc) return;
    const float far=ld<float>(object+0x9c), fov=ld<float>(object+0xa0);
    if (!freecamera::outdoor_projection(ld<float>(object+0x98),far,fov)) return;
    std::lock_guard guard(lock);
    const uint64_t now=g_frame.load(std::memory_order_relaxed);
    if (projections.size()>32) projections.clear();
    auto& p=projections[object];
    const uint64_t updates=(p.frame<=now && now-p.frame<=2)?p.updates+1:1;
    p={ld<float>(object+0xac),ld<float>(object+0xb0),ld<float>(object+0xb4),ld<float>(object+0xb8),now,updates};
}
struct Visibility {
    uint64_t group;
    int family;
    std::array<uint64_t, 4> cells{};
    std::array<float, 4> base{}, last{};
    bool initialized = false;
};
std::vector<Visibility> visibility;
struct Terrain { std::array<int32_t, 4> base, last; };
std::unordered_map<uint64_t, Terrain> terrain;
bool enabled() {
    if (std::strcmp(game::kInfo.id, "acnh")) return false;
    const char* env = std::getenv("NX_MODS");
    const std::string csv = env ? env : config_get("game.mods");
    for (size_t begin = 0; begin < csv.size();) {
        size_t end = csv.find(',', begin);
        std::string id = csv.substr(begin, end == std::string::npos ? end : end - begin);
        const size_t first = id.find_first_not_of(" \t"), last = id.find_last_not_of(" \t");
        if (first != std::string::npos && id.substr(first, last - first + 1) == "freecamera") return true;
        begin = end == std::string::npos ? csv.size() : end + 1;
    }
    return false;
}
float axis(int32_t v) {
    float x = std::clamp(float(v) / plat::kStickMax, -1.0f, 1.0f);
    return std::fabs(x) < 0.15f ? 0 : std::copysign((std::fabs(x) - 0.15f) / 0.85f, x);
}
void float_storage(CPU& cpu) {
    const uint64_t node = cpu.x[0];
    original_float_storage(cpu);
    if (node < kHeapBase || node + 0x30 > kHeapBase + heap_size()) return;
    const uint64_t name = ld<uint64_t>(node + 8);
    if (name < kModuleBase || name >= kModuleBase + 0x4b57930 - 64) return;
    const char* text = reinterpret_cast<const char*>(name);
    static constexpr const char* names[5][4] = {
        {"mAreaLeft", "mAreaRight", "mAreaNear", "mAreaFar"},
        {"mTreeAreaLeft", "mTreeAreaRight", "mTreeAreaNear", "mTreeAreaFar"},
        {"mCameraRangeLeft", "mCameraRangeRight", "mCameraRangeNear", "mCameraRangeFar"},
        {"mCreateRangeLeft", "mCreateRangeRight", "mCreateRangeNear", "mCreateRangeFar"},
        {"mCreateRangeLeftExtension", "mCreateRangeRightExtension", "mCreateRangeNearExtension", "mCreateRangeFarExtension"},
    };
    int family = -1, side = -1;
    for (int f = 0; f < 5; f++) for (int s = 0; s < 4; s++)
        if (!std::strcmp(text, names[f][s])) family = f, side = s;
    if (family < 0) return;
    const uint64_t group = ld<uint64_t>(node + 0x18);
    std::lock_guard guard(lock);
    auto it = std::find_if(visibility.begin(), visibility.end(), [&](const Visibility& v) { return v.group == group && v.family == family; });
    if (it == visibility.end()) {
        visibility.push_back(Visibility{group, family});
        it = visibility.end()-1;
    }
    it->cells[side] = cpu.x[0]; // verified getter returns the live value at node+0x24
}
void update_visibility(bool on) {
    for (Visibility& v : visibility) {
        if (std::any_of(v.cells.begin(), v.cells.end(), [](uint64_t a) { return a == 0; })) continue;
        bool valid = true;
        for (int i = 0; i < 4; i++) {
            const uint64_t cell = v.cells[i], node = cell-0x24;
            if (node < kHeapBase || cell+4 > kHeapBase+heap_size() || ld<uint64_t>(node+0x18) != v.group) { valid = false; break; }
            float value = ld<float>(v.cells[i]);
            if (!std::isfinite(value) || value < 0 || value > 10000) { valid = false; break; }
            // BYML reloads or another mod's changes replace our last value.
            // Use those as the new source rather than compounding rotations.
            if (!v.initialized || value != v.last[i]) v.base[i] = value;
        }
        if (!valid) continue;
        freecamera::Bounds b{v.base[0], v.base[1], v.base[2], v.base[3]};
        if (on) {
            const float reach=std::max({b.left,b.right,b.near,b.far});
            // Extensions are padding, not an independently visible region.
            if (v.family==4 || !perspective_bounds(reach,b)) b = freecamera::rotate_bounds(b, orbit.yaw);
        }
        const float values[] = {b.left, b.right, b.near, b.far};
        if (!v.initialized && on) log("freecamera: visibility family %d registered: L/R/N/F %.1f %.1f %.1f %.1f", v.family,
            double(v.base[0]), double(v.base[1]), double(v.base[2]), double(v.base[3]));
        for (int i = 0; i < 4; i++) {
            if (ld<float>(v.cells[i]) != values[i]) st<float>(v.cells[i], values[i]);
            v.last[i] = values[i];
        }
        v.initialized = true;
    }
}
void terrain_update(CPU& cpu) {
    const uint64_t object = cpu.x[0];
    original_terrain(cpu); // includes camera303's existing scale when enabled
    const VmInfo vm = vm_query(object);
    if (!(vm.perm & 2) || object < vm.addr || vm.size < 0x1a40 || object-vm.addr > vm.size-0x1a40) return;
    std::lock_guard guard(lock);
    if (!enabled()) return;
    auto* lo = reinterpret_cast<int32_t*>(object+0x1a28);
    auto* hi = reinterpret_cast<int32_t*>(object+0x1a38);
    std::array<int32_t, 4> current{lo[0], lo[1], hi[0], hi[1]};
    auto found = terrain.find(object);
    if (found != terrain.end() && current == found->second.last) current = found->second.base;
    const int64_t w = int64_t(current[2])-current[0]+1, d = int64_t(current[3])-current[1]+1;
    if (w < 18 || w > 76 || d < 6 || d > 28) return;
    const uint64_t frame=g_frame.load(std::memory_order_relaxed);
    terrain_frame.store(frame,std::memory_order_relaxed);
    static const bool trace=std::getenv("NX_FREECAMERA_TRACE")!=nullptr;
    static uint64_t next_trace=0;
    if (trace && frame>=next_trace) {
        next_trace=frame+120;
        log("freecamera: active terrain %lldx%lld at frame %llu",(long long)w,(long long)d,(unsigned long long)frame);
    }
    if (field_frame.load() == ~uint64_t(0)) return;
    // Cover the same height-band footprint as foreground/furniture. This uses
    // camera pitch, zoom, aspect and yaw, not a scaled stock terrain rectangle.
    float reach=0;
    for (const Visibility& v:visibility) if (v.initialized)
        reach=std::max(reach,*std::max_element(v.base.begin(),v.base.end()));
    freecamera::Bounds footprint;
    if (reach>=20 && perspective_bounds(reach,footprint)) {
        if (terrain.size()>16) terrain.clear();
        const std::array<int32_t,4> result{
            int32_t(std::floor((field_x-footprint.left)/10)),int32_t(std::floor((field_z-footprint.far)/10)),
            int32_t(std::ceil((field_x+footprint.right)/10)),int32_t(std::ceil((field_z+footprint.near)/10))};
        terrain[object]=Terrain{current,result};
        lo[0]=result[0];lo[1]=result[1];hi[0]=result[2];hi[1]=result[3];
        return;
    }
    if (std::abs(orbit.yaw) < .00001f) {
        lo[0] = current[0]; lo[1] = current[1]; hi[0] = current[2]; hi[1] = current[3];
        terrain[object] = Terrain{current, current};
        return;
    }
    // Field terrain tiles are 10 world units. Rotate around the field target,
    // retaining the source rectangle's forward offset, not just its center.
    const double px = field_x/10.0, pz = field_z/10.0;
    const double c = freecamera::snap_axis(std::cos(orbit.yaw)), s = freecamera::snap_axis(std::sin(orbit.yaw));
    double xmin = 1e30, xmax = -1e30, zmin = 1e30, zmax = -1e30;
    for (double x : {double(current[0]), double(current[2])+1}) for (double z : {double(current[1]), double(current[3])+1}) {
        const double rx = px+c*(x-px)+s*(z-pz), rz = pz-s*(x-px)+c*(z-pz);
        xmin = std::min(xmin, rx); xmax = std::max(xmax, rx);
        zmin = std::min(zmin, rz); zmax = std::max(zmax, rz);
    }
    if (xmin < INT32_MIN || xmax > INT32_MAX || zmin < INT32_MIN || zmax > INT32_MAX) return;
    const std::array<int32_t, 4> result{int32_t(std::floor(xmin+1e-6)), int32_t(std::floor(zmin+1e-6)), int32_t(std::ceil(xmax-1e-6))-1, int32_t(std::ceil(zmax-1e-6))-1};
    if (terrain.size() > 16) terrain.clear();
    terrain[object] = Terrain{current, result};
    static bool logged = false;
    if (!logged) {
        log("freecamera: terrain footprint %lldx%lld -> %lldx%lld at yaw %.1f", (long long)w, (long long)d,
            (long long)result[2]-result[0]+1, (long long)result[3]-result[1]+1, double(orbit.yaw*180/freecamera::pi));
        logged = true;
    }
    lo[0] = result[0]; lo[1] = result[1]; hi[0] = result[2]; hi[1] = result[3];
}
// This precedes both the CPU instance array and GPU buffer allocations.
// Increasing only the live capacity would write beyond those buffers.
void terrain_instance_capacity(CPU& cpu) {
    if (!enabled() || cpu.x[8]!=10000) return;
    cpu.x[8]=40000;
    log("freecamera: terrain instance capacity 10000 -> 40000 before allocation");
}
NX_HOOK(0x1462bb8, terrain_instance_capacity);
void land_bounds_update(CPU& cpu) {
    if (!freecamera_field_active()) return;
    const uint64_t object=cpu.x[19];
    const VmInfo vm=vm_query(object);
    if ((vm.perm&3)!=3 || object<vm.addr || vm.size<0xf8 || object-vm.addr>vm.size-0xf8) return;
    auto* lo=reinterpret_cast<int32_t*>(object+0xe0);
    auto* hi=reinterpret_cast<int32_t*>(object+0xf0);
    const int64_t w=int64_t(hi[0])-lo[0]+1, d=int64_t(hi[1])-lo[1]+1;
    if (w<20 || w>50 || d<20 || d>70) return;
    std::lock_guard guard(lock);
    if (orbit.identity()) return;
    float reach=0;
    for (const Visibility& v:visibility) if (v.initialized)
        reach=std::max(reach,*std::max_element(v.base.begin(),v.base.end()));
    freecamera::Bounds bounds;
    if (!perspective_bounds(reach,bounds)) return;
    static const bool trace=std::getenv("NX_FREECAMERA_TRACE")!=nullptr;
    static uint64_t next_log=0;
    const uint64_t frame=g_frame.load(std::memory_order_relaxed);
    if (trace && frame>=next_log) {
        next_log=frame+120;
        log("freecamera: land bounds old %d %d %d %d new %.0f %.0f %.0f %.0f at frame %llu",
            lo[0],lo[1],hi[0],hi[1],std::floor((field_x-bounds.left)/10),std::floor((field_z-bounds.far)/10),
            std::ceil((field_x+bounds.right)/10),std::ceil((field_z+bounds.near)/10),(unsigned long long)frame);
        const uint64_t map=ld<uint64_t>(object+0x100);
        const VmInfo mv=vm_query(map);
        if ((mv.perm&1) && map>=mv.addr && mv.size>=0x30 && map-mv.addr<=mv.size-0x30)
            log("freecamera: land map %llx job %llx covers %d %d %d %d",
                (unsigned long long)map,(unsigned long long)object,ld<int32_t>(map+0x18),
                ld<int32_t>(map+0x1c),ld<int32_t>(map+0x28),ld<int32_t>(map+0x2c));
    }
    lo[0]=int32_t(std::floor((field_x-bounds.left)/10));
    lo[1]=int32_t(std::floor((field_z-bounds.far)/10));
    hi[0]=int32_t(std::ceil((field_x+bounds.right)/10));
    hi[1]=int32_t(std::ceil((field_z+bounds.near)/10));
    static bool logged=false;
    if (!logged) { logged=true; log("freecamera: land tile footprint updated before submission (%lldx%lld)",
        (long long)w,(long long)d); }
}
NX_HOOK(0x11fe020, land_bounds_update);
void lookat_return(CPU& cpu) {
    if (direct_lookat_camera && cpu.x[0]==direct_lookat_camera) {
        std::memcpy(reinterpret_cast<void*>(direct_lookat_camera+0x38),
                    direct_lookat_source.data(),sizeof direct_lookat_source);
        direct_lookat_camera=0;
        static std::atomic<bool> logged{false};
        if (std::getenv("NX_FREECAMERA_TRACE") && !logged.exchange(true))
            log("freecamera: direct LookAt inputs restored after matrix update");
    }
}
NX_HOOK(0x37e4724, lookat_return);
void lookat_scoped(CPU& cpu) {
    const uint64_t camera = cpu.x[0];
    bool readable = camera >= kHeapBase && camera <= kHeapBase + heap_size() - 0x5c;
    // One field-camera branch passes a temporary object on the guest stack.
    // Verify its complete writable span instead of excluding it as non-heap.
    if (!readable && cpu.x[30]-kModuleBase==0x7a88a4) {
        const VmInfo vm=vm_query(camera);
        readable=(vm.perm&3)==3 && camera>=vm.addr && vm.size>=0x5c && camera-vm.addr<=vm.size-0x5c;
    }
    if (!readable) {
        original_lookat(cpu);
        return;
    }
    // The engine copies these fields into other cameras and reads them on its next
    // simulation update. Persisting the orbit here feeds it back into those updates.
    float source[9];
    std::memcpy(source, reinterpret_cast<void*>(camera + 0x38), sizeof source);
    struct Restore {
        uint64_t camera;
        const float* source;
        bool previous;
        ~Restore() {
            std::memcpy(reinterpret_cast<void*>(camera + 0x38), source, 9 * sizeof(float));
            wrapped_lookat = previous;
        }
    } restore{camera, source, wrapped_lookat};
    wrapped_lookat = true;
    original_lookat(cpu); // entry hook applies orbit and records the temporal snapshot
}
}
void freecamera_install(const ModuleInfo* module, FuncEntry* functions) {
    if (std::strcmp(game::kInfo.id, "acnh") || std::strcmp(module->name, "main")) return;
    // Verify the beginning of the 3.0.3 implementation before replacing its dispatch.
    constexpr uint64_t entry = 0x37e4634;
    if (entry + 16 > module->text_size) return;
    const uint32_t words[] = {0x2d470801, 0x2d480c00, 0x1e232020, 0x2d491404};
    if (std::memcmp(reinterpret_cast<void*>(module->base + entry), words, sizeof words)) return;
    freecamera_fruit_install(module, enabled());
    freecamera_fog_install(enabled());
    for (size_t i = 0; i < module->func_count; i++) {
        if (functions[i].offset != entry) continue;
        original_lookat = functions[i].fn;
        functions[i].fn = lookat_scoped;
        log("freecamera: scoped camera matrix update installed (3.0.3)");
        if (const char* initial = std::getenv("NX_FREECAMERA_YAW")) {
            char* end = nullptr;
            float degrees = std::strtof(initial, &end);
            if (end != initial && *end == '\0' && std::isfinite(degrees)) {
                orbit.yaw = std::remainder(degrees, 360.0f) * freecamera::pi / 180.0f;
                log("freecamera: diagnostic initial yaw %.1f degrees", double(degrees));
            }
        }
        if (const char* initial=std::getenv("NX_FREECAMERA_PITCH")) {
            char* end=nullptr;
            const float degrees=std::strtof(initial,&end);
            if (end!=initial && *end=='\0' && std::isfinite(degrees) && std::abs(degrees)<=90) {
                orbit.pitch=degrees*freecamera::pi/180;
                log("freecamera: diagnostic initial pitch %.1f degrees",double(degrees));
            }
        }
        if (const char* initial=std::getenv("NX_FREECAMERA_ZOOM")) {
            char* end=nullptr;
            const float zoom=std::strtof(initial,&end);
            if (end!=initial && *end=='\0' && std::isfinite(zoom) && zoom>=.25f && zoom<=3) {
                orbit.zoom=zoom;
                log("freecamera: diagnostic initial zoom %.2f",double(zoom));
            }
        }
        break;
    }
    for (size_t i = 0; i < module->func_count; i++) {
        if (functions[i].offset == 0x37e5968 && ld<uint32_t>(module->base+0x37e5968)==0xbd409800 &&
            ld<uint32_t>(module->base+0x37e596c)==0x1e2c1003) {
            original_projection=functions[i].fn;
            functions[i].fn=projection_update;
            log("freecamera: perspective visibility projection capture installed");
        }
        if (functions[i].offset == 0x60cba4 && ld<uint32_t>(module->base+0x60cba4) == 0x91009000 && ld<uint32_t>(module->base+0x60cba8) == 0xd65f03c0) {
            original_float_storage = functions[i].fn;
            functions[i].fn = float_storage;
            log("freecamera: yaw-aware foreground/furniture bounds installed");
        }
        if (functions[i].offset == 0x14681fc && ld<uint32_t>(module->base+0x14681fc) == 0xd10383ff && ld<uint32_t>(module->base+0x14688b8) == 0xb91a3e69) {
            original_terrain = functions[i].fn;
            functions[i].fn = terrain_update;
            log("freecamera: yaw-aware terrain footprint installed");
        }
    }
}
void freecamera_ui_input() {
    ui_frame.store(g_frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
}
bool freecamera_field_active() {
    const uint64_t seen=field_frame.load(std::memory_order_relaxed), now=g_frame.load(std::memory_order_relaxed);
    return terrain_active() && field_enabled.load(std::memory_order_relaxed) && seen!=~uint64_t(0) && now>=seen && now-seen<=2;
}
float freecamera_zoom_depth_offset() {
    return zoom_depth_offset.load(std::memory_order_relaxed);
}
void freecamera_input(plat::PadState& p) {
    using namespace plat;
    using Clock = std::chrono::steady_clock;
    std::lock_guard guard(lock);
    static auto last = Clock::now();
    auto now = Clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.05f);
    last = now;
    const bool on = enabled();
    const bool zoom_key = (p.buttons & kFreeCameraZoom) != 0;
    const bool reset = (p.buttons & kFreeCameraReset) != 0;
    const bool stick_button = (p.buttons & kStickR) != 0;
    p.buttons &= ~(kFreeCameraZoom | kFreeCameraReset);
    if (!on) { orbit = {}; controls.cancel(); return; }
    const uint64_t seen = field_frame.load(std::memory_order_relaxed);
    const uint64_t now_frame = g_frame.load(std::memory_order_relaxed);
    const uint64_t ui_seen = ui_frame.load(std::memory_order_relaxed);
    const bool ui_active = ui_seen != ~uint64_t(0) && now_frame >= ui_seen && now_frame-ui_seen <= 2;
    const bool active = terrain_active() && seen != ~uint64_t(0) && now_frame >= seen && now_frame-seen <= 2 &&
                        !ui_active && !plat::keyboard_typing();
    controls.update(orbit, active, reset, zoom_key, stick_button, axis(p.rx), axis(p.ry), dt);
    if (active) {
        float x, y;
        freecamera::movement(orbit.yaw, float(p.lx), float(p.ly), x, y);
        p.lx = std::clamp(int32_t(std::lround(x)), -kStickMax, kStickMax);
        p.ly = std::clamp(int32_t(std::lround(y)), -kStickMax, kStickMax);
        p.buttons &= ~(kStickLLeft | kStickLRight | kStickLUp | kStickLDown);
    }
    if (!active) return;
    p.rx = p.ry = 0;
    p.buttons &= ~(kStickR | kStickRLeft | kStickRRight | kStickRUp | kStickRDown);
}
void freecamera_lookat(uint64_t cam, uint64_t caller) {
    static const bool trace = std::getenv("NX_FREECAMERA_TRACE") != nullptr;
    static std::atomic<int> traces{0};
    if (trace && traces++ < 24) log("freecamera: lookat %llx caller %llx", (unsigned long long)cam, (unsigned long long)caller);
    // Known island camera call sites in 3.0.2/3.0.3. Leave UI, rooms and photo cameras alone.
    const auto space=freecamera::camera_space(caller);
    if (space==freecamera::CameraSpace::other) return;
    field_frame.store(g_frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
    if (caller==0x7a88a4) {
        const VmInfo vm=vm_query(cam);
        if ((vm.perm&3)!=3 || cam<vm.addr || vm.size<0x5c || cam-vm.addr>vm.size-0x5c) return;
    } else if (cam < kHeapBase || cam + 0x5c > kHeapBase + heap_size()) return;
    std::lock_guard guard(lock);
    const bool on = enabled() && terrain_active();
    field_enabled.store(on,std::memory_order_relaxed);
    if (space==freecamera::CameraSpace::world) {
        field_x = ld<float>(cam+0x44);
        field_z = ld<float>(cam+0x4c);
    }
    float pos[3], at[3];
    std::memcpy(pos, reinterpret_cast<void*>(cam + 0x38), sizeof pos);
    std::memcpy(at, reinterpret_cast<void*>(cam + 0x44), sizeof at);
    for (int i = 0; i < 3; i++) if (!std::isfinite(pos[i]) || !std::isfinite(at[i])) return;
    float result[3];
    std::memcpy(result,pos,sizeof result);
    // Both direct and dispatched world cameras feed terrain selection. Record
    // the same orbit that their native matrix routine will consume below.
    if (on && !orbit.identity() && !orbit.apply(pos, at, result)) return;
    if (space==freecamera::CameraSpace::world) {
        std::copy_n(result,3,field_eye.begin());
        std::copy_n(at,3,field_target.begin());
        field_view_valid=true;
        float before=0, after=0;
        for (int i=0;i<3;++i) {
            before+=(pos[i]-at[i])*(pos[i]-at[i]);
            after+=(result[i]-at[i])*(result[i]-at[i]);
        }
        zoom_depth_offset.store(on ? std::sqrt(after)-std::sqrt(before) : 0,std::memory_order_relaxed);
    }
    update_visibility(on);
    if (!on || orbit.identity()) return;
    if (!wrapped_lookat) {
        static const bool direct_on=[] {
            const char* value=std::getenv("NX_FREECAMERA_DIRECT_LOOKAT");
            return !value || std::strcmp(value,"0");
        }();
        if (!direct_on) return;
        // There are no calls between the entry and sole return of this leaf
        // routine, so one per-thread saved input suffices for direct calls.
        if (direct_lookat_camera) return;
        direct_lookat_camera=cam;
        std::memcpy(direct_lookat_source.data(),reinterpret_cast<void*>(cam+0x38),sizeof direct_lookat_source);
    }
    std::memcpy(reinterpret_cast<void*>(cam + 0x38), result, sizeof result);
    // Orbit about the world's vertical axis. The stock camera's tilted up vector
    // produces roll as the yaw passes the sides of the island.
    const float up[] = {0, 1, 0};
    std::memcpy(reinterpret_cast<void*>(cam + 0x50), up, sizeof up);
}
}
