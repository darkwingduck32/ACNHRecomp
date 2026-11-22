#include "uniform_adapter.h"
#include "config.h"
#include "freecamera.h"
#include "freecamera_fog.h"
#include "freecamera_sky.h"
#include "native.h"
#include "nvn/nvn_draw_state.h"
#include "version_303.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace nx::nvn::replay {
using namespace nx::vk;
namespace {
struct LightingSettings {
    uint64_t generation = ~uint64_t(0);
    bool glare = true, soft = true;
};
PerThread<LightingSettings> g_lighting;
void trace_sky_patch(const ShaderInfo *shader, int bank) {
    static const bool trace = std::getenv("NX_FREECAMERA_TRACE") != nullptr;
    if (!trace)
        return;
    thread_local unsigned seen = 0;
    const unsigned bit = bank == 3                                            ? 1
                         : shader->hash == camera303::shader_fce7966c1f0b7193 ? 2
                         : shader->hash == camera303::shader_3ebba4ffae3ffdd3 ? 4
                         : shader->hash == camera303::shader_0f62b0e3657eeab0 ? 8
                                                                              : 16;
    if (seen & bit)
        return;
    seen |= bit;
    log("camera: sky correction %s bank %d orbit %d fov %d zoom offset %.3f", shader->hash.c_str(), bank,
        int(freecamera_field_active()), camera_lens_degrees(), double(freecamera_zoom_depth_offset()));
}
} // namespace
// The merger gathers raw guest blocks. Such a block must not replace a host
// upload whose sky/fog values were adapted to the free camera.
bool camera_uniform_host_patch(const ShaderInfo *shader, int bank) {
    // Most terrain instance blocks cannot contain sky/fog constants. Reject
    // those before reading the cross-thread camera heartbeat on every tile.
    if (!shader || shader->stage != ShaderStage::Vertex || bank < 1 || bank > 3)
        return false;
    const auto &hash = shader->hash;
    const bool sky = bank == 1
                         ? hash == camera303::shader_fce7966c1f0b7193 || hash == camera303::shader_3ebba4ffae3ffdd3 ||
                               hash == camera303::shader_0f62b0e3657eeab0
                     : bank == 2 ? hash == camera303::shader_cabdc233d975169b
                                 : hash == camera303::shader_fce7966c1f0b7193;
    return (sky && camera_sky_active()) || (bank == 2 && freecamera_fog_shader(hash) && camera_view_active());
}

