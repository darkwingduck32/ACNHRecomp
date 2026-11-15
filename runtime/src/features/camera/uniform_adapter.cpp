#include "uniform_adapter.h"
#include "freecamera.h"
#include "freecamera_fog.h"
#include "freecamera_sky.h"
#include "nvn/nvn_draw_state.h"
#include "version_303.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace nx::nvn::replay {
using namespace nx::vk;
namespace {
struct BufMemo {
  const uint8_t *p = nullptr;
  uint64_t size = 0, n = 0, epoch = 0;
  VkDescriptorBufferInfo info{};
};
PerThread<TlsArray<BufMemo, 256>> g_fog_memo;
PerThread<uint64_t> g_fog_hits, g_fog_misses, g_fog_bytes;
} // namespace
void camera_uniform_stats() {
  if (*g_fog_hits + *g_fog_misses)
    log("nvn: camera fog uploads: %llu reused, %llu copied, %.2f MB",
        (unsigned long long)*g_fog_hits, (unsigned long long)*g_fog_misses,
        double(*g_fog_bytes) / 1e6);
  *g_fog_hits = *g_fog_misses = *g_fog_bytes = 0;
}

// The merger gathers raw guest blocks. Such a block must not replace a host
// upload whose sky/fog values were adapted to the free camera.
bool camera_uniform_host_patch(const ShaderInfo *shader, int bank) {
  // Most terrain instance blocks cannot contain sky/fog constants. Reject
  // those before reading the cross-thread camera heartbeat on every tile.
  if (!shader || shader->stage != ShaderStage::Vertex || bank < 1 || bank > 3)
    return false;
  const auto &hash = shader->hash;
  const bool candidate =
      bank == 1   ? hash == camera303::shader_fce7966c1f0b7193 ||
                        hash == camera303::shader_3ebba4ffae3ffdd3 ||
                        hash == camera303::shader_0f62b0e3657eeab0
      : bank == 2 ? hash == camera303::shader_cabdc233d975169b ||
                        freecamera_fog_shader(hash)
                  : hash == camera303::shader_fce7966c1f0b7193;
  return candidate && freecamera_field_active();
}

