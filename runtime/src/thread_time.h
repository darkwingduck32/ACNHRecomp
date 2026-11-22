#pragma once
#include <cstdint>

namespace nx {
// Cumulative CPU time of the calling thread, in nanoseconds (not wall time).
// Returns zero if the platform cannot supply it.
uint64_t thread_cpu_time_ns();
}
