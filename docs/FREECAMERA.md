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
retaining colour, intensity and inverse range. Its bias compensates for the
change in eye-to-player distance when zooming, keeping the ramp anchored to the
player. Guest constants are untouched;
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

The first ocean-edge asset probe extended ten `FldOut*SeaOnly` water/back-face
meshes beyond their native 240-unit outer limit, preserving the inner 80 units,
indices and materials. A byte audit limited changes to positions and culling
bounds/radii. It did not change the visible gap (`ocean-extended-forward_02881.png`).
A subsequent file-open trace explains why: this scene loads `WaterModel` and the
seabed/coast archives, but none of those `SeaOnly` assets. The unused `oceantest`
overlay is disabled. Do not promote this failed probe as an ocean fix.

`NX_TRACE_VERT_COUNT=<n>` now narrows an existing bounded `NX_TRACE_DRAWS` capture
to draws with that vertex/index count, alongside the existing frame, target and
fragment-shader filters. With 4 vertices and a count limit of 24, the harness
recorded exactly 24 matching draws (`water-quad-trace.log`), including 10-unit
water-preparation tiles. A main-target capture is needed to distinguish those
from the actual sea surface. Tracing still disables fast replay; these diagnostic
runs are not performance measurements.

The larger main-target capture (`water-main-coverage.log`, 1,200 four-vertex
draws) identifies the c2255355903b4a5e / 587e1cd12261624a pair as 10-unit
water tiles at inland coordinates, alongside particle quads. Disabling that
fragment shader does not remove the visible ocean (`water-main-comparison.jpg`).
Do not extend those tiles or the unused SeaOnly models as a presumed ocean fix.

The verified terrain capacity/coverage, guarded draw merging and panoramic
cloud fixes have now been promoted to the normal local installation. The
runtime and SpeedFix-capable official-hook library match the isolated test
copies byte-for-byte; CloudVR is installed directly in the existing freecamera
overlay, so no separate cloudtest mod is required. Backups and SHA-256 hashes
are recorded in `private/freecamera/install-backups/20261115-062356/manifest.json`.
The user's save and video configuration were not modified. The failed oceantest
overlay remains disabled. The ocean edge, distant LOD/grounding and broader
weather coverage remain open; this installation is not completion of those
acceptance gates.

The installed executable also passed a fresh copied-save smoke run with tracing
disabled and only freecamera/camera303 selected. The final eight sampled timing
windows measured about 55-56 FPS. Evidence: installed-camera-smoke.log,
installed-smoke-result.json and shots/installed-camera-smoke_03181.png.
Both CTests (freecamera/thread_time) and both live-harness tests passed.
The test process was stopped after capture.

### Outer ocean coverage

The experimental extension corrected exposed seabed at maximum zoom, but was
rolled back after the performance comparison below. The confirmed ocean surface uses vertex 911007845f398fde / fragment
fa26b7e08e326558, with shoreline variant 0abedcc9ad8d0174 / 613329b58bbd1870.
Disabling those two fragment shaders removes the sea while retaining the
shore and seabed (`sea-shader-confirmation_03001.png`). A bounded transform
capture (`sea-transform.log`) and matching index counts identify the actual
`FldOutS03`, airport, rocky coast and corner meshes, rather than only SeaOnly
archives. For example, the south tile is centered at Z=1040 and its native water
ends at local Z=240, exposing the finite surface when the eye moves farther out.

`tools/build_camera_ocean.py` extends the outer portion of native water and its
underside from 240 to an explicit experimental `--outer` extent (800 in the
initial trial). It preserves the inner 80 units, shoreline,
materials, UVs, triangle counts and collision data. Position, submesh bounds and
radius writes are byte-audited against the original archives. All 116 generated
archives matched the private test overlay byte-for-byte. They were subsequently
removed from `private/game/mods/freecamera/romfs/Model`; the audit/provenance report is
`private/game/mods/freecamera/ocean-extension.json`. Installation records are in
`private/freecamera/install-backups/ocean-*/manifest.json`.

The maximum-zoom forward capture (`ocean-shore-forward_03541.png`) no longer
shows the dark seabed strip, and the full low-angle orbit
(`ocean-shore-orbit-small.png`) retains coverage around the tested island.
This establishes one island layout in daylight; other layouts and storm waves
are not yet visually verified. There are no added triangles or draw calls,
although enlarged bounds can affect native culling. This run is not a matched
performance comparison. Distant scenery placement/LOD and broader weather
acceptance remain outstanding.

