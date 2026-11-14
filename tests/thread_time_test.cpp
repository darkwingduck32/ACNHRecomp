#include "thread_time.h"
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    using Clock = std::chrono::steady_clock;
    const uint64_t before = nx::thread_cpu_time_ns();
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    uint64_t after = before;
    while (after - before < 100000000 && Clock::now() < deadline)
        after = nx::thread_cpu_time_ns();
    if (after < before || after - before < 10000000) {
        std::fprintf(stderr, "Calling-thread CPU time did not advance during work\n");
        return 1;
    }
    const uint64_t busy = after - before;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const uint64_t slept = nx::thread_cpu_time_ns();
    if (slept < after || slept - after >= busy / 2) {
        std::fprintf(stderr, "Sleeping incorrectly counted as substantial CPU work\n");
        return 1;
    }
    std::printf("thread CPU time: work %.2f ms; sleep charged %.2f ms\n",
                double(busy)/1e6, double(slept-after)/1e6);
}
