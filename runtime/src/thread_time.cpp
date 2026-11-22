#include "thread_time.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace nx {
uint64_t thread_cpu_time_ns() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
    const auto ticks = [](const FILETIME& t) {
        return (uint64_t(t.dwHighDateTime) << 32) | uint64_t(t.dwLowDateTime);
    };
    return (ticks(kernel) + ticks(user)) * 100; // FILETIME uses 100 ns units.
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) return 0;
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#else
    return 0;
#endif
}
}
