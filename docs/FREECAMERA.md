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

## Prepared live validation

The existing `NX_INPUT`/`input_live` harness now accepts `RUp`, `RDown`, `RLeft`,
`RRight`, `StickL` and `StickR`. Right-stick commands pass through normal camera
input, including UI gating and click/hold gesture handling. Ordinary input is
unchanged when no script is supplied.

`tools/play/scenarios/freecamera_sweep.txt` is a stationary outdoor low/high-angle
sweep with settling periods and a final reset. It must start with outdoor control
and no dialogue/menu, in an isolated save copy. Generate it with `tools/play/gen.py`
for inspection before executing it through the live harness. Camera rotation uses
wall time, so actual captured headings must be checked rather than assuming a
frame count always corresponds to 90 degrees. The harness has now run low-angle
orbit sweeps, inventory input, walking and repeated tent transitions. The complete
prepared high/low scenario, zoom, weather and geometry-grounding tests remain open.

## Windows harness and performance validation (2026-11-15)

Live tests used `private/freecamera/perf-test`, an isolated copy of the user's
save with the same freecamera/camera303 assets and 2160p, SSAA2, DLAA, 60 FPS
configuration. No detailed draw tracing was enabled. The Windows harness now
queries process liveness with GetExitCodeProcess (Python os.kill(pid, 0) can
terminate a Windows process), replaces its frame-counter file correctly, accepts
native paths and converts captures with Pillow. Two harness regression tests and
the freecamera/thread_time CTests pass.

Frame readback now prefers CPU-cached coherent memory, with a coherent fallback.
For 5760x3240 captures, the 60-frame timing window's present time fell from about
91 ms/frame to 1.2–1.6 ms/frame: roughly 5.5 seconds of capture overhead became
0.06–0.08 seconds. Ordinary gameplay performance is measured separately.

At the same stationary inland pose (eye 445,19.5,1129.6; target 445,12,979.8),
16 baseline and 17 dynamic-UBO timing windows, excluding captures, measured:

| Metric | Baseline | Dynamic UBO |
| --- | ---: | ---: |
| Median FPS | 30.4 | 34.9 |
| Median CPU replay | 30.86 ms | 26.56 ms |
| Median replay wall time | 31.44 ms | 27.18 ms |
| Descriptor sets per submit | about 11,400 | about 270 |

The existing dynamic-UBO path is now the default for ACNH on Windows/NVIDIA;
`NX_DYNAMIC_UBO=0` retains the diagnostic fallback. Other Windows games and GPU
vendors retain their prior defaults. Logs and samples are in the isolated root's
`cached-readback.log`, `dynamic-ubo.log` and `dynamic-comparison.json`.

Nighttime orbit captures show sky and stars across the sampled headings. The
inventory kept the camera stationary under right-stick input. Indoor testing
found that the supposed field look-at sites also run in rooms. Camera input,
matrix modification, sky and fog adaptation now additionally require the recent
populated island terrain update. This preserves native room controls and resumed
outdoor orbit on two tent round trips, including return with cached resources.
Evidence is in `terrain-gating.log` and `shots/terrain-*.png`.

Remaining limitations: low-angle inland rendering is still CPU-bound and below
60 FPS on this machine; close trees can obstruct the camera; distant grounding,
LOD/outline behavior, daytime/cloud coverage and other interior types need further
validation. These tests do not establish that the whole 360-camera task is complete.

Daylight testing identified the main CloudVR vertex shader `0f62b0e3657eeab0`
(fragment `7d6cc12c1cda411a`); its c4 model translation now follows the same eye
recovery as the sky. The `defb2a20eda77287` environment-probe variant remains
unchanged. The low-angle forward capture no longer shows the broken cloud dome.
The rotation capture still exposes stretching in the native cloud UV mapping at
side/back headings, and a dark horizon band beyond the finite ocean. These are
open visual gates. Captures: `perf-test/shots/cloud-main-low_03781.png` and
`cloud-360-small.png`; the native CloudVR front/back meridians have substantially
different UV coverage at equal elevation, so centering alone cannot fix that.

`tools/build_camera_clouds.py` builds a separate private CloudVR overlay with
panoramic UV0 coverage. It interpolates the native front meridian's vertical
profile around every heading and respects the existing split back seam. Both
410-vertex/696-triangle shapes remain unchanged geometrically. Byte-level
validation found all 2070 changed bytes inside UV0 fields; positions, indices,
materials, texture references and archive length are preserved. The daylight
`cloud-uv-orbit-small.png` capture shows consistent cloud shapes through the
sampled headings. Other weather palettes remain a validation gate. This overlay
is currently tested through an isolated `cloudtest` mod, ahead of freecamera.

