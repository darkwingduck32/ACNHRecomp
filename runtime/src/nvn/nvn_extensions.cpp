#include "nvn_extensions.h"
#include "config.h"
#include "nvn_draw_state.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>
namespace nx::nvn::replay {
using namespace nx::vk;
namespace {
std::atomic<uint64_t> revision{0};
std::vector<UniformExtension> &extensions() {
    static std::vector<UniformExtension> entries;
    return entries;
}
bool &frozen() {
    static bool value = false;
    return value;
}
const auto &ordered() {
    if (!frozen())
        throw std::logic_error("Initialize native render extensions before replay");
    return extensions();
}
struct Memo {
    const uint8_t *source = nullptr;
    const ShaderInfo *shader = nullptr;
    int bank = 0;
    uint64_t size = 0, padded = 0, epoch = 0, generation = 0, revision = 0;
    VkDescriptorBufferInfo info{};
};
PerThread<TlsArray<Memo, 256>> memo;
PerThread<uint64_t> hits, misses, bytes;
} // namespace
void register_uniform_extension(UniformExtension extension) {
    if (frozen())
        throw std::logic_error("Uniform extension registration is closed");
    if (extension.id.empty() || !extension.transform || !extension.matches ||
        std::any_of(extensions().begin(), extensions().end(), [&](const auto &old) { return old.id == extension.id; }))
        throw std::invalid_argument("Invalid/duplicate uniform extension");
    extensions().push_back(std::move(extension));
}
void invalidate_uniform_extensions() {
    revision.fetch_add(1, std::memory_order_relaxed);
}
void freeze_uniform_extensions() {
    if (frozen())
        return;
    std::sort(extensions().begin(), extensions().end(), [](const auto &a, const auto &b) {
        return a.priority != b.priority ? a.priority < b.priority : a.id < b.id;
    });
    frozen() = true;
}
bool extension_uniform_host_patch(const ShaderInfo *shader, int bank) {
    for (const auto &extension : ordered())
        if (extension.host_owned && extension.host_owned(shader, bank))
            return true;
    return false;
}
std::optional<VkDescriptorBufferInfo> extension_uniform_patch(const ShaderInfo *shader,
                                                              const DescriptorBinding &binding, const uint8_t *source,
                                                              uint64_t size, uint64_t padded) {
    if (!source || !shader || size > 65536 || padded > 65536 || padded < size || written_overlaps(source, size))
        return {};
    const auto &handlers = ordered();
    const uint64_t generation = config_generation();
    const uint64_t state_revision = revision.load(std::memory_order_relaxed);
    bool cache = true, any = false;
    for (const auto &extension : handlers)
        if (extension.matches(shader, binding)) {
            any = true;
            cache &= extension.cacheable && extension.cacheable(shader, binding);
        }
    if (!any)
        return {};
    auto &entry = (*memo)[(uintptr_t(source) >> 4 ^ uintptr_t(source) >> 15 ^ size) & 255];
    const int bank = binding.nvn.index;
    if (cache && entry.source == source && entry.shader == shader && entry.bank == bank && entry.size == size &&
        entry.padded == padded && entry.epoch == *g_upload_epoch && entry.generation == generation &&
        entry.revision == state_revision) {
        (*hits)++;
        return entry.info;
    }
    BytePatch accepted(size);
    std::vector<uint8_t> current;
    for (const auto &extension : handlers) {
        if (!extension.matches(shader, binding))
            continue;
        auto patch = extension.transform(shader, binding, current.empty() ? source : current.data(), size);
        if (patch.empty() && patch.valid())
            continue;
        if (!accepted.merge(patch, extension.ordered_overwrite)) {
            static std::atomic<int> reported{0};
            if (reported++ < 8)
                log("uniform extension %s rejected: overlapping or invalid edits", extension.id.c_str());
            continue;
        }
        if (current.empty())
            current.assign(source, source + size);
        patch.apply(current);
    }
    if (accepted.empty())
        return {};
    Upload upload = upload_alloc(padded, 256);
    std::memset(upload.ptr, 0, padded);
    std::memcpy(upload.ptr, current.data(), size);
    VkDescriptorBufferInfo info{upload.buffer, upload.offset, padded};
    (*misses)++;
    (*bytes) += padded;
    if (cache)
        entry = {source, shader, bank, size, padded, *g_upload_epoch, generation, state_revision, info};
    return info;
}
void extension_uniform_stats() {
    if (*hits + *misses)
        log("nvn: feature uniform uploads: %llu reused, %llu copied, %.2f MB", (unsigned long long)*hits,
            (unsigned long long)*misses, double(*bytes) / 1e6);
    *hits = *misses = *bytes = 0;
}
} // namespace nx::nvn::replay