The high-angle capture (ocean-shore-high_07741.png) also retains nearby ocean
coverage, but exposes distant tree/terrain silhouettes and missing ground beyond
the resident island region. This is evidence for the remaining distant-grounding
and LOD work, not a pass for overall high-angle scenery.

The temporary world LookAt call also bypasses the dispatch wrapper because it
is compiled as a direct call. It previously supplied an orbited loading snapshot
but left the native matrix unorbited. The entry adapter now saves/applies inputs
for that direct path, and the sole leaf return at main+37e4724 restores them after
matrix generation. Indirect calls retain their existing scoped wrapper. This
requires rebuilding the guest library with the updated official hook list.

The isolated 60 Hz library was rebuilt with that one regenerated function; the
other generated bodies, including the SpeedFix variant, were preserved. The
runtime trace confirms direct inputs are restored, and stationary camera
snapshots remain stable. `distant-direct-lookat_03121.png` still has missing
terrain, so matrix consistency is not proof that terrain coverage is complete.
The steep-view instance count is below the 40,000 capacity, and the optional
renderer clip box is null. A temporary horizon-plane bypass did not visibly
fill the gap; its timing is insufficient to exclude native horizon culling.
`tools/play/scenarios/freecamera_high.txt` retains a harness case for this work.
Camera/timing CTests and live-harness unit tests pass. These changes are still
under isolated test, not yet promoted to the normal executable/library.

The same missing-ground pattern also occurs with NX_DRAW_MERGE=0 in
distant-inclined-unmerged3_04441.png (distant-overhead-unmerged.log). This
excludes repeated-draw merging as the cause of that pattern. Maximum-zoom
overhead captures also show excessive fog over nearby ground; the current fog
normal follows the camera but retains the native bias as eye-target distance
changes. Zoom-aware fog bias was investigated next.

The zoom-fog correction is now implemented in the isolated build: it subtracts
the added camera-to-player distance times plane length and inverse range from
the fog bias. Tests verify unchanged attenuation at the player and at a fixed
distance beyond the player for zoom factors .25, 1 and 3. Matched fixed-pose
captures `terrain-cull-normal_02881.png` and `fog-zoom-high_02581.png` show the
excessive nearby haze removed. A full orbit (`fog-zoom-orbit-small.png`) and reset
to native zoom (`fog-native-zoom_05281.png`) retain consistent nearby colour.
Native weather colour, intensity and ramp slope are unchanged; broader weather
acceptance remains open.

Diagnostics can now start at a fixed relative pitch and zoom with
`NX_FREECAMERA_PITCH` (degrees, -90 through 90) and `NX_FREECAMERA_ZOOM` (.25
through 3), alongside the existing initial yaw. `NX_FREECAMERA_DIRECT_LOOKAT=0`
isolates the recent direct-call change in matched runs. Invalid pitch/zoom
values are ignored. These are harness controls, not persisted player settings.

A temporary native culling bypass at 146a660 did not change the remaining gap
at an identical fixed pose (`terrain-cull-bypass_03841.png` versus
`terrain-cull-normal_02881.png`). That diagnostic hook has been removed from
both runtime and generated guest code. The land map reports coverage of cells
(16,16)..(127,111), so its resident map is not limited to the original small
camera rectangle. This does not exclude other ground-rendering paths.

**Ocean extension performance regression:** a controlled low-angle comparison
with the same executable, copied save and fixed camera restores about 57 FPS
with original water assets, versus about 42 FPS with the 800-unit extension.
The direct-camera switch does not explain that regression. The 116 generated
ocean archives were therefore removed from the normal freecamera installation
after checking their installed hashes; original assets were untouched. The
800-unit build is not an accepted release fix. The smaller 560-unit extension
also costs performance and remains disabled. The generator now requires an
explicit `--outer` argument rather than defaulting to the rejected 800 extent.

The final twelve 60-frame windows of fixed low-angle harness runs are recorded
in `private/freecamera/perf-test/ocean-performance-comparison.json`:

| Water extent | Direct LookAt adapter | Mean FPS | CPU replay ms | Upload MB/frame |
| --- | --- | ---: | ---: | ---: |
| 800 | Off | 44.03 | 20.95 | 23.67 |
| Original 240 | Off | 55.89 | 15.97 | 15.71 |
| 560 | Off | 54.19 | 16.52 | 16.33 |
| Original 240 | On | 54.78 | 16.34 | 16.40 |

