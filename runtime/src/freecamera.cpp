#include "freecamera.h"
#include "freecamera_math.h"
#include "freecamera_fruit.h"
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
freecamera::Mode mode;
Fn original_lookat = nullptr;
Fn original_float_storage = nullptr, original_terrain = nullptr;
thread_local bool wrapped_lookat = false;
std::atomic<uint64_t> field_frame{~uint64_t(0)};
float field_x = 0, field_z = 0;
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
        if (on) b = freecamera::rotate_bounds(b, orbit.yaw);
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
    if (!enabled() || field_frame.load() == ~uint64_t(0)) return;
    auto* lo = reinterpret_cast<int32_t*>(object+0x1a28);
    auto* hi = reinterpret_cast<int32_t*>(object+0x1a38);
    std::array<int32_t, 4> current{lo[0], lo[1], hi[0], hi[1]};
    auto found = terrain.find(object);
    if (found != terrain.end() && current == found->second.last) current = found->second.base;
    const int64_t w = int64_t(current[2])-current[0]+1, d = int64_t(current[3])-current[1]+1;
    if (w < 18 || w > 76 || d < 6 || d > 28) return;
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
void lookat_scoped(CPU& cpu) {
    const uint64_t camera = cpu.x[0];
    if (camera < kHeapBase || camera + 0x5c > kHeapBase + heap_size()) {
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
        break;
    }
    for (size_t i = 0; i < module->func_count; i++) {
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
void freecamera_input(plat::PadState& p) {
    using namespace plat;
    using Clock = std::chrono::steady_clock;
    std::lock_guard guard(lock);
    static auto last = Clock::now();
    auto now = Clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.05f);
    last = now;
    const bool on = enabled();
    const bool toggle = (p.buttons & kFreeCameraToggle) != 0 || (on && (p.buttons & kStickR));
    const bool reset = (p.buttons & kFreeCameraReset) != 0 || (on && mode.editing && (p.buttons & kStickL));
    p.buttons &= ~(kFreeCameraToggle | kFreeCameraReset);
    if (on) p.buttons &= ~kStickR;
    if (reset && on && mode.editing) p.buttons &= ~kStickL;
    const bool was_editing = mode.editing;
    mode.update(toggle, on);
    if (toggle && !on) {
        static bool warned = false;
        if (!warned) log("freecamera: F6 ignored: enable freecamera in Mod Manager and restart"), warned = true;
    }
    if (was_editing != mode.editing) log("freecamera: camera controls %s (F6/R3 toggle, F7/L3 reset)", mode.editing ? "on" : "off");
    if (!on) { orbit = {}; return; }
    const uint64_t seen = field_frame.load(std::memory_order_relaxed);
    const uint64_t now_frame = g_frame.load(std::memory_order_relaxed);
    if (seen != ~uint64_t(0) && now_frame >= seen && now_frame-seen <= 2) {
        float x, y;
        freecamera::movement(orbit.yaw, float(p.lx), float(p.ly), x, y);
        p.lx = std::clamp(int32_t(std::lround(x)), -kStickMax, kStickMax);
        p.ly = std::clamp(int32_t(std::lround(y)), -kStickMax, kStickMax);
        p.buttons &= ~(kStickLLeft | kStickLRight | kStickLUp | kStickLDown);
    }
    if (reset) orbit = {};
    if (!mode.editing) return;
    if (reset) { orbit = {}; }
    else {
        int direction = int(bool(p.buttons & kDown)) - int(bool(p.buttons & kUp));
        orbit.advance(axis(p.rx), axis(p.ry), direction, dt);
    }
    p.rx = p.ry = 0;
    p.buttons &= ~(kUp | kDown | kStickRLeft | kStickRRight | kStickRUp | kStickRDown);
}
void freecamera_lookat(uint64_t cam, uint64_t caller) {
    static const bool trace = std::getenv("NX_FREECAMERA_TRACE") != nullptr;
    static std::atomic<int> traces{0};
    if (trace && traces++ < 24) log("freecamera: lookat %llx caller %llx", (unsigned long long)cam, (unsigned long long)caller);
    // Known island camera call sites in 3.0.2/3.0.3. Leave UI, rooms and photo cameras alone.
    if (caller != 0x292cb0c && caller != 0x374b944 && caller != 0x374b954 && caller != 0x7a8830) return;
    field_frame.store(g_frame.load(std::memory_order_relaxed), std::memory_order_relaxed);
    if (cam < kHeapBase || cam + 0x5c > kHeapBase + heap_size()) return;
    std::lock_guard guard(lock);
    const bool on = enabled();
    if (caller != 0x7a8830) {
        field_x = ld<float>(cam+0x44);
        field_z = ld<float>(cam+0x4c);
    }
    update_visibility(on);
    if (!wrapped_lookat || !on || orbit.identity()) return;
    float pos[3], at[3];
    std::memcpy(pos, reinterpret_cast<void*>(cam + 0x38), sizeof pos);
    std::memcpy(at, reinterpret_cast<void*>(cam + 0x44), sizeof at);
    for (int i = 0; i < 3; i++) if (!std::isfinite(pos[i]) || !std::isfinite(at[i])) return;
    float result[3];
    if (!orbit.apply(pos, at, result)) return;
    std::memcpy(reinterpret_cast<void*>(cam + 0x38), result, sizeof result);
    // Orbit about the world's vertical axis. The stock camera's tilted up vector
    // produces roll as the yaw passes the sides of the island.
    const float up[] = {0, 1, 0};
    std::memcpy(reinterpret_cast<void*>(cam + 0x50), up, sizeof up);
}
}
