#include "version_303.h"
#include "freecamera.h"
#include "native.h"
#include "features/module_hooks.h"
#include "platform.h"
#include <nx/runtime.h>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <climits>
namespace nx {
Fn g_camera_terrain_update = nullptr;
int g_camera_terrain_scale = 1;

void camera_terrain_update(CPU& c) {
    const uint64_t object = c.x[0];
    g_camera_terrain_update(c);
    if (!camera_enabled()) return;
    const VmInfo vm = vm_query(object);
    if (!(vm.perm & 2) || object < vm.addr || vm.size < 0x1a40 ||
        object - vm.addr > vm.size - 0x1a40) return;
    auto* low = reinterpret_cast<int32_t*>(object + 0x1a28);
    auto* high = reinterpret_cast<int32_t*>(object + 0x1a38);
    // The manager also exists in UI/indoor stages. Only a populated, small
    // outdoor tile window is eligible; preserve invalid/empty sentinel bounds.
    const int64_t width = int64_t(high[0]) - low[0] + 1;
    const int64_t depth = int64_t(high[1]) - low[1] + 1;
    if ((width != 18 && width != 19) || (depth != 6 && depth != 7)) return;
    for (int axis = 0; axis < 2; ++axis) {
        const int64_t count = int64_t(high[axis]) - low[axis] + 1;
        const int64_t extra = count * (g_camera_terrain_scale - 1);
        const int64_t first = int64_t(low[axis]) - extra / 2;
        const int64_t last = int64_t(high[axis]) + (extra - extra / 2);
        if (first < INT32_MIN || last > INT32_MAX) return;
    }
    for (int axis = 0; axis < 2; ++axis) {
        const int64_t count = int64_t(high[axis]) - low[axis] + 1;
        const int64_t extra = count * (g_camera_terrain_scale - 1);
        low[axis] -= int32_t(extra / 2);
        high[axis] += int32_t(extra - extra / 2);
    }
}

void camera_install_terrain(const ModuleInfo* mi, FuncEntry* funcs) {
    if (std::strcmp(mi->name, "main") || mi->text_size != camera303::offset_3f06df0) return;
    constexpr int scale = 2;
    const uint32_t offsets[] = {camera303::offset_14681fc, camera303::offset_1468890, camera303::offset_14688a4, camera303::offset_14688a8, camera303::offset_14688b4, camera303::offset_14688b8};
    const uint32_t words[] = {camera303::instruction_d10383ff, camera303::instruction_6b0b015f, camera303::instruction_b91a3a6a, camera303::instruction_b91a2a6d, camera303::instruction_b91a2e6a, camera303::instruction_b91a3e69};
    features::InstructionWord expected[6];
    for (size_t i=0;i<6;++i) expected[i]={offsets[i],words[i]};
    if (!features::instructions_match(*mi,expected)) {
        log("camera303: terrain range patch skipped: 3.0.3 instruction mismatch");
        return;
    }
    if (features::wrap_function({funcs,mi->func_count}, camera303::offset_14681fc,
                                camera_terrain_update, g_camera_terrain_update)) {
        g_camera_terrain_scale = scale;
        log("camera303: experimental terrain tile range %dx enabled (3.0.3)", scale);
        return;
    }
    log("camera303: terrain range patch skipped: update function missing");
}

}
