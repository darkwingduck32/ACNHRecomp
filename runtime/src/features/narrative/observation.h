#pragma once
#include <cstdint>

namespace nx::features::narrative {
enum class Known : uint8_t { unknown, no, yes };
enum class Eligibility : uint8_t { unknown, blocked, eligible };
enum Reason : uint32_t {
    unsupported_build = 1 << 0, unsupported_session = 1 << 1,
    unsupported_language = 1 << 2, native_branch = 1 << 3,
    missing_identity = 1 << 4, local_pending = 1 << 5,
    global_pending = 1 << 6, ui_busy = 1 << 7,
    unselected = 1 << 8,
};
// Owned evidence only. Unknown is deliberately the default for every fact.
// Pending=no requires an actual verified negative check, never absence of UI.
struct Context {
    Known build{}, session{}, language{}, selected{}, smalltalk{}, identity{};
    Known npc_pending{}, island_pending{};
    bool busy = false;
};
struct Result { Eligibility state; uint32_t reasons; };
constexpr Result evaluate(const Context& c) {
    uint32_t blocked = 0, missing = 0;
    const auto require = [&](Known fact, uint32_t reason) {
        if (fact == Known::no) blocked |= reason;
        if (fact == Known::unknown) missing |= reason;
    };
    require(c.build, unsupported_build);
    require(c.session, unsupported_session);
    require(c.language, unsupported_language);
    require(c.selected, unselected);
    require(c.smalltalk, native_branch);
    require(c.identity, missing_identity);
    const auto pending = [&](Known fact, uint32_t reason) {
        if (fact == Known::yes) blocked |= reason;
        if (fact == Known::unknown) missing |= reason;
    };
    pending(c.npc_pending, local_pending);
    pending(c.island_pending, global_pending);
    if (c.busy) blocked |= ui_busy;
    return {blocked ? Eligibility::blocked : missing ? Eligibility::unknown : Eligibility::eligible,
            blocked | missing};
}
// Presentation observations are NOT conversation identities or acknowledgements.
struct UiObservation {
    enum class Kind : uint8_t { page, menu } kind{};
    uint64_t frame = 0, object = 0, handler = 0;
    bool waiting = false, blocked = false;
};
} // namespace nx::features::narrative
