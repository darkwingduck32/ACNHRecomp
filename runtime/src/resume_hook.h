#pragma once

#include <cstdint>

namespace nx {
struct CPU;

// A continuation has one owner. Returning false leaves the source instruction in control.
struct ResumeHookRegistrar {
    ResumeHookRegistrar(uint64_t offset, bool (*hook)(CPU&));
};
}

// The generated function resumes at its configured label only when requested.
void nx_hook_resume(nx::CPU& cpu, uint64_t pc, bool& resume);
