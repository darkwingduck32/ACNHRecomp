#pragma once
#include <algorithm>
#include <cstring>
#include <nx/module.h>
#include <span>
namespace nx::features {
struct InstructionWord {
    uint64_t offset;
    uint32_t value;
};
inline bool instructions_match(const ModuleInfo &module, std::span<const InstructionWord> expected) {
    for (const auto &word : expected) {
        if (word.offset < module.text || word.offset - module.text > module.text_size ||
            module.text_size - (word.offset - module.text) < 4)
            return false;
        uint32_t actual;
        std::memcpy(&actual, reinterpret_cast<const void *>(module.base + word.offset), 4);
        if (actual != word.value)
            return false;
    }
    return true;
}
// Preserve the preceding wrapper as `next`. This affects indirect dispatch;
// direct compiled calls still require a generated instruction hook.
inline bool wrap_function(std::span<FuncEntry> entries, uint64_t offset, Fn replacement, Fn &next) {
    auto found =
        std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.offset == offset; });
    if (found == entries.end() || !found->fn || !replacement || found->fn == replacement || next)
        return false;
    next = found->fn;
    found->fn = replacement;
    return true;
}
} // namespace nx::features
