#include "version_303.h"
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nx::freecamera {
constexpr float pi = 3.14159265358979323846f;
enum class CameraSpace { other, world, drum };
inline CameraSpace camera_space(uint64_t caller) {
    // The alternate drum branch builds a temporary WORLD camera on the stack.
    // Both branches must receive the same orbit as the main world camera.
    if (caller==camera303::offset_292cb0c || caller==camera303::offset_374b944 || caller==camera303::offset_374b954 || caller==camera303::offset_7a88a4)
        return CameraSpace::world;
    if (caller==camera303::offset_7a8830) return CameraSpace::drum;
    return CameraSpace::other;
}
struct Bounds {
    float left, right, near, far;
};
inline float snap_axis(float value) {
    if (std::fabs(value) < 1e-6f) return 0;
    if (std::fabs(value-1) < 1e-6f) return 1;
    if (std::fabs(value+1) < 1e-6f) return -1;
    return value;
}
inline Bounds rotate_bounds(Bounds b, float yaw) {
    float xmin = 0, xmax = 0, zmin = 0, zmax = 0;
    const float c = snap_axis(std::cos(yaw)), s = snap_axis(std::sin(yaw));
    for (float x : {-b.left, b.right}) for (float z : {-b.far, b.near}) {
        float rx = c*x+s*z, rz = -s*x+c*z;
        xmin = std::min(xmin, rx); xmax = std::max(xmax, rx);
        zmin = std::min(zmin, rz); zmax = std::max(zmax, rz);
    }
    return {-xmin, xmax, zmax, -zmin};
}
inline void movement(float yaw, float x, float y, float& result_x, float& result_y) {
    result_x = std::cos(yaw)*x - std::sin(yaw)*y;
    result_y = std::sin(yaw)*x + std::cos(yaw)*y;
}
struct Orbit {
    float yaw = 0, pitch = 0, zoom = 1;
    void advance(float horizontal, float vertical, float distance, float dt) {
        dt = std::clamp(dt, 0.0f, 0.05f);
        yaw = std::remainder(yaw + horizontal * dt * 1.8f, 2 * pi);
        pitch = std::clamp(pitch + vertical * dt, -pi / 2, pi / 2);
        zoom = std::clamp(zoom * std::exp(distance * dt), 0.25f, 3.0f);
    }
    bool identity() const { return yaw == 0 && pitch == 0 && zoom == 1; }
    bool apply(const float pos[3], const float at[3], float result[3]) const {
        for (int i = 0; i < 3; i++) if (!std::isfinite(pos[i]) || !std::isfinite(at[i])) return false;
        float x = pos[0] - at[0], y = pos[1] - at[1], z = pos[2] - at[2];
        float radius = std::sqrt(x*x + y*y + z*z);
        if (!(radius > 1 && radius < 10000)) return false;
        float azimuth = std::atan2(x, z) + yaw;
        float elevation = std::clamp(std::asin(std::clamp(y / radius, -1.0f, 1.0f)) + pitch, 0.05f, 1.48f);
        float horizontal = radius * zoom * std::cos(elevation);
        result[0] = at[0] + horizontal * std::sin(azimuth);
        result[1] = at[1] + radius * zoom * std::sin(elevation);
        result[2] = at[2] + horizontal * std::cos(azimuth);
        return true;
    }
};
// Right stick orbits during outdoor play. Hold its button (or F6) to zoom
// instead of tilting; a click without a zoom gesture recenters on release.
struct Controls {
    bool pressed = false, dragged = false;
    void cancel() { pressed = dragged = false; }
    void update(Orbit &orbit, bool active, bool reset, bool zoom_key, bool stick_button, float x, float y, float dt,
                float reset_zoom = 1, float orbit_speed = 1, float zoom_speed = 1, bool invert_vertical = false) {
        if (!active) { cancel(); return; }
        if (stick_button && !pressed) dragged = false;
        if (stick_button && (x != 0 || y != 0)) dragged = true;
        const bool click = pressed && !stick_button && !dragged;
        pressed = stick_button;
        if (reset || click) {
            orbit = {};
            orbit.zoom = std::clamp(reset_zoom, .25f, 3.f);
            return;
        }
        const bool zoom = zoom_key || stick_button;
        orbit.advance(x * orbit_speed, zoom ? 0 : y * orbit_speed * (invert_vertical ? -1.f : 1.f),
                      zoom ? -y * zoom_speed : 0, dt);
    }
};
}
