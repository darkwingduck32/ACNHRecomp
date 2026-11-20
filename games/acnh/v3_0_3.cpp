// ACNH 3.0.3 (main build id FF1D1C05670DB6021C85B624A710B963): 3.0.2's main with 16 bytes more code before
// main+31c3b50, so the hooks from there on sit 0x10 further and the data (vtables, the budget table, the page turner
// class) stays where 3.0.2 has it. Its hook list is games/acnh/hooks-3.0.3.txt (games/acnh/versions.txt); an offset
// here does nothing in a build whose list does not have it. The 60 FPS and wide screen hooks are next to 3.0.2's
// (native60.cpp, runtime/src/nvn/nvn_ultrawide.cpp); Pane::Calculate and the Phive guard are at 3.0.2's addresses.
#include <nx/runtime.h>
#include "config.h"
#include "hle_ldn.h"

#include <atomic>
#include <cstdlib>

namespace nx {

void acnh_gui_touch_update(uint64_t manager);  // games/acnh/pointer.cpp
void acnh_focus_input(CPU& c, uint64_t set_focus);
void acnh_button_input(CPU& c, uint64_t page_turner);

namespace {

constexpr uint64_t kPageTurner303 = 0x49fc8f0;  // the message window's page turner class: 3.0.2's

// guiTouchManager::update (main+31c3b60) right before its hit tests (x24 = the manager, as in 3.0.2).
void gui_touch(CPU& c) { acnh_gui_touch_update(c.x[24]); }
// The focus manager input (main+3c48c70) and its focus move (main+3c48b78; 3.0.2's main+3c48b68).
void focus_input(CPU& c) { acnh_focus_input(c, 0x3c48b78); }
// A button handler's input (main+3c4b20c): a click while a message waits.
void button_input(CPU& c) { acnh_button_input(c, kPageTurner303); }

// Native field lookup returns the same root pointer for an anchor and its
// continuations. With an output position it also returns the center of the
// root half-cell in world units. Keep all temporary arguments on guest stack.
struct FieldHit { uint64_t record; int x, y; };
FieldHit field_hit(const CPU& caller, int x, int y, unsigned layer = 0) {
    CPU q = caller;
    q.x[31] -= 0x40;
    const auto pos = q.x[31] + 0x10, root = pos + 0x10;
    st<float>(pos, x * 5.f + 2.5f);
    st<float>(pos + 4, 0.f);
    st<float>(pos + 8, y * 5.f + 2.5f);
    st<float>(root, 0.f);
    st<float>(root + 8, 0.f);
    q.x[0] = pos;
    q.x[1] = root;
    q.x[2] = layer;
    call(q, kModuleBase + 0x15e1014);
    return {q.x[0], static_cast<int>(ld<float>(root) / 5.f),
            static_cast<int>(ld<float>(root + 8) / 5.f)};
}

// Use the native shovel's ground test for each newly covered terrain tile.
// The guest tile-position object contains its vtable followed by two ints.
bool diggable_tile(const CPU& c, int x, int y) {
    CPU q = c;
    q.x[31] -= 0x20;
    const auto pos = q.x[31] + 0x10;
    st<uint64_t>(pos, ld<uint64_t>(kModuleBase + 0x4ab2018) + 0x10);
    st<int32_t>(pos + 8, x);
    st<int32_t>(pos + 12, y);
    q.x[0] = pos;
    call(q, kModuleBase + 0x877130);
    return (q.x[0] & 1) != 0;
}

float field_height(const CPU& c, int x, int y) {
    CPU q = c;
    q.x[31] -= 0x20;
    q.x[0] = q.x[31] + 0x10;
    st<float>(q.x[0], x * 5.f + 2.5f);
    st<float>(q.x[0] + 4, 0.f);
    st<float>(q.x[0] + 8, y * 5.f + 2.5f);
    q.x[1] = q.x[2] = 0;
    q.v[0].f32[0] = 0.f;
    call(q, kModuleBase + 0x15e026c);
    return q.v[0].f32[0];
}

bool clear_placement(const CPU& c, int x, int y, int dx, int dy) {
    const float ground = field_height(c, x, y);
    const auto source = field_hit(c, x, y);
    const auto hole = source.record && source.x == x && source.y == y &&
                      ld<uint16_t>(source.record) == 60025 ? source.record : 0;
    for (int ty = y / 2; ty <= (y + dy + 1) / 2; ++ty)
        for (int tx = x / 2; tx <= (x + dx + 1) / 2; ++tx)
            if (!diggable_tile(c, tx, ty)) return false;
    for (int cy = y + dy; cy < y + dy + 2; ++cy) {
        for (int cx = x + dx; cx < x + dx + 2; ++cx) {
            if (field_height(c, cx, cy) != ground) return false;
            for (unsigned layer = 0; layer < 2; ++layer) {
                const auto hit = field_hit(c, cx, cy, layer);
                if (!hit.record) return false;
                if (ld<uint16_t>(hit.record) != 0xfffe && !(layer == 0 && hit.record == hole))
                    return false;
            }
        }
    }
    return true;
}

void refresh_field_cell(const CPU& c, int x, int y) {
    CPU q = c;
    q.x[31] -= 0x20;
    const auto pos = q.x[31] + 0x10;
    st<uint64_t>(pos, ld<uint64_t>(kModuleBase + 0x4ab2010) + 0x10);
    st<int32_t>(pos + 8, x);
    st<int32_t>(pos + 12, y);
    q.x[0] = pos;
    q.x[1] = 7; // same field refresh used after the shovel changes an item
    call(q, kModuleBase + 0x10dbc54);
}

void placement_offset(CPU& c) {
    const auto setting = config_get("game.field_placement", "tile");
    const int dx = setting == "east" || setting == "southeast";
    const int dy = setting == "south" || setting == "southeast";
    // Local preferences cannot change a peer's copy of an already-sent action.
    if ((!dx && !dy) || ldn_net_up()) return;
    const int x = static_cast<int>(c.x[8]), y = static_cast<int>(c.x[9]);
    if (x < 32 || y < 32 || x + dx >= 255 || y + dy >= 223 || ((x | y) & 1)) return;
    if (!clear_placement(c, x, y, dx, dy)) return;
    c.x[8] = x + dx;
    c.x[9] = y + dy;
}

void refresh_placement(CPU& c, uint64_t written) {
    const int x = ld<int32_t>(written), y = ld<int32_t>(written + 4);
    const auto action = c.x[19];
    if (x != ld<int16_t>(action + 12) || y != ld<int16_t>(action + 14))
        refresh_field_cell(c, x, y);
}

void refresh_plant(CPU& c) { refresh_placement(c, c.x[29] - 0x20); }
void refresh_burial(CPU& c) { refresh_placement(c, c.x[29] - 0x10); }

void plant_offset(CPU& c) {
    // This branch of the local field action also handles other natural items.
    const auto id = ld<uint16_t>(c.x[31] + 0x38);
    // PltTree field-item IDs in the 3.0.3 field catalog.
    static constexpr uint16_t trees[][2] = {
        {60000,60002}, {60005,60009}, {60022,60023}, {60063,60063},
        {60084,60088}, {60094,60106}, {60352,60371}, {60523,60531},
        {60536,60538}, {60567,60567}, {60570,60570}, {60572,60576},
        {60604,60607}, {60694,60694},
    };
    for (const auto& range : trees) {
        if (id >= range[0] && id <= range[1]) {
            placement_offset(c);
            return;
        }
    }
}

FieldHit fractional_trunk(const CPU& c, int x, int y) {
    if (static_cast<unsigned>(x) > 255 || static_cast<unsigned>(y) > 255) return {};
    const auto hit = field_hit(c, x, y);
    if (!hit.record || ((hit.x | hit.y) & 1) == 0 ||
        hit.x < x - 1 || hit.x > x || hit.y < y - 1 || hit.y > y) return {};
    const auto id = ld<uint16_t>(hit.record);
    return id >= 60000 && id < 0xfffd ? hit : FieldHit{};
}

// Tree effects carry a source point and a second point. For a falling
// fruit the second point is the landing spot, independent of the trunk.
void tree_effect(CPU& c) {
    const auto caller = c.x[30] - kModuleBase;
    int x, y;
    switch (caller) {
    case 0x21bc6e4:
        x = static_cast<int>(c.x[22]);
        y = static_cast<int>(c.x[25]);
        break;
    case 0x21c59b4:
    case 0x21c515c: {
        const auto event = c.x[caller == 0x21c59b4 ? 19 : 20];
        x = ld<int16_t>(event + 12);
        y = ld<int16_t>(event + 14);
        break;
    }
    default:
        return;
    }
    const auto hit = fractional_trunk(c, x, y);
    if (!hit.record) return;
    const auto effect = c.x[0];
    st<int32_t>(effect + 8, hit.x);
    st<int32_t>(effect + 12, hit.y);
    if (caller != 0x21c515c) {
        st<int32_t>(effect + 24, hit.x);
        st<int32_t>(effect + 28, hit.y);
    }
}

// The shovel surveys a 3x3 neighborhood. Each tile has four resolved item
// records, but the native occupancy mask includes only even/even anchors.
// Promote a single buried footprint under the shovel to its ordinary native
// dig path. The native action still owns removal, inventory and hole creation.
void select_buried_target(CPU& c) {
    const auto sp = c.x[31];
    const int x = static_cast<int>(c.x[26]), y = static_cast<int>(c.x[27]);
    const int col = x - ld<int32_t>(sp + 0x210), row = y - ld<int32_t>(sp + 0x214);
    if (static_cast<unsigned>(col) >= 3 || static_cast<unsigned>(row) >= 3) return;
    const unsigned slot = static_cast<unsigned>(row * 3 + col);
    const auto mask = ld<uint32_t>(sp + 0xc8);
    if (mask & (1u << slot)) return;
    const auto records = c.x[22] + slot * 32;
    // Native removal starts at the tile's first half-cell. A footprint
    // touching only its far edge cannot be consumed from this aim.
    if (ld<uint16_t>(records) == 0xfffe) return;
    uint64_t selected = 0, anchor = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto record = ld<uint64_t>(records + i * 8);
        if (static_cast<uint16_t>(record) == 0xfffe) continue;
        if (!(record & 0x40000) || (selected && record != selected)) return;
        // 15e1014 resolves a half-cell hit to the layer's anchor record.
        // Equal payloads on two adjacent fossils still have distinct roots.
        const auto hit = field_hit(c, x * 2 + (i == 1 || i == 2), y * 2 + (i >= 2));
        if (!hit.record || ld<uint64_t>(hit.record) != record || (anchor && anchor != hit.record)) return;
        selected = record;
        anchor = hit.record;
    }
    if (selected) {
        st<uint64_t>(records, selected);
        st<uint32_t>(sp + 0xc8, mask | (1u << slot));
    }
}

void buried_target(CPU& c) {
    static const bool trace = std::getenv("NX_ACNH_SHOVEL_SURVEY_TRACE") != nullptr;
    static std::atomic<unsigned> samples{0};
    if (!trace || samples.fetch_add(1, std::memory_order_relaxed) >= 16) {
        select_buried_target(c);
        return;
    }

    const auto stack = c.x[31];
    const auto x = static_cast<int32_t>(c.x[26]);
    const auto z = static_cast<int32_t>(c.x[27]);
    const int64_t column = int64_t{x} - ld<int32_t>(stack + 0x210);
    const int64_t row = int64_t{z} - ld<int32_t>(stack + 0x214);
    if (column < 0 || column >= 3 || row < 0 || row >= 3) {
        select_buried_target(c);
        return;
    }

    const auto slots = c.x[22] + (3 * row + column) * 32;
    const auto first = ld<uint64_t>(slots);
    const auto second = ld<uint64_t>(slots + 8);
    const auto third = ld<uint64_t>(slots + 16);
    const auto fourth = ld<uint64_t>(slots + 24);
    const auto mask = ld<uint32_t>(stack + 0xc8);
    select_buried_target(c);
    log("acnh shovel survey x=%d z=%d before=%x after=%x records=%llx,%llx,%llx,%llx selected=%llx",
        x, z, mask, ld<uint32_t>(stack + 0xc8),
        static_cast<unsigned long long>(first), static_cast<unsigned long long>(second),
        static_cast<unsigned long long>(third), static_cast<unsigned long long>(fourth),
        static_cast<unsigned long long>(ld<uint64_t>(slots)));
}

// The save layer removes continuations correctly, but the field actor manager
// refreshes only the shovel's whole-tile position. Remember the mark across
// the native erase call so its original actor can be refreshed as well.
thread_local FieldHit buried_mark{};
void remember_mark(CPU& c) {
    buried_mark = {};
    const auto pos = c.x[1];
    const auto hit = field_hit(c, ld<int32_t>(pos + 8), ld<int32_t>(pos + 12));
    if (hit.record && ((hit.x | hit.y) & 1) && (ld<uint32_t>(hit.record) & 0x40000))
        buried_mark = hit;
}

void refresh_mark(CPU& c) {
    const auto hit = buried_mark;
    buried_mark = {};
    if (hit.record) refresh_field_cell(c, hit.x, hit.y);
}

// The native button probe casts three rays. Test the real center first.
// A glancing approach can also reach the stock contact point if that point
// lies on this same trunk. Run the native clearance checks for both points.
enum class Probe { first, can_retry, retrying };
thread_local Probe probe = Probe::first;
void start_probe(CPU&) { probe = Probe::first; }
void missed_probe(CPU& c) {
    if (probe == Probe::can_retry) {
        c.x[25] -= 4; // the next instruction advances this ray index by four
        probe = Probe::retrying;
    } else {
        probe = Probe::first;
    }
}

void shake_aim(CPU& c) {
    const auto sample = c.x[29] - 0x58;
    const auto world_x = ld<float>(sample), world_y = ld<float>(sample + 8);
    if (!(world_x >= 0 && world_x < 1280 && world_y >= 0 && world_y < 1280)) return;
    const int x = static_cast<int>(world_x / 5.f), y = static_cast<int>(world_y / 5.f);
    const auto hit = fractional_trunk(c, x, y);
    if (!hit.record) return;
    const int old_x = static_cast<int>(c.v[12].f32[0] / 5.f);
    const int old_y = static_cast<int>(c.v[13].f32[0] / 5.f);
    if (old_x >= hit.x && old_x <= hit.x + 1 && old_y >= hit.y && old_y <= hit.y + 1 &&
        (old_x != hit.x + 1 || old_y != hit.y + 1)) {
        if (probe == Probe::retrying) return;
        probe = Probe::can_retry;
    }
    c.v[12].f32[0] = (hit.x + 1) * 5.f;
    c.v[13].f32[0] = (hit.y + 1) * 5.f;
}

void shake_contact(CPU& c, uint64_t selected) {
    const int x = ld<uint8_t>(selected), y = ld<uint8_t>(selected + 1);
    if (!fractional_trunk(c, x, y).record) return;
    c.v[8].f32[0] = x * 5.f;
    c.v[9].f32[0] = y * 5.f;
}
void shake_approach(CPU& c) { shake_contact(c, c.x[21]); }
void shake_pose(CPU& c) { shake_contact(c, c.x[20]); }

// x20 holds the two half-cell coordinates selected by the action.
// Both visual notifications use an inclusive one-cell actor search rectangle.
void shake_target(CPU& c) {
    const int x = ld<uint8_t>(c.x[20]), y = ld<uint8_t>(c.x[20] + 1);
    const auto hit = fractional_trunk(c, x, y);
    if (hit.record) {
        c.x[9] = hit.x;
        c.x[10] = hit.y;
    }
}

}  // namespace

NX_HOOK(0x23e0808, shake_target);
NX_HOOK(0x21c128c, plant_offset);
NX_HOOK(0x21c303c, placement_offset); // 1950ae4 sets the burial flag on the pocket item
NX_HOOK(0x21c12ac, refresh_plant);
NX_HOOK(0x21c3044, refresh_burial);
NX_HOOK(0x23dff18, shake_aim);
NX_HOOK(0x23dfcec, start_probe);
NX_HOOK(0x23dfd14, missed_probe);
NX_HOOK(0x23e0394, shake_approach);
NX_HOOK(0x23e0698, shake_pose);
NX_HOOK(0x10e8d00, tree_effect);
NX_HOOK(0x23e0f50, shake_target);
NX_HOOK(0x21f7618, buried_target);
NX_HOOK(0x21f9918, remember_mark);
NX_HOOK(0x21f991c, refresh_mark);
NX_HOOK(0x31c3fa8, gui_touch);     // guiTouchManager::update, before its hit tests
NX_HOOK(0x3c48c70, focus_input);   // focus manager input: a click on a list item
NX_HOOK(0x3c4b20c, button_input);  // button handler input: a click while a message waits

}  // namespace nx
