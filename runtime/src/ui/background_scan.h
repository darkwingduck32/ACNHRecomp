#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace nx::ui {
// UI-thread cache with at most one filesystem scan in flight. A slow drive
// must not block drawing, leaving the page, or destruction of the cache.
// Workers own their result storage and never access the cache or ImGui.
template<class T> class BackgroundScan {
    struct Pending {
        T value{};
        bool succeeded=false;
        std::atomic<bool> done{false};
    };
    std::shared_ptr<Pending> pending;
    T cached{};
    double next=0;
public:
    const T& value() const { return cached; }
    bool running() const { return bool(pending); }

    template<class Load> void poll(double now, Load load) {
        if (pending) {
            if (!pending->done.load(std::memory_order_acquire)) return;
            if (pending->succeeded) cached=std::move(pending->value);
            pending.reset();
            next=now+5;
        }
        if (now<next) return;
        auto result=std::make_shared<Pending>();
        try {
            std::thread([result, load=std::move(load)]() mutable {
                try {
                    result->value=load();
                    result->succeeded=true;
                } catch (...) { /* Keep the previous result on failure. */ }
                result->done.store(true,std::memory_order_release);
            }).detach();
            pending=std::move(result);
        } catch (...) {
            // Thread creation can fail; retry later without breaking the menu.
            next=now+5;
        }
    }
};
}