Further daylight isolation separated two horizon defects. Hiding only cloud FS
`7d6cc12c1cda411a` removes the diagonal bright wedge at an unchanged pose
(`horizon-no-cloud-side_11641.png` versus `horizon-cloud-restored_12001.png`).
CloudVR also uses UV1 for its horizon layer. The builder now maps each channel
using its own native front-meridian vertical profile. All 4482 changed BFRES
bytes are inside UV0/UV1; all other bytes remain identical. The isolated overlay
hash is `f63e135dfa970fae2a9fa332bb0870d8939b0ec86a42f746f149f1f9c41207da`.

With both UVs corrected, `dual-uv-orbit-small.png` exposes a separate dark sky
band. Native main-sky c6[7].x is a roughly -5.6-degree lookup tilt, which depresses
the horizon forward but raises it behind. A field-only host UBO copy replaces
that tilt with an equal vertical lookup offset, using ParkVR's model scale and
retaining its existing height offset. The source guest block is unchanged.
`combined-orbit-small.png` shows the cloud wedge and dark sky band removed at
the sampled low-angle headings. The horizon math test checks equal depression
around the sphere and rejection of unexpected scales/tilts. Native weather and
sun parameters remain unchanged, but other weather conditions need live checks.

`combined-wide-small.png` tests a larger orbit distance. It exposes missing
terrain under still-visible trees/grass and beyond the water mesh, plus a visible
cloud-horizon strip/seam. These remain open gates; the sky fixes do not establish
that wide-distance terrain visibility or the full camera mod is complete.
The installed `acnh.exe` remains the measured performance/interior-gating build;
these sky changes are currently in `acnh-fruit.exe` and the isolated cloud overlay.

Wide-zoom investigation: `tools/play/scenarios/freecamera_wide.txt` reproduces
missing terrain under visible grass and trees. Two dispatch-table experiments
attempted to update the native curved-horizon camera and bypass tile culling.
Neither produced a demonstrated fix. Crucially, the instrumented culling probe
registered but never executed (`terrain-no-cull-verified.log`): these native
calls are direct compiled C++ calls (`recomp/emit.py::i_bl`), so changing the
indirect dispatch table does not intercept them. The screenshots cannot rule
out the horizon/frustum tests. Both experimental runtime wrappers were removed.
Further probing must use compiled instruction hooks or intercept a verified
indirect caller; do not repeat dispatch replacements at these direct callees.

The isolated instruction-hook rebuild subsequently confirmed execution of the
terrain culler probe at `146a660` from `11fd40c` (log
`terrain-instruction-bypass.log`). With that probe active, the wide rotation
still has the ground holes (`instruction-no-cull-wide-small.png`). Native land
worker `11fcb00` consumes its own bounds at job+e0/f0. Unlike water worker
`11fc610`, it does not consume the already-patched manager+1a28/1a38 rectangle.
Land submission `11fdbf8` calculates a fixed roughly 30x51-tile region and stores
it at `11fe014`/`11fe01c`, before resetting the work index and submitting jobs.

An isolated hook at `11fe020` replaced those land bounds with the foreground
perspective footprint. It executed, but the first expanded selection stalled
at frame 241 and the process memory guard stopped it at 33.30 GB
(`land-fov-selection.log`). This is a failed experiment, not an accepted fix;
its runtime changes were removed. Do not raise the memory guard to accommodate
it. Further work must investigate allocation/culling for the added land tiles
and bound their work before any release. The experimental source and generated
library remain private; the normal installed executable/library are unchanged.

A subsequent gameplay-only land probe identified two additional issues. Direct
unwrapped temporary world cameras overwrote the orbit visibility snapshot with
the stock eye. Computing the orbit snapshot for these calls (without changing
their native camera objects) produces the expected larger land window. The
low-angle test remained stable at 7,768 queued terrain instances. A wide orbit
then reached 10,210 instances against the native 10,000 capacity; an external
monitor stopped that isolated process (`land-snapshot-counts.json`, frame 2985).

Native enqueue `146ab80` increments manager+558, compares it with capacity+510,
and falls back to array entry zero on overflow. This is a concrete capacity
violation and a plausible cause of the earlier memory spike, not yet proof of
that spike's entire cause. Capacity comes from manager+6e8 before both CPU and
GPU allocation. An isolated `1462bb8` instruction hook is testing 40,000 entries
at allocation time; modifying only the live capacity would be unsafe. Neither
the expanded land selection nor this capacity change is released yet. Selection
cost, wide-orbit coverage and transitions remain required checks.

The 40,000-entry allocation probe completed the wide rotation with a monitored
peak of 10,269 entries and about 6.4 GB private process memory. Captures in
`land-capacity-wide-small.png` show ground beneath the trees at headings where
the earlier wide captures showed holes. The runtime now computes an orbit
snapshot for temporary cameras, and the official 3.0.3 hook list includes both
land selection and the pre-allocation capacity adjustment. Rebuild the guest
library when using these changes; the imported original DLL cannot execute the
new instruction hooks. The allocation hook changes only the known 10,000-entry
setting while the free-camera mod is enabled. Land selection preserves the
native identity view and rejects unexpected job dimensions.