std::optional<VkDescriptorBufferInfo>
camera_uniform_patch(const ShaderInfo *s, const DescriptorBinding &b,
                     const uint8_t *p, uint64_t size, uint64_t n) {
  // Native sky lookup tilts toward the fixed forward camera. Its lower
  // hemisphere becomes a dark band when looking backwards. Convert that
  // horizon depression to an azimuth-independent vertical lookup offset.
  if (p && size >= 25 * 16 && s->stage == ShaderStage::Vertex &&
      s->hash == camera303::shader_fce7966c1f0b7193 &&
      b.nvn.source == NvnSource::UniformBlock && b.nvn.index == 3 &&
      freecamera_field_active() && !written_overlaps(p, size)) {
    const Range &model = (*g_state).uniforms[0][1];
    const uint8_t *model_bytes =
        model.address ? guest_bytes(model.address) : nullptr;
    float tilt, height;
    std::memcpy(&tilt, p + 7 * 16, 4);
    std::memcpy(&height, p + 24 * 16 + 4, 4);
    if (model_bytes && model.size >= 48 && !written_overlaps(model_bytes, 48)) {
      float row[3];
      std::memcpy(row, model_bytes + 16, sizeof row);
      const float scale =
          std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
      if (freecamera::sky_horizon_height(tilt, height, scale, height)) {
        const float zero = 0;
        Upload u = upload_alloc(n, 256);
        std::memset(u.ptr, 0, n);
        std::memcpy(u.ptr, p, size);
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 7 * 16, &zero, 4);
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 24 * 16 + 4, &height, 4);
        return VkDescriptorBufferInfo{u.buffer, u.offset, n};
      }
    }
  }
  // Main-view sky/CloudVR (not their environment-probe variants) and DrawStar
  // share the stale model center. Sky/cloud c4 holds the model and c3 the
  // view; stars pack model+view into c5. CloudVR also needs its directional
  // horizon tilt replaced with a level depression. UVs/weather remain native.
  const bool sky =
      b.nvn.index == 1 &&
      (s->hash == camera303::shader_fce7966c1f0b7193 ||
       s->hash == camera303::shader_3ebba4ffae3ffdd3 ||
       s->hash == camera303::shader_0f62b0e3657eeab0); // sky colour/depth and
                                                       // main CloudVR
  const bool stars =
      b.nvn.index == 2 && s->hash == camera303::shader_cabdc233d975169b;
  if (p && size >= (stars ? 96u : 48u) && s->stage == ShaderStage::Vertex &&
      b.nvn.source == NvnSource::UniformBlock && (sky || stars) &&
      freecamera_field_active() && !written_overlaps(p, size)) {
    const Range &camera = (*g_state).uniforms[0][0];
    const auto *view_bytes = stars            ? p + 48
                             : camera.address ? guest_bytes(camera.address)
                                              : nullptr;
    float view[12], eye[3];
    if (view_bytes && (stars || camera.size >= sizeof view) &&
        !written_overlaps(view_bytes, sizeof view)) {
      std::memcpy(view, view_bytes, sizeof view);
      if (freecamera::sky_camera_center(view, eye)) {
        Upload u = upload_alloc(n, 256);
        std::memset(u.ptr, 0, n);
        std::memcpy(u.ptr, p, size);
        float model[12];
        std::memcpy(model, p, sizeof model);
        for (int row = 0; row < 3; ++row)
          model[row * 4 + 3] = eye[row];
        if (s->hash == camera303::shader_0f62b0e3657eeab0)
          freecamera::cloud_horizon_model(model);
        std::memcpy(u.ptr, model, sizeof model);
        return VkDescriptorBufferInfo{u.buffer, u.offset, n};
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
  static const bool glare_on = [] {
    const char *e = std::getenv("NX_FREECAMERA_GLARE");
    return !e || std::strcmp(e, "0") != 0;
  }();
  static const bool softer_lighting = [] {
    const char *e = std::getenv("NX_FREECAMERA_LIGHTING");
    return !e || std::strcmp(e, "0") != 0;
  }();
  const bool soft = softer_lighting;
  if (p && size >= 24 * 16 && s->stage == ShaderStage::Fragment &&
      b.nvn.source == NvnSource::UniformBlock && b.nvn.index == 4 &&
      freecamera_field_active() && freecamera_fog_shader(s->hash, true) &&
      !written_overlaps(p, size)) {
    float params[4], start;
    std::memcpy(params, p + 20 * 16, sizeof params);
    const bool scatter =
        scattering_on &&
        freecamera::scattering_start(params[2], params[1],
                                     freecamera_zoom_depth_offset(), start);
    float light[8]{}, light_scale = 1;
    if (soft && glare_on && size >= 77 * 16) {
      std::memcpy(light, p + 75 * 16, sizeof light);
      if (std::isfinite(light[7]) && light[7] > 0)
        light_scale =
            .25f * freecamera_directional_glare_scale(light, light[6]);
    }
    float haze;
    std::memcpy(&haze, p + 23 * 16 + 4, 4);
    const bool soften_haze = soft && std::isfinite(haze) && haze > 0;
    if (scatter || light_scale < 1 || soften_haze) {
      BufMemo &m =
          (*g_fog_memo)[(uintptr_t(p) >> 4 ^ uintptr_t(p) >> 15 ^ size) & 255];
      if (m.p == p && m.size == size && m.n == n &&
          m.epoch == (*g_upload_epoch)) {
        (*g_fog_hits)++;
        return m.info;
      }
      (*g_fog_misses)++;
      (*g_fog_bytes) += n;
      Upload u = upload_alloc(n, 256);
      std::memset(u.ptr, 0, n);
      std::memcpy(u.ptr, p, size);
      if (scatter)
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 20 * 16 + 8, &start, 4);
      if (soften_haze) {
        haze *= .75f;
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 23 * 16 + 4, &haze, 4);
      }
      if (light_scale < 1) {
        const float strength = light[7] * light_scale;
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 76 * 16 + 12, &strength, 4);
      }
      m = {p, size, n, (*g_upload_epoch), {u.buffer, u.offset, n}};
      return m.info;
    }
  }
  // Park UBER's c5[10..12] is the dark-fog colour/intensity, plane and
  // inverse range. Patch only a host upload, never shared guest constants.
  if (p && size >= 13 * 16 && s->stage == ShaderStage::Vertex &&
      b.nvn.source == NvnSource::UniformBlock && b.nvn.index == 2 &&
      freecamera_field_active() && freecamera_fog_shader(s->hash) &&
      !written_overlaps(p, size)) {
    float fog[12], plane[4];
    std::memcpy(fog, p + 10 * 16, sizeof fog);
    const bool adapted = freecamera::view_fog_plane(
        fog + 4, fog[3], fog[8], freecamera_zoom_depth_offset(), plane);
    const bool soften_fog = soft && std::isfinite(fog[3]) && fog[3] > 0;
    if (adapted || soften_fog) {
      BufMemo &m =
          (*g_fog_memo)[(uintptr_t(p) >> 4 ^ uintptr_t(p) >> 15 ^ size) & 255];
      if (m.p == p && m.size == size && m.n == n &&
          m.epoch == (*g_upload_epoch)) {
        (*g_fog_hits)++;
        return m.info;
      }
      (*g_fog_misses)++;
      (*g_fog_bytes) += n;
      Upload u = upload_alloc(n, 256);
      std::memset(u.ptr, 0, n);
      std::memcpy(u.ptr, p, size);
      if (adapted)
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 11 * 16, plane,
                    sizeof plane);
      if (soften_fog) {
        const float strength = fog[3] * .6f;
        std::memcpy(static_cast<uint8_t *>(u.ptr) + 10 * 16 + 12, &strength, 4);
      }
      m = {p, size, n, (*g_upload_epoch), {u.buffer, u.offset, n}};
      return m.info;
    }
  }
  return std::nullopt;
}
} // namespace nx::nvn::replay