BytePatch camera_uniform_patch(const ShaderInfo *s, const DescriptorBinding &b, const uint8_t *p, uint64_t size) {
    BytePatch patch(size);
    // Native sky lookup tilts toward the fixed forward camera. Its lower
    // hemisphere becomes a dark band when looking backwards. Convert that
    // horizon depression to an azimuth-independent vertical lookup offset.
    if (p && size >= 25 * 16 && s->stage == ShaderStage::Vertex && s->hash == camera303::shader_fce7966c1f0b7193 &&
        b.nvn.source == NvnSource::UniformBlock && b.nvn.index == 3 && camera_sky_active() &&
        !written_overlaps(p, size)) {
        const Range &model = (*g_state).uniforms[0][1];
        const uint8_t *model_bytes = model.address ? guest_bytes(model.address) : nullptr;
        float tilt, height;
        std::memcpy(&tilt, p + 7 * 16, 4);
        std::memcpy(&height, p + 24 * 16 + 4, 4);
        if (model_bytes && model.size >= 48 && !written_overlaps(model_bytes, 48)) {
            float row[3];
            std::memcpy(row, model_bytes + 16, sizeof row);
            const float scale = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
            if (freecamera::sky_horizon_height(tilt, height, scale, height)) {
                const float zero = 0;
                patch.write(7 * 16, zero);
                patch.write(24 * 16 + 4, height);
                trace_sky_patch(s, b.nvn.index);
                return patch;
            }
        }
    }
    // Main-view sky/CloudVR (not their environment-probe variants) and DrawStar
    // share the stale model center. Sky/cloud c4 holds the model and c3 the
    // view; stars pack model+view into c5. CloudVR also needs its directional
    // horizon tilt replaced with a level depression. UVs/weather remain native.
    const bool sky = b.nvn.index == 1 &&
                     (s->hash == camera303::shader_fce7966c1f0b7193 || s->hash == camera303::shader_3ebba4ffae3ffdd3 ||
                      s->hash == camera303::shader_0f62b0e3657eeab0); // sky colour/depth and
                                                                      // main CloudVR
    const bool stars = b.nvn.index == 2 && s->hash == camera303::shader_cabdc233d975169b;
    if (p && size >= (stars ? 96u : 48u) && s->stage == ShaderStage::Vertex &&
        b.nvn.source == NvnSource::UniformBlock && (sky || stars) && camera_sky_active() &&
        !written_overlaps(p, size)) {
        const Range &camera = (*g_state).uniforms[0][0];
        const auto *view_bytes = stars ? p + 48 : camera.address ? guest_bytes(camera.address) : nullptr;
        float view[12], eye[3];
        if (view_bytes && (stars || camera.size >= sizeof view) && !written_overlaps(view_bytes, sizeof view)) {
            std::memcpy(view, view_bytes, sizeof view);
            if (freecamera::sky_camera_center(view, eye)) {
                float model[12];
                std::memcpy(model, p, sizeof model);
                for (int row = 0; row < 3; ++row)
                    model[row * 4 + 3] = eye[row];
                if (s->hash == camera303::shader_0f62b0e3657eeab0)
                    freecamera::cloud_horizon_model(model);
                patch.write(0, model);
                trace_sky_patch(s, b.nvn.index);
                return patch;
            }
        }
    }
    // Park UBER's fragment scattering is separate from vertex dark fog.
    // It adds light according to view depth; anchor its onset to the player
    // so zooming out does not push nearby scenery into full scattering.
    // The same block carries a separate directional glint at c7[75..76].
    // Bound its shallow-angle growth without changing other material lighting.
    static const bool scattering_on = [] {
        const char *e = std::getenv("NX_FREECAMERA_SCATTERING");
        return !e || std::strcmp(e, "0") != 0;
    }();
    auto &lighting = *g_lighting;
    const uint64_t settings_generation = config_generation();
    if (lighting.generation != settings_generation) {
        lighting.glare = [] {
            if (const char *all = std::getenv("NX_FREECAMERA_LIGHTING"); all && std::strcmp(all, "0") == 0)
                return false;
            const char *e = std::getenv("NX_FREECAMERA_GLARE");
            return e ? std::strcmp(e, "0") != 0 : config_get_bool("camera.reduce_glare", true);
        }();
        lighting.soft = [] {
            const char *e = std::getenv("NX_FREECAMERA_LIGHTING");
            return e ? std::strcmp(e, "0") != 0 : config_get_bool("camera.reduce_fog", true);
        }();
        lighting.generation = settings_generation;
        static const bool trace = std::getenv("NX_FREECAMERA_TRACE") != nullptr;
        if (trace && camera_view_active())
            log("camera: fog reduction %d orbit %d", int(lighting.soft), int(freecamera_field_active()));
    }
    const bool glare_on = lighting.glare && freecamera_field_active(), soft = lighting.soft;
    if (p && size >= 24 * 16 && s->stage == ShaderStage::Fragment && b.nvn.source == NvnSource::UniformBlock &&
        b.nvn.index == 4 && camera_view_active() && freecamera_fog_shader(s->hash, true) &&
        !written_overlaps(p, size)) {
        float params[4], start;
        std::memcpy(params, p + 20 * 16, sizeof params);
        const bool scatter =
            scattering_on && freecamera::scattering_start(params[2], params[1], freecamera_zoom_depth_offset(), start);
        float light[8]{}, light_scale = 1;
        if (glare_on && size >= 77 * 16) {
            std::memcpy(light, p + 75 * 16, sizeof light);
            if (std::isfinite(light[7]) && light[7] > 0)
                light_scale = .25f * freecamera_directional_glare_scale(light, light[6]);
        }
        float haze;
        std::memcpy(&haze, p + 23 * 16 + 4, 4);
        const bool soften_haze = soft && std::isfinite(haze) && haze > 0;
        if (scatter || light_scale < 1 || soften_haze) {
            if (scatter)
                patch.write(20 * 16 + 8, start);
            if (soften_haze) {
                haze *= .75f;
                patch.write(23 * 16 + 4, haze);
            }
            if (light_scale < 1) {
                const float strength = light[7] * light_scale;
                patch.write(76 * 16 + 12, strength);
            }
            return patch;
        }
    }
    // Park UBER's c5[10..12] is the dark-fog colour/intensity, plane and
    // inverse range. Patch only a host upload, never shared guest constants.
    if (p && size >= 13 * 16 && s->stage == ShaderStage::Vertex && b.nvn.source == NvnSource::UniformBlock &&
        b.nvn.index == 2 && camera_view_active() && freecamera_fog_shader(s->hash) && !written_overlaps(p, size)) {
        float fog[12], plane[4];
        std::memcpy(fog, p + 10 * 16, sizeof fog);
        const bool adapted = camera_sky_active() &&
                             freecamera::view_fog_plane(fog + 4, fog[3], fog[8], freecamera_zoom_depth_offset(), plane);
        const bool soften_fog = soft && std::isfinite(fog[3]) && fog[3] > 0;
        if (adapted || soften_fog) {
            if (adapted)
                patch.write(11 * 16, plane);
            if (soften_fog) {
                const float strength = fog[3] * .6f;
                patch.write(10 * 16 + 12, strength);
            }
            return patch;
        }
    }
    return patch;
}
bool camera_uniform_cacheable(const ShaderInfo *shader, const DescriptorBinding &binding) {
    if (!shader || !camera_view_active())
        return false;
    return binding.nvn.source == NvnSource::UniformBlock &&
           ((shader->stage == ShaderStage::Fragment && binding.nvn.index == 4 &&
             freecamera_fog_shader(shader->hash, true)) ||
            (shader->stage == ShaderStage::Vertex && binding.nvn.index == 2 && freecamera_fog_shader(shader->hash)));
}
bool camera_uniform_matches(const ShaderInfo *shader, const DescriptorBinding &binding) {
    if (!shader || binding.nvn.source != NvnSource::UniformBlock)
        return false;
    if (shader->stage == ShaderStage::Fragment)
        return binding.nvn.index == 4 && freecamera_fog_shader(shader->hash, true) && camera_view_active();
    return camera_uniform_host_patch(shader, binding.nvn.index);
}
void register_camera_uniform_extension() {
    register_uniform_extension({"acnh.camera", 0, camera_uniform_host_patch, camera_uniform_matches,
                                camera_uniform_cacheable, camera_uniform_patch, false});
}
} // namespace nx::nvn::replay
