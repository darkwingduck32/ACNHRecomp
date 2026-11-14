# 360 camera runtime

Enable `freecamera` in the Mod Manager and restart the game. The runtime currently
recognizes the ACNH 3.0.3 camera implementation by instruction fingerprints.

## Controls

During outdoor play the right stick (I/J/K/L with default keyboard bindings)
orbits immediately. Movement follows the camera direction. There is no camera
editing mode to enter or exit.

- Hold R3 and move the right stick vertically to zoom. Release to keep that zoom.
- Click and release R3 without moving the stick to recenter.
- On keyboard, hold F6 while using I/K to zoom; F7 recenters.
- The D-pad, L3, shoulders, triggers, and action buttons retain their game bindings.

The camera retains its orientation when leaving outdoor play. Active layout
cursors, dialogue page handlers, and text entry suspend camera input and movement
rotation; the regular game input then passes through. These signals do not yet
prove every overlay or cutscene is covered. Outdoor/menu/indoor transitions require
live verification before treating the control integration as complete.

## Visibility work in progress

Foreground/furniture loading and the terrain rectangle now use the perspective
frustum intersected with a conservative terrain/canopy height band. The footprint
follows yaw, pitch, zoom and aspect ratio, retains a small neighborhood around the
player, and respects each range's distance budget. Extension fields remain padding.
The engine still needs to reject individual objects outside the view; this is a
broad-phase loading rectangle, not a replacement per-object culler. Without a
recent valid projection it falls back to rotated native bounds. The existing
camera303 terrain scale remains a separate local experiment in that fallback.

Live inspection identified camera303's field projection as 31 degrees with a
100000-unit far plane. Its UI and minimap projections are excluded. Analytical
ground-plane checks and camera-space sample sweeps cover yaw, pitch, zoom, aspect,
and ground/canopy height. The corrected projection filter still needs a live
reload and coverage/performance comparison. The height band is conservative for
ordinary terrain; compatibility with the game's distant drum deformation remains
part of the grounding investigation.

The local parameter investigation identified `Param/Gfx/ActorDistantField.byml`
with `mDrumOffsetYOffset.f32`, `mDrumOffsetYScale.f32`, `mYScalePeakOffset.f32`,
`mRotateYParam.f32`, and `mScale.f32`. The matrix function at main+0x60acd4
combines drum rotation and a quadratic vertical offset with camera-relative
placement; its relationship to foreground terrain still needs validation before
changing distant-scenery placement. Fog palette parameters exist separately;
increasing fog alone does not establish lower rendering cost or correct LOD.

Both field-camera branches now receive the orbit, including the temporary
stack-based LookAtCamera returned to main+0x7a88a4. Previously only the heap camera
in the other branch was adjusted, leaving an alternate world/drum matrix in the
original orientation. The temporary object's writable memory span is checked and
its inputs are restored after matrix generation, as for heap cameras. This fix
compiles and its scope classification is tested; its visual effect is unverified.

The view-relative fog adapter targets Park UBER's vertex dark-fog plane. Its host
uniform upload replaces the plane normal with negative view Z of the same length,
retaining colour, intensity, bias and inverse range. Guest constants are untouched;
compute-written blocks, other shaders and inactive outdoor cameras are excluded.
`tools/build_camera_shader_profile.py` generates the private shader hash profile;
install its `view_fog_shaders.txt` into `mods/freecamera/` with the matching runtime.
Without the profile the adapter leaves native fog intact. The shared layout was
identified in a decompiled Park UBER shader; every variant and native onset have
not yet been visually verified. Directional math tests are not visual acceptance.
Consistent fog in forward, reverse, and side views remains a live acceptance gate.

The native SkyScatter shader generates a spherical latitude/longitude atmosphere
texture. DrawCloud composites a screen-space cloud image; DrawStar transforms
points through separate model, view and projection matrices. These are distinct
passes, so changing the atmosphere generator alone cannot establish a 360-degree
sky. A live draw trace identifies the displayed background as Park UBER VR vertex
`fce7966c1f0b7193` / fragment `d579b8327391ea0a`, sampling SkyScatter's target.
It draws ParkVR's 800-triangle full sphere, with radius 2000 in model units.
The captured model scale was 0.3, with center (524.041, -75.8444, -1166.58),
while the orbit eye was (381.887, 59.4954, 5.80881): outside the 600-unit sphere.
The native sky placement therefore does not follow the modified view.

The host uniform adapter now centers this main-view sphere at the eye recovered
from its rigid view matrix. It preserves model rotation/scale, materials and guest
constants, and excludes the environment-probe shader. Rigid-matrix checks reject
invalid inputs. The DrawStar pass uses exactly the same stale center in its model matrix;
its host c5 upload now receives the eye recovered from its packed view matrix too.
The matching 800-triangle depth-only pass (`3ebba4ffae3ffdd3`) uses the same
model/view layout and now follows the same center, keeping colour and depth aligned.
Unit tests verify center recovery and zero view-space position at four headings;
visual sky/star coverage and the separately transformed cloud models still need
validation. The generic AGL full-screen sky passes were absent from this
capture and are not patched.

Validation so far: camera/gesture unit tests and compilation of the affected
runtime, SDL input, and game UI hooks. Live playability, frustum coverage,
distant-scene grounding, LOD transitions, and performance remain open gates.

## Low-angle performance investigation

Existing runtime samples (without another live capture) show about 31 FPS with
an 814,000-square-unit loading rectangle at 15.8 degrees elevation, versus 60 FPS
with a 198,000-square-unit rectangle at 37.8 degrees. These are different views,
not a controlled benchmark; they establish a workload correlation, not causality.
The detailed draw trace caused an additional transient slowdown and was cancelled.

The native terrain worker at main+0x11fc610 enumerates the rectangle from manager
fields +0x1a28/+0x1a38. Its per-tile path main+0x146a660 already bends the tile's
bounding sphere using the drum parameters, then calls the frustum test at
main+0x63e274 before passing visibility flags to main+0x146ab80. Therefore replacing
this with a flat-world frustum test, or skipping tile updates outright, is not yet
justified: it could lose curved terrain or leave prior-frame visibility stale.
The next performance check must distinguish enumeration cost, accepted geometry,
and shadows, and verify that these native culling planes follow the orbit camera.
No LOD or outline-mode performance improvement is claimed yet.

Read-only live inspection of the active terrain frustum at scene-camera slot 0
(main global +0x5131900, scene +0x58, slot +0x138) confirms its rotation follows
the orbit at the inspected pose. The world eye (945.7, 103.9, 810.0) corresponds
to culling eye (945.672, 103.928, 64.868); the Z difference is the target/drum
offset 745.1. The view back-axis agrees with normalized eye-minus-target within
0.001 per component. UI/minimap slots have distinct projections. This rules out
an unchanged forward-facing terrain frustum for this pose only; it does not prove
all transitions or other object families. The native tree archives inspected
contain growth/animation/stump models rather than separately named distant LODs.

Windows replay CPU timing previously returned zero unconditionally. A shared
thread CPU clock now uses GetThreadTimes on Windows and the existing POSIX clock
where supported. The standalone test measured 109.38 ms for CPU work and 0 ms
charged during a 150 ms sleep. This repairs diagnosis, not frame rate itself.
Existing zero-CPU log entries must not be used to infer a GPU bottleneck. The
capture also includes 5760x3240 targets; the saved configuration requests 2160p
with 2x supersampling. A controlled CPU/wall comparison is still required before
choosing between submission, detail-level, shadow and pixel-cost optimizations.