This is a correctness checkpoint, not a performance release. The sampled wide
pose is roughly 23 FPS; broader land selection costs additional work. The ocean
mesh edge and cloud horizon strip remain visible. Full interior-transition,
weather and selection/LOD performance verification remain open. The user's
installed executable and game DLL are unchanged. Local `acnh-fruit.exe` and the
isolated rebuilt DLL were used for these captures; the diagnostic DLL also has
three unused culling/horizon probe hooks not present in the official hook list.

Instancing investigation: the existing `NX_DRAW_MERGE=1` path reduced the matched
low-angle terrain pose from median 53.12 ms CPU replay (4 windows) to 15.62 ms
(38 windows). Rendered FPS rose from 17.9 to the isolated test's 30 FPS ceiling.
Both runs used the same orbit eye (445, 19.5, 1129.6), target (445, 12, 979.8),
2160/SSAA2/DLAA configuration, and expanded land window. The capacity allocation
was 10,000 in the earlier baseline and 40,000 in the merged run; the selected
low-angle instances fit both. The runs were not simultaneous and daylight/wind
advanced. Evidence: `merge-comparison-30hz.json`, `merge-actual-low_03181.png`,
and `merge-wide-small.png`. The latter preserves the observed land coverage;
the pre-existing ocean-edge and cloud-horizon defects are still visible.

The isolated test folder had lost its SpeedFix mod junction when its mods were
split from the normal root, and the diagnostic DLL was generated without the
SpeedFix variant. Consequently `frame_rate=60` stayed at native 30 Hz. These
terrain-probe FPS figures are not directly comparable to the earlier installed
60 Hz baseline. Restoring the existing parameter overlay and generating the
matching pinned SpeedFix variant is required before 60 Hz acceptance. The
runtime's normal protection against running uncorrected game logic at double
speed must remain enabled.

The matched 60 Hz rerun restored the SpeedFix mod junction and rebuilt the guest
DLL with the pinned 3.0.3 variant (commit e5e03e64739334c9fbd36f19658b98e805f8ba8d,
6,417 words; 1,545 changed functions and 866 added routines). Logs confirm
`variant on`, present interval 1 and the SpeedFix RomFS overlay in both runs.
Both used the same executable, rebuilt DLL, copied save and scripted low pose;
only `NX_DRAW_MERGE=0` versus the new default differed. Sixteen stable timing
windows per run, excluding startup, camera movement and screenshots, measured:

| Metric | Merging off | Windows/NVIDIA ACNH default |
| --- | ---: | ---: |
| Median rendered FPS | 18.9 | 56.4 |
| Median CPU replay | 50.78 ms | 15.23 ms |
| Median replay wall time | 51.09 ms | 15.785 ms |

Evidence: `merge-comparison-60hz.json`, `orbit-merge-off60.log`,
`orbit-merge-on60.log` and matching `shots/orbit-merge-{off,on}60_03301.png`.
The sampled captures retain geometry, materials and lighting. The merger keeps
draw order and excludes individual object-motion draws. It now also rejects
variable blocks requiring host-side free-camera fog/sky adaptation, since raw
guest copies would otherwise bypass that adaptation. This guard was enabled in
the measured default run. Other Windows vendors/games and parallel replay keep
their existing defaults; `NX_DRAW_MERGE=0` remains the comparison fallback.

With the guarded default, the inventory, tent interior (60 FPS), exit and
outdoor orbit passed a live round trip. Captures: `merge-inventory_04981.png`,
`merge-tent-inside_06721.png`, `merge-tent-exit_09361.png` and
`merge-return-orbit-small.png`. This is one interior type and daylight condition.
It does not establish weather/other-interior coverage, resolve the ocean edge or
cloud seam, or replace the remaining distant-detail/grounding investigation.
The normal installed executable/library still remain unchanged.

The remaining diagonal cloud band came from CloudVR's model basis, not another
UV seam. A two-draw shader-filtered capture at frame 2300 recorded the main
model Y/Z basis as (0, .991868, .127272) / (0, -.127272, .991868), about a
7.3-degree pitch toward the native camera (`cloud-transform-trace.log`). Its
front equator was lowered while the back equator was raised above the horizon.
The main-view cloud host upload now levels that basis, preserves heading and
uniform scale, and lowers its center by the native equator depression (CloudVR
radius about 525.18). The environment-probe shader is excluded. Unexpected
nonuniform, reflected or steeply tilted matrices remain unmodified.

The math test checks that the old front equator height is retained through a
full circle at multiple headings/scales. Live daylight and 5 PM wide-orbit
captures show the diagonal band removed while cloud shapes and the evening
palette remain visible: `cloud-level-wide-small.png` and
`cloud-level-evening-wide-small.png`. Storm/snow/cloudless conditions remain
untested. The wide ocean near edge is still exposed independently of this fix.
The normal installed game remains unchanged.
