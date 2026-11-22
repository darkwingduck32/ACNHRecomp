#include "native.h"
#include "observation.h"
#include "features/module_hooks.h"
#include "features/registry.h"
#include "platform.h"
#include <nx/runtime.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace nx::features {
namespace {
using namespace narrative;
// Multiple producers may hit the shared UI hook. Contention or overflow drops
// diagnostics; it never waits for the consumer, allocates, logs or performs I/O.
class Observer {
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<UiObservation, 256> queue_{};
    size_t count_ = 0;
    std::atomic<uint64_t> dropped_{0};
    std::jthread worker_;
public:
    std::atomic<bool> ready{false};
    void start() {
        worker_ = std::jthread([this](std::stop_token stop) {
            std::array<UiObservation, 256> batch;
            std::array<UiObservation, 2> previous{};
            std::array<bool, 2> seen{};
            uint64_t revision = 0;
            log("narrative: observation only; no delivery; missing session/language/selection/identity/pending evidence");
            while (!stop.stop_requested()) {
                size_t count = 0;
                if (!lock_.test_and_set(std::memory_order_acquire)) {
                    count = count_;
                    for (size_t i = 0; i < count; ++i) batch[i] = queue_[i];
                    count_ = 0;
                    lock_.clear(std::memory_order_release);
                }
                for (size_t i = 0; i < count; ++i) {
                    const auto& e = batch[i];
                    const size_t k = size_t(e.kind);
                    const auto& p = previous[k];
                    // Suppress repeated A/B handlers and steady frame updates.
                    if (seen[k] && e.object == p.object && e.waiting == p.waiting &&
                        e.blocked == p.blocked && e.frame >= p.frame && e.frame - p.frame < 120)
                        continue;
                    seen[k] = true;
                    previous[k] = e;
                    Context context;
                    context.build = Known::yes;
                    context.busy = e.blocked || e.kind == UiObservation::Kind::menu;
                    const auto result = evaluate(context);
                    log("narrative: ui rev %llu frame %llu kind %s object %llx handler %llx waiting %d blocked %d eligibility %s reasons %03x dropped %llu context_verified 0",
                        (unsigned long long)++revision, (unsigned long long)e.frame,
                        k ? "menu" : "page", (unsigned long long)e.object,
                        (unsigned long long)e.handler, int(e.waiting), int(e.blocked),
                        result.state == Eligibility::blocked ? "blocked" : "unknown", result.reasons,
                        (unsigned long long)dropped_.load());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }
    void push(const UiObservation& event) noexcept {
        if (!ready.load(std::memory_order_relaxed)) return;
        if (lock_.test_and_set(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (count_ < queue_.size()) queue_[count_++] = event;
        else dropped_.fetch_add(1, std::memory_order_relaxed);
        lock_.clear(std::memory_order_release);
    }
};
Observer observer;
bool enabled = false;
} // namespace
void register_narrative_observer(Registry& registry) {
    Definition feature;
    feature.id = "acnh.narrative.observer";
    feature.configure = [] {
        const auto* value = std::getenv("NX_NARRATIVE_TRACE");
        enabled = value && std::string_view(value) == "1";
        if (enabled) {
            // Read the imported NSO header outside game hooks. Instruction
            // checks below independently validate the loaded boundaries.
            std::ifstream input(std::filesystem::u8path(plat::data_root()) / "exefs" / "main", std::ios::binary);
            std::array<unsigned char, 0x50> header{};
            constexpr std::array<unsigned char, 16> build = {
                0xff,0x1d,0x1c,0x05,0x67,0x0d,0xb6,0x02,0x1c,0x85,0xb6,0x24,0xa7,0x10,0xb9,0x63};
            input.read(reinterpret_cast<char*>(header.data()), header.size());
            enabled = bool(input) && header[0] == 'N' && header[1] == 'S' &&
                header[2] == 'O' && header[3] == '0' &&
                std::equal(build.begin(), build.end(), header.begin() + 0x40);
            if (!enabled) log("narrative: observation disabled (unknown/missing build header)");
        }
        if (enabled) observer.start();
    };
    feature.module_loaded = [](const ModuleInfo& module, FuncEntry*) {
        if (!enabled || std::string_view(module.name) != "main") return;
        // 3.0.3 input boundaries already present in the generated hook list.
        // No wrapper or new instruction hook is installed by this feature.
        constexpr InstructionWord words[] = {
            {0x3c4b20c, 0xd10143ff}, {0x3c4b210, 0xa9027bfd}, {0x3c4b214, 0xa90357f6},
            {0x3c48c70, 0xd10303ff}, {0x3c48c74, 0xa9067bfd}, {0x3c48c78, 0xa9076ffc},
        };
        const bool matched = instructions_match(module, words);
        observer.ready.store(matched, std::memory_order_relaxed);
        log("narrative: observation fingerprints %s", matched ? "accepted (3.0.3 boundaries)" : "rejected");
    };
    feature.ui_observation = [](const UiObservation& event) { observer.push(event); };
    registry.add(std::move(feature));
}
} // namespace nx::features
