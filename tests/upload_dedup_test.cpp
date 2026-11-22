#include "nvn_draw_state.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
std::vector<uint8_t> uploads(180ull << 20);
uint64_t used = 0, allocations = 0;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
}

namespace nx {
void log(const char*, ...) {}
namespace nvn {
void write_back_before_read(const uint8_t*, uint64_t, const char*) {}
Upload upload_alloc(uint64_t size, uint64_t alignment) {
    used = (used + alignment - 1) & ~(alignment - 1);
    check(size <= uploads.size() - used, "test upload ring exhausted");
    Upload result{VK_NULL_HANDLE, used, uploads.data() + used};
    used += size;
    ++allocations;
    return result;
}
namespace replay { PerThread<State> g_state; }
}
}

int main() {
    using namespace nx::nvn::replay;
    try {
        std::vector<uint8_t> data(72ull << 20, 0x31);
        auto reset = [&] { dedup_reset(); used = allocations = 0; };
        reset();
        auto original = dedup_upload(data.data(), 256, 512, 256);
        auto same = dedup_upload(data.data(), 256, 512, 256);
        check(original.ptr == same.ptr && allocations == 1, "unchanged upload was not reused");
        check(std::all_of(original.ptr + 256, original.ptr + 512, [](uint8_t b) { return b == 0; }),
              "uniform padding was not zeroed");
        dedup_upload(data.data() + 512, 256, 256, 256);
        dedup_invalidate(data.data() + 256, 256);  // touching endpoints do not overlap.
        check(dedup_upload(data.data(), 256, 512, 256).ptr == original.ptr, "adjacent update invalidated upload");
        data[255] = 0x72;
        dedup_invalidate(data.data() + 255, 258);  // overlaps both ranges, including their front-cache entries.
        check(dedup_upload(data.data(), 256, 512, 256).ptr != original.ptr, "overlapping update reused stale upload");
        check(dedup_upload(data.data(), 256, 512, 256).ptr[255] == 0x72, "updated byte was not uploaded");

        reset();
        auto large = dedup_upload(data.data(), 70ull << 20, 70ull << 20, 16);
        dedup_upload(data.data() + 64, 16, 16, 16);
        data[68ull << 20] = 0x5a;
        dedup_invalidate(data.data() + (68ull << 20), 1);
        auto changed = dedup_upload(data.data(), 70ull << 20, 70ull << 20, 16);
        check(changed.ptr != large.ptr && changed.ptr[68ull << 20] == 0x5a,
              "long overlapping upload hidden by a shorter entry was not invalidated");

        reset();
        std::mt19937 rng(0xAC2026);
        struct Reference { size_t at, size; bool valid; uint8_t* upload; };
        std::vector<Reference> reference;
        for (int step = 0; step < 4000; ++step) {
            if (step % 3 == 0 && !reference.empty()) {
                const size_t at = rng() % 65536, size = 1 + rng() % 2048;
                std::fill_n(data.data() + at, size, uint8_t(step));
                dedup_invalidate(data.data() + at, size);
                for (auto& e : reference)
                    if (e.at < at + size && at < e.at + e.size) e.valid = false;
            } else {
                const size_t at = 16 * (rng() % 4096), size = 1 + rng() % 2048;
                auto entry = std::find_if(reference.begin(), reference.end(), [&](const auto& e) { return e.at == at; });
                const bool hit = entry != reference.end() && entry->valid && entry->size == size;
                auto upload = dedup_upload(data.data() + at, size, size, 16);
                if (hit) check(upload.ptr == entry->upload, "valid cached range was not reused");
                check(std::memcmp(upload.ptr, data.data() + at, size) == 0, "cached upload differs from guest bytes");
                if (entry == reference.end()) reference.push_back({at, size, true, upload.ptr});
                else *entry = {at, size, true, upload.ptr};
            }
        }
        reset();
        data[0] = 0x91;
        check(dedup_upload(data.data(), 16, 16, 16).ptr[0] == 0x91, "submit reset retained stale entries");
        std::cout << "Upload reuse, padding, overlapping updates, long ranges, and randomized byte checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