These use the same executable, copied save, initial pitch -90, yaw 0, zoom 1,
time offset 28800, 2160 resolution, SSAA2, DLAA and 60 Hz. Sequential runs have
normal scene variation; they establish the large extension regression, not a
precise sub-millisecond cost for the camera adapter. The final default-adapter
orbit (`fog-direct-native-orbit-small.png`) retains visible ground, water and
consistent fog at the tested low angle. Camera/timing CTests and harness unit
tests pass.

The camera/zoom-fog runtime and matching official-hook guest library were
promoted with backups in `private/freecamera/install-backups/20261115-070631`.
The executable SHA256 is
`ca74eb14ad6c16dabf2734fe8f735624e9e39036175249b62e98af61a86f6e5c`;
the guest library SHA256 is
`16a23643de2b61e2acd1be1b93e2ca2474daafe26be5ee2f5dbdba4eb9a5a80a`.
Original ocean assets remain in use. This is not acceptance of maximum-zoom
ocean coverage, elevated distant terrain, LOD or untested weather/interiors.

### Elevated ground diagnosis

The fixed inclined pose (initial pitch -7, yaw 30, zoom 3, copied save and time
offset 28800) reproduces the brown distant silhouettes. A temporary entry probe
at `11fc610` confirms the terrain worker itself reads the expanded rectangle
(-25,28)..(70,129), rather than merely leaving those values after submission.
The probe is removed from source and the isolated guest library. Both map
representations cover the island: job+100 uses inclusive bounds, whereas
job+108 stores dimensions (112,96) at +18 and origin (16,16) at +28. Do not
interpret those two structures using the same field semantics.

Function `1469c10` generates river ripple effects, confirmed by its references
to `Env_RiverRippleCorner`, `Env_RiverRippleDents` and related names. It is not
the missing grass-surface submission path.

Dropping fragment `20bd0cfd20c2db06` removes nearby grass ground. Its vertex
shader is `c0aa42f5f330ef6d`; the corresponding ground depth shader is
`f6b2598606445ade` with fragment `9defa7b050d05f3b`. The bounded capture in
`ground-depth-matrices.log` and `ground-matrix-comparison.json` shows identical
view/projection constants, viewport, scissor, culling mode and depth range.
Both passes submit nonzero draws at distant coordinates. A second capture
(`ground-deformation.log`) confirms identical deformation rows c8[45..46],
including (1,-979.8,1,0). These are diagnostic observations, not a terrain fix.

`NX_EQUAL_ALWAYS=1` breaks nearby occlusion without filling the distant brown
region; `NX_NO_STENCIL=1` also leaves it intact. A temporary host-upload
dark-fog-intensity bypass changes water haze but leaves the region intact.
These overrides are not installed; the temporary fog bypass is removed.
Screenshots are `ground-depth-always_03181.png`, `ground-no-stencil_02341.png`
and `ground-no-dark-fog_02941.png`. The cause of the shaded-pass cutoff remains
unresolved. The trace switch `NX_TRACE_VS_BLOCKS` now also records deformation
c8[45..46], dark-fog c5[10..12] and fragment material c6[27..29], so those values
can be compared without treating matching camera matrices as sufficient proof.
The subsequent `ground-material-comparison.json` records full grass-material
opacity (c6[29].x=1) and no forced colour override (c6[28].x=0). Native dark fog
is green-tinted in this daylight sample, not the flat brown seen in the cutoff.
The normal installed executable/library remain the promoted hashes above;
none of the depth, stencil, worker or dark-fog diagnostics were promoted.

The cutoff is now identified: the recentered ParkVR sky sphere was still drawn
at its physical radius (600 world units), covering terrain beyond it. Dropping
only its colour fragment shader (`d579b8327391ea0a`) reveals the ground, cliffs
and trees (`ground-without-sky-sphere_02701.png`). The apparent terrain holes
were sky occlusion, not absent island tiles.

The runtime now maps the main-view ParkVR colour/depth, CloudVR and star draws
to viewport depth 1 while the outdoor free camera is active. Their shading,
geometry and projection remain native; environment-probe variants are excluded.
This is applied consistently when issuing draws and comparing/storing merged
draw state. Captured native background draws use LEQUAL and do not write depth.
The fixed inclined capture `sky-depth-inclined_03361.png` and full orbit
`sky-depth-inclined-orbit-small.png` retain the previously hidden terrain.
The stationary inclined run reaches about 58–59 FPS. The finite ocean edge is
now clearly visible beyond the shoreline water; this fix does not extend it.

