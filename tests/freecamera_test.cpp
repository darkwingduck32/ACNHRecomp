#include "freecamera_math.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
    using namespace nx::freecamera;
    Mode mode;
    const Bounds original_bounds{140, 140, 60, 265};
    const Bounds quarter = rotate_bounds(original_bounds, pi/2);
    check(std::fabs(quarter.left-265)<.001f && std::fabs(quarter.right-60)<.001f &&
          std::fabs(quarter.near-140)<.001f && std::fabs(quarter.far-140)<.001f,
          "Quarter-turn visibility must transfer forward distance to the world X axis");
    const Bounds half = rotate_bounds(original_bounds, pi);
    check(std::fabs(half.near-265)<.001f && std::fabs(half.far-60)<.001f,
          "Half-turn visibility must look behind the original camera");
    float mx, my;
    movement(pi/2, 0, 1, mx, my);
    check(std::fabs(mx+1)<.0001f && std::fabs(my)<.0001f, "Camera-relative up at 90 degrees must move world-left");
    movement(pi, 1, 0, mx, my);
    check(std::fabs(mx+1)<.0001f && std::fabs(my)<.0001f, "Camera-relative right at 180 degrees must move world-left");
    movement(0, .3f, .7f, mx, my);
    check(std::fabs(mx-.3f)<.0001f && std::fabs(my-.7f)<.0001f, "Stock camera must preserve movement");
    mode.update(true, true);
    check(mode.editing, "Dedicated shortcut must activate immediately");
    for (int i = 0; i < 100; i++) mode.update(true, true);
    check(mode.editing, "Held shortcut must not repeatedly toggle");
    mode.update(false, true); mode.update(true, true);
    check(!mode.editing, "Second shortcut press must exit camera controls");
    mode.update(false, true); mode.update(true, false);
    check(!mode.editing, "Disabled mod must not take over controls");
    float pos[] = {0, 50, 100}, at[] = {0, 0, 0}, out[3];
    Orbit o;
    o.yaw = pi;
    check(o.apply(pos, at, out) && std::fabs(out[0]) < .001f && std::fabs(out[2]+100) < .001f,
          "Half orbit must reveal the opposite side of the target");
    o.yaw = 2*pi;
    check(o.apply(pos, at, out) && std::fabs(out[2]-100) < .001f, "Full orbit must return to start");
    o.zoom = .5f;
    check(o.apply(pos, at, out) && std::fabs(out[1]-25) < .001f && std::fabs(out[2]-50) < .001f,
          "Zoom must scale distance without changing target");
    o = {};
    bool left = false, right = false, behind = false;
    for (int i = 0; i < 1000; i++) {
        o.advance(1, 0, 0, .01f);
        check(o.apply(pos, at, out), "Continuous orbit must remain valid across wraps");
        left |= out[0] < -90; right |= out[0] > 90; behind |= out[2] < -90;
    }
    check(left && right && behind, "Orbit must cross all four quadrants");
    const float moving_at[] = {123, 10, -456}, moving_pos[] = {143, 70, -356};
    for (int i = 0; i <= 360; i++) {
        o = {}; o.yaw = i*pi/180;
        check(o.apply(moving_pos, moving_at, out), "Offset orbit must remain valid");
        const float dx = out[0]-moving_at[0], dz = out[2]-moving_at[2];
        check(std::fabs(dx*dx+dz*dz-10400) < .1f && std::fabs(out[1]-70) < .001f,
              "Yaw must preserve horizontal radius and height at every angle");
    }
    for (int i = 0; i < 500; i++) o.advance(0, 1, -1, .05f);
    check(o.zoom == .25f && o.apply(pos, at, out) && std::isfinite(out[2]), "Pitch and zoom limits must remain finite");
    float invalid[] = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    check(!o.apply(invalid, at, out) && !o.apply(at, at, out), "Reject invalid and degenerate views");
    o = {};
    check(o.identity(), "Reset must restore the unmodified camera");
    std::puts("freecamera: orbit, wrap, zoom, limits, invalid input and reset passed");
}