The night orbit (`sky-depth-night-orbit-small.png`), inventory and tent interior
captures retain native stars, clouds and UI. The stationary comparison subsequently
found a separate repeatability problem: the installed executable first averaged
58.28 FPS / 15.05 MB uploaded per frame, but a repeat of that unchanged executable
averaged 47.71 FPS / 22.29 MB. The sky-depth build averaged 48.02 FPS / 22.30 MB;
disabling only its depth adjustment averaged 48.06 FPS / 22.49 MB. All use pitch
-7, yaw 30, zoom 3, original ocean assets and sample windows 45–56, with time
offset 28800. Evidence: `sky-depth-performance-comparison.json`. This does not
establish a sky-depth regression, nor a stable 58 FPS result. Performance
investigation remains open before the next promotion.

`NX_FREECAMERA_SKY_DEPTH=0` disables only the background-depth adjustment for
diagnosis. Aggregate `camera fog uploads` counters distinguish host-patched fog
copies from ordinary upload deduplication while investigating the slow state.

The fog profile records about 6.57 MB of adapted copies per 300 frames
(0.022 MB/frame), with over 1.2 million reuse hits; these copies do not explain
the roughly 7 MB/frame difference between fast and slow runs.

The promotion audit found that the earlier worker-probe source restore had
preserved its timestamp, so Ninja had retained the diagnostic object despite
the source being restored. Rebuilding `code_376.cpp` produces a guest library
whose `.text`, `.rdata`, `.data`, `.pdata`, `.tls`, `.rsrc` and `.reloc` sections
all match the pre-diagnostic installed library. Evidence:
`promoted-sky-dll-sections.json`. The preceding repeated sky/performance runs
used the same stale diagnostic DLL in both executable controls; final smoke
testing uses the rebuilt official library. Do not assume a successful no-op
build proves that a timestamp-preserving source restore reached the binary.

Promotion backups are in `private/freecamera/install-backups/20261115-074920`
and `20261115-075102`. The installed sky runtime SHA256 is
`66ed504223ad3fb454ffdbc57c57170fac999a3cdc724372d9a5d92731aec93e`;
the rebuilt official guest library SHA256 is
`68e6eae88940d0b63bcb4a2d1f7e9121f3558e7e8f2a532730f8ac732cfdb6fc`.
Original ocean assets remain installed. The user's save was not used by the
harness; testing uses the separate copied save under `perf-test`.

### Material batching investigation

Fast and slow stationary runs both replay about 6.16 million draws per 300
frames. The fast state combines about 5.40 million of them; the slow state
combines only 4.71 million. Rejection reason 9 includes fragment-only bindings
that have no vertex-shader read extent; it does not necessarily mean a dynamic
vertex index. `merge-binding-probe.log` identifies fragment bindings 23 (c4)
and 25 (c6). `merge-content-probe.log` confirms some 496-byte c6 blocks are
identical at different guest addresses, while the 192-byte c4 blocks differ.

A conservative whole-buffer comparison experiment was rejected: it did not
produce a reliable improvement. One process alternated it every 600 frames,
retaining three settled intervals per mode. The control averaged 57.88 FPS /
15.27 MB uploaded per frame; the experiment averaged 54.69 FPS / 17.43 MB.
The final enabled interval entered the slow state, so this is not a clean
measurement of comparison overhead, but it provides no basis for promotion.
Evidence: `merge-equal-ab-comparison.json`. The experimental patch is archived
privately as `merge-equal-fragment-experiment.patch` and removed from runtime
source. The installed executable remains the audited sky-depth build.

The larger c4 case needs a more precise comparison: fragment shader
`2330b41795e4250d` reads only c4[8].x, and `f2501b67d1064071` reads only c4[8].z,
as verified by their SPIR-V decompilations (`merge-material.frag` and
`merge-material-alt.frag`). Changing unused transform rows should not by itself
break batching. Any implementation must establish the complete shader read
footprint, reject unknown/dynamic pointer use, preserve zero-padding semantics,
and retain the existing epoch/GPU-write guards. A visual match or identical
whole-buffer sample alone does not establish those conditions.
