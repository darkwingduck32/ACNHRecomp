# 360 camera runtime

The camera now runs as a built-in native feature. Use Camera and 360 Camera in
Settings; no camera ROMFS mod installation is required. See the
[current feature guide](../games/acnh/features/camera/README.md) for settings,
build inputs, migration and reload boundaries, and [NATIVE_FEATURES.md](NATIVE_FEATURES.md)
for the internal extension contracts. The runtime recognizes ACNH 3.0.3 using
instruction fingerprints.

The investigations below are historical implementation notes. Earlier package
installation instructions are superseded by the current feature guide.

## Controls

During outdoor play the right stick (I/J/K/L with default keyboard bindings)
orbits immediately. Movement follows the camera direction. There is no camera
editing mode to enter or exit.

- Hold R3 and move the right stick vertically to zoom. Release to keep that zoom.
- Click and release R3 without moving the stick to recenter.
- On keyboard, hold F6 while using I/K to zoom; F7 recenters.
- The D-pad, L3, shoulders, triggers, and action buttons retain their game bindings.
- Pitch stops at the visible limits and responds immediately when input reverses.

The camera retains its orientation when leaving outdoor play. Active layout
cursors, dialogue page handlers, and text entry suspend camera input and movement
rotation, including while dialogue text is typing; the regular game input then
passes through. Installed-build harness
checks cover outdoor orbit, inventory, settled tent interiors, native indoor
camera rotation, the phone UI, save-dialogue typing, page waits and choices, and
return outdoors. These checks do not yet prove
every overlay, dialogue interaction or cutscene is covered.

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
and ground/canopy height. Live coverage and performance comparisons, including
the installed build, are recorded below. The height band is conservative for
ordinary terrain; broader coverage of the game's distant drum deformation remains
part of the grounding investigation.

The local parameter investigation identified `Param/Gfx/ActorDistantField.byml`
with `mDrumOffsetYOffset.f32`, `mDrumOffsetYScale.f32`, `mYScalePeakOffset.f32`,
`mRotateYParam.f32`, and `mScale.f32`. The matrix function at main+0x60acd4
combines drum rotation and a quadratic vertical offset with camera-relative
placement; its relationship to foreground terrain still needs validation before
changing distant-scenery placement. Fog palette parameters exist separately;
increasing fog alone does not establish lower rendering cost or correct LOD.
Subsequent asset inspection identifies `FldDistantView*` as separate background
islands, rather than playable-island terrain or a general tree LOD system.

Both field-camera branches now receive the orbit, including the temporary
stack-based LookAtCamera returned to main+0x7a88a4. Previously only the heap camera
in the other branch was adjusted, leaving an alternate world/drum matrix in the
original orientation. The temporary object's writable memory span is checked and
its inputs are restored after matrix generation, as for heap cameras. This fix
is compiled, scope-tested and exercised by the live orbit captures recorded below.

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

The standalone `uniform_reads` tests now prove scalar/vector read footprints
for the restricted translated c[] layout, reject dynamic indexing and pointer
escapes, and check truncated-buffer zero padding. `uniform_reads_acnh` checks
the two actual shader binaries against offsets 128 and 136. The experimental
runtime comparison initially contained a loop-bound error: a two-stage merge
snapshot was traversed using the full six-stage state count. Those earlier
comparison figures are not reliable optimization evidence. A regression test
now covers snapshots with fewer stages than the current state.

After correcting the bound, a fresh six-interval-per-mode stationary A/B test
averaged 50.225 FPS / 18.271 ms replay / 20.107 MB uploaded with comparison off,
and 50.078 FPS / 18.357 ms / 20.331 MB with it on. Evidence:
`merge-reads-bounded-ab-comparison.json`; intervals 2400–9599, with the first
240 frames of each interval excluded. There is no measured benefit, so the
runtime integration is removed and archived privately in
`merge-read-footprint-experiment.patch`. Only the standalone proof utility and
its tests are retained; the installed game is unchanged.

`merge-read-values.log` establishes why ignoring unused rows is insufficient:
shader `2330b41795e4250d` actually reads palette values 0.09375, 0.03125 and
0.21875 at c4 offset 128 across consecutive draws. Shader `63a579cd6aed8229`
reads changing values around 0.36–0.39 at offset 136. These are used values,
not merely different source addresses or unused transforms. Further batching
must preserve per-instance fragment constants. The current merger gathers
only vertex constants and its SPIR-V rewrite supplies `InstanceIndex` only
to the vertex stage. Extending that path requires an unused flat varying
between stages, fragment-block gathering, pipeline-key/module support and
paired shader validation. Temporal motion already reserves locations 30/31;
any additional varying must avoid those and the original shader interfaces.

### Support-cache overflow and FPS recovery

The fragment-instancing prototype now has a validated shader-link utility and
standalone tests. Khronos `spirv-val --target-env vulkan1.1` accepts its fixture
pair, the game's vertex shader (including export-only mode), and three material
fragment shaders. However, the renderer experiment averaged only 50.98 versus
51.08 FPS across six intervals per mode (`material-instances-ab-comparison.json`).
Its renderer integration is archived privately and removed from active source.

Aggregating refusals by binding identified the dominant problem that the earlier
small samples missed: `material-refusals-profile.log` records 702,900 failures
per 300 frames for ground shaders c0aa42f5f330ef6d / 20bd0cfd20c2db06 at binding
0, the host support buffer. Expanded views use 72–74 distinct viewport/render-
scale combinations, exceeding the support cache's 32-entry limit. Every miss
after the limit makes another host upload and prevents draw merging. Driver
constant-cache fallbacks remain zero.

The support cache now allows 256 entries, allocated lazily at stable addresses.
The driver cache keeps its original limit. `NX_SUPPORT_BLOCKS=32` restores the
control capacity; `NX_SUPPORT_BLOCKS_AB=1` alternates 32/256 every 600 frames.
Aggregate logging records peak support entries and both fallback counts.

The same-process fixed-pose A/B run uses pitch -7, yaw 30, zoom 3, copied save,
2160/SSAA2/DLAA and time offset 28800. Six intervals per mode over frames
2400–9599 exclude the first 240 frames of each interval. Results:

| Capacity | Mean FPS | Replay ms/frame | Upload MB/frame |
| --- | ---: | ---: | ---: |
| 32 | 50.59 | 18.16 | 20.16 |
| 256 | 59.74 | 13.22 | 11.01 |

Evidence: `support-cache-ab-comparison.json` and `support-cache-ab.log`.
The enlarged-cache intervals have zero support-cache fallbacks; the control
reaches over two million fallbacks per 300 frames. This change preserves the
support-buffer contents and existing per-submit lifetime/deduplication rules.

The final default-capacity build averaged 59.67 FPS / 13.35 ms replay / 10.97 MB
upload over frames 2400–3419, without diagnostic overrides. Its full orbit,
inventory and tent-entry captures preserve the scene and normal UI behavior.
Cache peak reached 78 with zero support or driver fallbacks. Evidence:
`support-cache-default-result.json`, `support-cache-default-orbit-small.png`,
`support-cache-inventory_05161.png`, and `support-cache-interior_06241.png`.

The runtime was promoted with backups at
`private/freecamera/install-backups/20261115-084431`. Installed executable SHA256:
`49f6fef1b3dca32d5ab3a50862854b9b6a492e48457889bb29a199bd6b735dd9`.
The official guest DLL remains unchanged. A separate installed-executable run
uses the isolated save and records `support-cache-installed-result.json` and
`support-cache-installed_04141.png`. All five focused CTests and both harness
unit tests pass. The outer-ocean boundary remains visible at maximum zoom;
this performance fix does not establish ocean, distant LOD or weather acceptance.

### Ocean re-test with the cache fixed

The earlier ocean FPS rejection was confounded by support-cache overflow.
Fresh sequential tests with the same fixed runtime, copied save and settings
give the following results. Day samples use frames 2400–3419; the real clock
advances between runs, so these are not same-process A/B measurements.

| View | Water extent | FPS | Replay ms/frame | Upload MB/frame |
| --- | ---: | ---: | ---: | ---: |
| Day, pitch -7 / yaw 30 / zoom 3 | Native 240 | 59.67 | 13.35 | 10.97 |
| Same day view | 800 | 59.68 | 13.74 | 11.38 |
| Same day view | 1200 | 59.52 | 14.24 | 11.79 |
| Day, minimum pitch / yaw 0 / zoom 1 | Native 240 | 59.73 | 13.19 | 10.86 |
| Same low day view | 1200 | 59.47 | 14.59 | 11.87 |
| Night, minimum pitch / yaw -78.03 / zoom 3 | Native 240 | 52.71 | 16.67 | 14.80 |
| Same low night view | 1200 | 52.41 | 16.78 | 14.77 |

The 800-unit orbit retains a thin exposed strip at one heading. The 1200-unit
day/night orbits cover the exposed boundary in this island test. The extension
adds daytime render work, but the heavier night view is already below 60 FPS
with native water. The night control's traced eye/target coordinates match the
extended-water run; the latter uses its final stationary 17 windows after the
orbit. No support or driver cache overflows occur in these tests.

Evidence: `ocean-cache-fixed-comparison.json`,
`support-cache-ocean1200-orbit-small.png`,
`support-cache-ocean1200-night-orbit-small.png`, and
`support-cache-ocean1200-night-low-orbit-small.png` under the private test root.
The 116 audited 1200-unit shoreline archives were promoted with manifest and
backups at `private/freecamera/install-backups/20261115-085718-ocean`.
Only water/seaback positions, bounds and radii change; counts, indices, UVs,
materials, shore geometry and collisions remain native. The actual save is
untouched. A visible dark fog/horizon band at night, heavy-view performance,
distant LOD/grounding and broader weather/island coverage remain open work.

The final installed-runtime/installed-overlay smoke reproduces 59.53 FPS,
14.34 ms replay and 11.79 MB upload in the wide daytime view. Peak cache use
is 85, with no fallback copies. Evidence: `support-cache-ocean-installed-result.json`
and `support-cache-ocean-installed_04321.png`. The harness process was stopped
after inspection; normal game configuration and the actual save are unchanged.

### Heavy-view terrain visibility diagnosis

The fixed low-angle pose (minimum pitch, yaw -78.03, zoom 3) is also slow in
daylight: 50.72 FPS / 17.41 ms replay / 15.32 MB uploaded over frames 2400–3419.
`heavy-view-day-control-result.json` rules out night lighting as the sole cause.
The normal renderer reports roughly 33,560 draws/frame before batching, with
95.6% using the fast path and about 31,824 combined into 412 instanced draws.
Support-cache fallbacks remain zero. The remaining problem is not the old
cache overflow recurring.

A read-only isolated guest probe at 146a660 and 146ab80 records native terrain
culling inputs and the two visibility bytes passed to enqueue. At frame 2700,
`terrain-queue-probe.log` records 950,100 enqueues with both bytes clear,
46,800 with only the second set, and 2,083,811 with both set over approximately
300 frames. All sampled culling calls have a non-null native frustum. Therefore
the null-frustum bypass present in the native code is not the explanation in
this pose; adding another frustum test without checking the existing results
would not be an established optimization. The first probe incorrectly labels
x4 as flags: it is the worker index. The follow-up groups calls by return PC
and reads the actual visibility-byte pointer at enqueue. Those bytes become
instance flag bits 6 and 7 in the native routine.

Foreground parameter inspection identifies the previously undecoded 55/75
values as `mAreaTilt` / `mTreeAreaTilt`, not detail-distance thresholds.
`FldDistantView*` assets are separate background island meshes; they are not
evidence of playable-island tree/terrain LODs. The native `Outline` and
`DepthSilhouette` render-pass names likewise do not establish a distance LOD
policy without tracing their selection.

No culling or LOD behavior was changed by this investigation. Probe code is
archived in `private/freecamera/terrain-visibility-probe.patch`; it was removed
from active source and both test binaries restored/rebuilt. Every rebuilt
guest DLL code/data section matches the official library
(`tile-probe-restored-dll-sections.json`); the test root uses the exact official
68e6eae DLL. The installed runtime and ocean overlay remain the verified ones.

### Avoiding redundant fixed-state invalidation

A sampled CPU profile that preserves fast drawing and merging attributes most
draw-processing cost to full setup, despite about 95.6% of draws taking the fast
path. The probe is archived as `sampled-fast-draw-profile.patch` and removed
from active source. Turning off debug-file polling alone does not resolve the
heavy view: `heavy-no-debug-control-result.json` measures 50.85 FPS / 17.27 ms.
Private harness launchers now default that polling off; live input/screenshots
remain available, and explicit `NX_DEBUG_FILES=1` enables diagnostic files.

Fixed-state commands now preserve the prepared draw and pending instance batch
when the copied state is unchanged. Comparisons include complete relevant blobs,
vector lengths and exact floating-point bit patterns. Resource/program/pool
bindings, deferred bindings, memory writes and counters keep their existing path.
`NX_STATE_DEDUP=0` restores the control; `NX_STATE_DEDUP_AB=1` alternates every
600 frames. The final path has no new shared mutable profiling counters.

The heavy-view same-process A/B test, six intervals per mode over frames
2400–9599 excluding the first 240 frames of each interval, measures 50.21 FPS /
17.63 ms replay without suppression and 51.16 FPS / 16.90 ms with it. Upload
volume remains about 15.45 MB/frame. About 4,300 fixed-state commands/frame are
redundant, and fast-path use rises to about 97.3%. Evidence is
`state-dedup-ab-comparison.json` and `state-dedup-ab.log`. This is a modest CPU
improvement, not a resolution of the remaining distance-detail requirements.

The final default build measures 51.44 FPS / 17.08 ms in the heavy pose.
Its full outdoor orbit and inventory checks passed. The installed executable
(`5625d5112a2a514d5fd5bed8606d3be410c9cb2370edb1d47e4efeba88ab64d8`)
then measures 59.66 FPS / 13.73 ms / 11.79 MB in the wide pose over frames
2400-3419, with peak support-cache use 82 and no support or driver fallbacks.
The settled tent interior, native indoor camera rotation, phone UI and return
outdoors were visually inspected successfully. An earlier interior capture
caught the iris transition and was not counted as a pass.

Evidence: `state-dedup-default-result.json`, `state-dedup-installed-result.json`,
and `shots/state-dedup-installed-{wide,interior,exit,phone,return}_*.png` in the
private test root (`exit` shows native indoor rotation; `return` shows outdoors).
The previous runtime is backed up in
`private/freecamera/install-backups/20261115-092908`. The official guest DLL,
normal configuration and actual save are unchanged. The isolated harness was
stopped after validation. Heavy-view performance, distance detail/grounding,
night horizon and broader weather coverage remain open.

### Installed grounding coverage checkpoint

The installed 5625d511 runtime and official 68e6eae guest library were checked
at maximum zoom from a steep overhead pose, then an oblique pose, while orbiting
the starting island location. Terrain remains continuous beneath foreground
trees and buildings across the captured headings, and the shoreline remains
covered. No foreground floating was reproduced in this test. Distant terrain
cutoffs/silhouettes remain visible through fog, so this is not full acceptance
of distant grounding or detail selection.

Evidence is `grounding-installed-result.json`, `grounding-overhead-installed.log`,
`grounding-overhead-installed-orbit-small.png` and
`grounding-oblique-installed-orbit-small.png` under the private test root.
The JSON pins executable/library hashes and records the input sequences and
full-resolution contact sheets. This test covers one island position in daytime;
it makes no new performance or weather-coverage claim. No runtime or asset change
was made for the test, and only the isolated save was used.

### Stored detail alternatives and outline-pass entry

`tools/audit_camera_detail.py` reads all `PltTree*` and `Fld*` archives and records
the BFRES shape mesh-slot counts with source hashes. The native audit covers
450 archives and 2,691 shapes; the installed freecamera overlay covers 127
archives and 1,422 shapes. Every inspected shape has exactly one mesh slot,
with no parse errors. This rules out selecting an already-stored alternate
mesh slot in these assets, but not separate model selection or shader-based
simplification. Reports are `native-mesh-detail-audit.json` and
`modified-mesh-detail-audit.json` under `private/freecamera`.

Across the 11 modified tree archives, stored first-mesh index counts total
416,724 in the native assets and 609,000 in the replacements (about 46% more).
These archive totals include multiple growth/animation models and are not
per-frame GPU counts. They motivate evaluating authored distant geometry;
they do not establish that tree geometry causes the remaining replay bottleneck.

Static native references place `Model(Outline/ZOnly)` and
`Model(Outline/Opa+AlphaMask)` in the render routine at `3938f9c`, called from
`3905e74`. Its entry gates inspect render-state flags and counts before creating
an `OutlineDepth` target through `39ed840`; this inspected entry path does not
select geometry by distance. Disassembly evidence is `outline-refs.txt`.
Neither this trace nor the asset audit justifies treating outline as an existing
distance LOD switch. No native pass flags or installed assets were changed.

### Rejected additional raster-state suppression

A separate experiment skipped byte-identical polygon-offset values, unchanged
depth-clamp state and stencil assignments whose selected faces already held the
same truncated byte. Unlike the retained fixed-state optimization, these extra
checks show no measurable benefit. The heavy-pose same-process A/B run covers
frames 2400-9599 with six intervals per mode, excluding the first 240 frames of
each 600-frame interval: control 51.08 FPS / 17.2325 ms replay, experiment
51.04 FPS / 17.2322 ms. Upload volume is 15.43 versus 15.45 MB/frame.

Evidence is `raster-dedup-ab-comparison.json`, `raster-dedup-ab-hashes.json` and
`raster-dedup-ab.log` in the private test root. Five focused tests passed, but
that is not evidence of a performance gain. The experiment was archived as
`private/freecamera/raster-dedup-experiment.patch`, removed from source and the
preview runtime rebuilt. The installed 5625d511 runtime was never replaced.
Further work should target the measured terrain/submission workload or actual
distance-detail selection rather than adding these checks to every command.

### Filtering camera uniform checks before reading shared state

The batch continuation path calls `uniform_host_patch` for each gathered tile
block. It now rejects irrelevant banks/stages/shaders before reading the camera
heartbeat atomics. The same sky/fog blocks remain protected from raw guest-block
merging; no shader constants or visibility limits change. The final code removes
the temporary A/B switch and duplicated control path.

Six paired heavy-view intervals over frames 2400-9599, excluding the first 240
frames of each interval, measure 50.91 FPS / 17.30 ms replay before and 51.37 FPS /
16.90 ms after. Upload volume is effectively identical at 15.45 MB/frame. This is
a modest CPU improvement. Five focused tests passed. Evidence is
`host-patch-filter-ab-comparison.json`, `host-patch-filter-ab-hashes.json` and its
log in the private test root; the A/B implementation is archived privately as
`host-patch-filter-ab.patch`.

The final build averages 59.70 FPS / 13.27 ms replay / 11.78 MB upload in the
wide pose, with peak support-cache use 79 and no fallbacks. Its outdoor orbit
and inventory were visually inspected; known distant silhouettes and horizon
blending remain. The verified executable
`d02944fb4389555867921641f96bb1da7a6691d13a7fa98349d12a8a6dfe5a12`
was installed after stopping the isolated harness, with the prior runtime backed
up at `private/freecamera/install-backups/20261115-095650`. The promotion now
requires a visual-pass record whose executable/library hashes match the inputs.
Guest DLL, cloud/ocean assets, normal configuration and actual save are unchanged.

Static tracing also finds that terrain draw routine `146b080` tests instance
visibility flags before binding draw data: passes 0, 1 and 2 require bit 6,
and pass 9 requires bit 7. The skip at `146b1c8` branches to the next instance.
Other pass IDs bypass this particular test. `terrain-draw-pass-code.txt` and
`terrain-draw-pass-map.json` record the code and table at `4075b7c`, pinned to the
native binary hash. Live per-pass counts are still needed before changing culling.

### Terrain pass counts identify world ambient occlusion

An isolated guest-library probe counts instances at `146b19c` before the native
visibility tests and at `146b260` after them, without changing guest registers,
memory or draw decisions. Over the last 20 million diagnostic observations,
pass 12 accepts 2,134,146 instances, including 702,072 with both visibility bits
clear. Passes 0 and 2 accept none with bit 6 clear; pass 9 accepts none with bit 7
clear. The pass-12 clear-bit instances are about 8.4% of accepted instances across
these four passes, not a measured fraction of GPU cost. Diagnostic overhead means
this run is not an FPS benchmark.

Guest frame-chain sampling identifies pass 12's caller at `157226c`. The descriptor
there explicitly selects pass 12 and references the native string `Model(WorldAo)`
at `3f87121`. Pass 13 similarly references `Model(FieldUnitMap)` at `3f9685d` from
`145dfb0`. Thus the unculled pass is world-space ambient occlusion, not evidence
that the main-view culler is broken. Reusing main-camera visibility would risk
removing contributors to a different map. The next culling investigation must
compare tile bounds with the AO pass's own projection/coverage.

Evidence: `terrain-pass-probe-result.json`, `terrain-pass-probe-stderr.log`,
`terrain-pass-stack-stderr.log`, `terrain-extra-pass-callers.txt`, and
`world-ao-decoded-parameters.json` under the private freecamera/test directories.
The latter decodes native outdoor AO intensity 0.4, near/far 1/200, Gaussian
level 10 and sigma 4; unresolved vector fields are still labelled by hash and
must not be treated as established coverage dimensions.

Probe variants are archived as `code_427.pass-count-probe.cpp` and
`code_427.pass-stack-probe.cpp`. The original generated source was restored and
rebuilt; all PE sections match the official guest DLL
(`terrain-pass-restored-sections.json`). The test root again uses the exact
official DLL, the harness is stopped, and the installed runtime/assets and actual
save were not changed by this investigation.

### World-AO coverage and shader coordinate space

A second isolated read-only probe captures the AO camera update at `1571a6c`.
The settled heavy pose uses eye `(444.55957, 99.99877, -5.24520)`, a top-down view,
and orthographic X/Y scales approximately 0.01: a 200-by-200 world-unit footprint.
The terrain drum offset is -979.8 with blend 1 in this sample. Testing queued
tile spheres against that flat X/Z footprint estimates about 95% outside it.
This is a candidate count, not an accepted-AO-draw or performance measurement;
the diagnostic uses the observed fixed half-width and omits a deformation margin.

Tracing the native 386-by-386 AO target identifies vertex shaders
`395365e711e54f6f` and `458db431ec44ee1c`. Their decompiled code transforms local
positions with c4, adds `c8[46].z * c8[46].y` to world Z, and applies the c3 view
and projection. The former additionally perturbs X/Z by bounded cosine products
scaled by half of `c8[45].y/w`; the latter has no such perturbation. This supports
testing flat AO-map coverage rather than the curved main-camera frustum, but a
production culler must account for the active projection, full model transforms,
bounding spheres and shader deformation before rejecting a draw.

Evidence: `ao-coverage-result.json`, `ao-tile-coverage-probe-stderr.log`,
`ao-target-trace.log`, captured executable/library hashes, and
`ao-{terrain,model}-candidate.vert` under the private freecamera/test directories.
The generated-source probes were archived and both modified translation units
restored/rebuilt. The test root again uses the exact official library; the
installed runtime and actual save remain unchanged. No AO culling is enabled yet.

### Isolated AO culling prototype: measured benefit, pending production integration

A private generated-library prototype caches each terrain instance's transformed
resource sphere at enqueue, then tests it against the active AO map's four X/Y
clip planes. It preserves every enqueue and every other rendering pass. Sphere
radius uses the affine matrix's Frobenius norm plus a 20-unit deformation margin;
invalid bounds/projections fail open. This is deliberately conservative, but the
deformation margin and cache lifetime still need broader validation before release.

In the demanding stationary dusk view, it rejects 6,830 of 7,259 AO terrain
instances per frame. Eight alternating/balanced 600-frame intervals (180 settling,
420 measured) produce 28 samples per mode: 49.30 FPS / 17.07 ms replay / 15.58 MB
upload without rejection, and 51.65 FPS / 15.81 ms / 14.25 MB with rejection.
Both modes include prototype bounds, counters and file-toggle overhead. These
results establish a useful candidate, not final production performance.

Actual 386-by-386 AO targets were captured off/on/off. Their visible silhouettes
remain intact; mean channel differences are roughly 0.11-0.21 on a 0-255 scale,
including off/off animation differences. The camera orbit also passes visual
inspection. This is not frozen-frame equivalence or coverage of other locations,
weather and interiors. Existing distant silhouettes and horizon blending remain.

Evidence in the private test root: `ao-toggle-ab-result.json`,
`ao-cull-prototype-result.json`, `ao-map-comparison-refreshed/`, and
`shots/ao-cull-orbit-review.png`. The prototype DLL hash is
`ce2cc408bfda70118bd6283de5b78aadd5e947390117901c597c34dfe4cfd9e4`.
The two generated source variants are archived as
`probe-library/code_{427,447}.ao-cull-prototype.cpp`. After testing, original
sources were rebuilt and all PE sections compared with the official library;
the exact official DLL was restored to the test root and the harness stopped.
No experimental culling is installed in the normal game.

Production integration should use version-checked runtime hooks rather than
shipping these manually edited generated files. The enqueue hook can read the
original model/matrix at `146ac2c` (x1 and the saved matrix at x29-8), while the
AO update completes at `1571a6c`. Existing conditional branches at `146b1d4`
and `146b1dc` offer a register-only skip path without changing shared instance
flags; its hook overhead needs benchmarking against this direct-branch prototype.

The guarded runtime-hook implementation now exists in `freecamera_ao.cpp`,
disabled by default (`NX_FREECAMERA_AO_CULL=1` opts in). It verifies the four
hook instructions, only rejects pass 12 while the field camera is active, and
fails open for stale bounds or map captures. Registration handles simultaneous
workers claiming the same manager. `NX_FREECAMERA_AO_AB=1` alternates rejection
every 600 frames for measurement; both modes still incur hook/cache overhead.

The windowed run's first clean pair measured 50.80 versus 52.60 FPS, but host
presentation subsequently stalled at about 500 ms/frame while culling was off.
This run cannot support a complete paired performance conclusion. A separate
headless run completed two pairs over frames 2400-4799: 51.23 versus 52.98 FPS,
16.59 versus 15.33 ms replay, and 15.48 versus 14.12 MB upload. These numbers
exclude the normal display path and are not interchangeable with windowed FPS.
Evidence is `ao-hooks-windowed-partial-result.json`,
`ao-hooks-headless-ab-comparison.json`, and their executable/library hashes.
The runtime and the three regenerated guest functions compile; the existing
freecamera test passes. Broader visual/transition validation is still pending.
The normal installed executable/library remain unchanged, the harness is stopped,
and its exact official guest DLL has been restored. These hooks are experimental,
not a completed performance fix or release candidate.

### Foreground range check

The missing registration messages were a logging issue, not a broken getter:
visibility groups initialize before the outdoor camera becomes active, so the
old `!initialized && on` log condition suppresses them. The existing getter is
called and the perspective/land-footprint logs confirm that FOV coverage runs.
Registration is now logged on initialization regardless of camera state.

Captured L/R/N/F ranges are 280/280/120/530 for generic foreground and
300/300/180/650 for trees; the main furniture creation range is
270/270/200/690. An isolated test capped only generic foreground reach at 360,
retaining tree and terrain ranges. Two headless pairs over frames 2400-4799
measured 44.82 versus 45.51 FPS, 19.11 versus 18.86 ms replay, and essentially
identical uploads (15.361 versus 15.358 MB). Off/on images show no substantial
scene change in this shore-facing pose. This does not establish useful distant
detail reduction, so the cap and its test switches were removed.

Evidence: `detail-distance-ab-comparison.json`, `detail-distance-ab-hashes.json`,
`shots/detail360-off_03721.png`, and `shots/detail360-on_04261.png` in the private
test root. The rejected implementation is archived as
`private/freecamera/detail-distance-experiment.patch`. The harness is stopped;
installed game files and the actual save are unchanged.

The AO candidate now uses shared, tested bounds math (`freecamera_ao_math.h`).
Tests cover rotated top-down projections, translated/scaled/sheared model bounds,
edge contact, invalid inputs and clearing stale clipping state. A small edge
tolerance retains touching spheres. Five focused tests pass. The candidate
enables AO culling with freecamera by default, with `NX_FREECAMERA_AO_CULL=0`
as a diagnostic override; its normal installation is still pending the final
orbit and indoor/outdoor transition test. Candidate executable/library hashes
are stored under `private/freecamera/release-candidates/ao-*` with
`visual_passed: false` until that validation is performed. A normal game session
was observed running separately from the harness, so further game launches and
promotion were deferred rather than competing with that session for the GPU.

After the user authorized closing the idle normal game, the final candidate
passed outdoor orbit, inventory, tent entry, native indoor camera rotation,
return outdoors and resumed outdoor orbit. The settled wide view averages
59.79 FPS / 12.39 ms replay / 11.21 MB upload with peak support-cache use 82
and no fallbacks. `ao-final-wide-result.json` records the images and exact hashes.
The known distant terrain/tree cutoff remains visible; this validates the AO
change, not completion of distance-detail work.

The validated pair was installed with both previous files backed up at
`private/freecamera/install-backups/20261115-105216-ao`. Installed runtime hash:
`e4eada40e47f9990faf1280099ebb740d96691082e609754a7b222a6e1dd8e53`;
guest library hash:
`6771b1731ca864531bad0279efe29796d94999baeffbdf327f1d2f507ec6248d`.
The installer checked that neither game executable was running, verified
candidate hashes against visual validation, and backed up both files before
replacement, with rollback on copy failure. It changed no assets, configuration
or save files. The private test root's `install-ao.json` records the promotion.

The installed heavy-view check averages 51.64 FPS / 14.96 ms replay / 14.05 MB
upload, with cache peak 82 and no fallbacks (`ao-installed-heavy-result.json`).
The shipped executable/library also passed an off/on/off AO-texture comparison:
mean channel differences are 0.059-0.092 out of 255 for enabled/control pairs,
versus 0.085-0.098 between the two disabled controls. Neither map loses any
stable opaque interior pixels (11,600 and 9,899 tested pixels respectively).
The matching first two full-scene images show no visible regression; the final
control's full-scene camera angle changed, so it is not treated as a matching
full-scene comparison. AO map coordinates remain independent of that orbit.
This is animated-scene evidence, not frozen-frame equivalence or exhaustive
coverage of every island/weather combination.

`ao-installed-fidelity-result.json` and `ao-installed-fidelity-images/` record
the evidence and limitations. The retained AO optimization is implemented,
tested and installed; rejected experiments are removed. At the user's request,
the harness is stopped and no new investigations are being started. Remaining
distant detail/cutoff and horizon issues are deferred, not marked resolved.

### River surface cutoff follow-up

The user subsequently reproduced a sharp river-water cutoff in visible range.
`river-camera-moved_24601.png` records the requested view, with eye
(304.1,271.4,1022.1) and target (593.0,12.0,794.7). This reported fidelity defect
is being investigated within the wrap-up; broader new optimization work remains
deferred.

An isolated bypass of the two `146a660` calls in worker `11fc610` did not change
the cutoff (`river-water-control_04801.png`, `river-water-bypass_06181.png`). It
has been disabled, its generated source restored, and the harness guest library
restored to the validated `6771b173...` build. No bypass was promoted.

A read-only live snapshot identifies model names at model+8, with terrain models
at manager+508, count +500, stride 4a8. River models occupy indices 120..164 in
this scene. Their four instance lists start at model+20 with stride 18; instance
flags are at +10 and sphere at +14. Of 659 sampled river instances, 488 have
both main-view and shadow visibility bits set. Projecting their spheres into
the captured view shows visible instances on both sides of the cutoff
(`river-queued-bounds.json`, `river-queued-overlay.png`). This is a non-atomic
live snapshot, not proof of the separate water surface's submission.

In the restored-library reproduction, temporarily skipping fragment shader
`587e1cd12261624a` removes the blue river surface and leaves the banks intact
(`river-surface-hidden_02701.png`). The skip is cleared afterward. This confirms
the shader to trace for the missing surface; the queued terrain meshes alone
cannot establish that water tiles are submitted past the cutoff. The river
defect remains unresolved and no new runtime fix is claimed.

The bounded shader-filtered trace contains 800 draws with 283 unique tile
transforms; the first tile repeats at indices 0, 283 and 566, covering two
complete cycles. Surface tile origins span X=420..790, Z=380..760, including
different water elevations. Their projected edge follows the observed cutoff,
while the river terrain instances continue beyond it. Evidence is
`river-surface-trace.log`, `river-surface-positions.json`, and
`river-surface-overlay.png`. This narrows the next change to the separate water
surface selection rather than the already-expanded terrain rectangle or its
per-model visibility flags. The diagnostic run is stopped; its slow FPS with
uniform tracing enabled is not a performance measurement.

Static lookup also locates the `mRiverBackLimitDepth` parameter registration at
`14be3c8`, with its node at owner+338 and storage at owner+360. Its relationship
to the surface selection is not yet established; changing it blindly is not a
validated fix. `river-surface-refs.txt` records the registration disassembly.

The confirmed selection path is `14b7158`, which queues surface worker
`14b0460`. Live `WaveSimulationParam` values are `mNormAreaSide=200`,
`mNormAreaUp=400`, and `mNormAreaDown=80`. They generate an independent inclusive
10-unit tile rectangle at job+288/28c and +298/29c. The new `14b76e8` hook updates
that rectangle before submission using the same perspective footprint as land.
It preserves the native tile/frustum tests, water materials and buffer limits;
inactive/identity cameras retain native selection. The hook requires the updated
3.0.3 guest library. `NX_FREECAMERA_WATER_BOUNDS=0` disables it for diagnostics;
`NX_FREECAMERA_WATER_AB=1` alternates disabled/enabled 600-frame intervals.

The candidate fills the reported cutoff at the matching pose. A full orbit,
top-down view and return to native camera completed. A 90-second live monitor
sampled 8,740 times and observed a peak of 624 entries against the existing
3,000-entry surface buffer; no allocation expansion was necessary for these
tests. Evidence: `water-bounds-repro_02581.png`,
`water-bounds-orbit-review.png`, `water-bounds-top_06481.png`,
`water-bounds-native_09601.png`, and `water-bounds-capacity-result.json`.

The matched off/on/off/on comparison restores the cutoff in both disabled
captures and fills it in both enabled captures. Over the two measured pairs,
mean replay time changes from 14.778 to 15.031 ms, FPS from 54.475 to 54.108,
and uploads from 12.598 to 12.633 MB. These are one-scene animated measurements,
not a guarantee for every island. `water-bounds-ab-comparison.json` and
`water-bounds-ab-images/manifest.json` record the intervals and captures. The
camera math/control test passes. Temporary shader/parameter tracing has been
removed from source and the candidate runtime.

The user additionally reports trees disappearing until a collision occurs.
That issue is queued after the water fix; it has not yet been reproduced or
attributed to a particular culling change.

The water candidate is installed in `private/acnh/build/acnh.exe` with its
matching guest library. Runtime SHA256 is
`d37b54b51dae2125ca0992f173c7b413b684fb51edd825c998ea7070cf205aae`; guest SHA256 is
`019dac144bcf642b7a84e226d1b7f950b3e4ad911b25fb4d99c907bddf87522b`.
The previous pair is backed up in
`private/freecamera/install-backups/20261115-114940-water`; `install-water.json`
records promotion. Installed-build checks passed for inventory, tent entry,
native indoor camera rotation, exit, and restored outdoor orbit. Screenshots:
`water-installed-inventory_03121.png`, `water-installed-interior_04021.png`,
`water-installed-indoor-camera_04681.png`, `water-installed-return_05881.png`,
and `water-installed-field-orbit_14761.png`. No tree-culling change is included.

### Foreground refresh after camera changes

The user clarified that movement, rather than collision itself, may restore
missing trees. A live read-only test found that rotating the camera changed
`mTreeArea*` values but left the foreground selection rectangles unchanged.
Moving then refreshed them (`tree-refresh-baseline.json`). Native `10d5570`
compares the player's five-unit cell at `10d6618..10d6624` and skips selection
rebuilding when it is unchanged. `1125ddc` constructs the tree/general regions;
the four stored regions are owner+1e278/1e2a8/1e2d8/1e308.

The new `10d660c` hook sets only the temporary register's force-refresh bit when
the camera's foreground/tree footprint changes. It retains the native list
update and persistent gameplay flags. Whole-unit bounds suppress sub-unit
projection jitter inside the existing twenty-unit footprint padding. Each
foreground owner consumes a revision once; stationary unchanged views do not
continually rebuild. `NX_FREECAMERA_FOREGROUND_REFRESH=0` disables this change.
The updated 3.0.3 guest hook library is required.

The candidate's stationary-player test kept cell (88,195) unchanged while
rotation updated the tree rectangle from (2,167)..(116,271) to
(49,168)..(157,278). A further 300 idle frames produced no new refreshes and no
rectangle changes (`tree-refresh-stationary-result.json`). Camera tests pass.
The zoomed-out full orbit was reviewed in `tree-refresh-wide-review.png`;
inventory, tent entry and native indoor rotation were also checked. This proves
the stale-selection defect and its correction; it does not establish that every
possible disappearing-tree case has the same cause. No tree geometry, material,
detail level, or draw-distance budget was changed.

Promoted the tested pair after also verifying exit and restored outdoor orbit
(`tree-refresh-return_15361.png`). Runtime SHA256:
`7144e9ccd691d4e073eaf46bbe763a19e30c757bfc4e3862c04e6b8443597fff`;
guest SHA256: `f335b05c731f46eb92af3182e1fe4a6d86cc1edd113b27a59dc41ffcd2e71106`.
The water-fixed previous pair is backed up in
`private/freecamera/install-backups/20261115-120628-foreground`.
`install-foreground.json` records the promotion. The installed executable passed
the stationary-player refresh test again: all four rectangles changed on camera
rotation, player cell remained (88,195), and 315 settled frames produced no new
refreshes (`tree-refresh-installed-result.json`). The zoomed-out candidate run
reported approximately 60 FPS in the sampled settled view; this is not a matched
performance benchmark or a guarantee for other scenes.

The combined installed build also passed the original river reproduction after
the foreground-refresh change. The logged eye (304.1,271.4,1022.1), target
(593.0,12.0,794.7), and water rectangle (32,10)..(129,106) match the water-fix
validation. `water-tree-combined_03061.png` shows the upstream and far-right
surface filled; `water-tree-combined-result.json` pins the installed hashes and
capture. The isolated harness was then stopped.

Current wrap-up status: AO optimization, river surface selection, and stationary
camera foreground refresh are implemented, tested and installed. The ineffective
foreground-distance cap and diagnostic culling bypasses were rejected and are
not shipped. Broader distant-detail/outline behavior, distant terrain cutoff,
and horizon coverage remain unproven or incomplete. They are deferred under the
user's instruction not to start new investigations; the full camera goal is
not claimed complete. The earlier entries above are a chronological investigation
record, including superseded candidate and installation states.

### Zoom-dependent foreground haze

The camera branch includes the Saves-menu background scan via cherry-pick
`48cf681`. The subsequent foreground-haze report was tested independently of
the existing vertex dark-fog adapter. In matched 3x-zoom captures, disabling
vertex dark fog removed distant haze but left the foreground wash
(`fog-layer-ab-images/`). Park UBER has a separate fragment scattering term:
`clamp(viewDepth-c7[20].z,0,c7[20].y)`, which adds light to the material result.
Its measured onset was about 149 units and range 104, while zoom added about
300 units to camera-to-player distance. The existing correction did not cover
this term.

Two repeated disabled/corrected/native scattering sequences at one fixed pose
isolate that term (`scatter-layer-ab-images/manifest.json`). Correcting its onset
removes the foreground wash while retaining distant scattering; disabling it
entirely also removes the distant atmospheric contribution. In a fixed foreground
grass crop, native scattering averaged about RGB (190,213,134), versus corrected
(170,205,113) and disabled (171,207,114) in the second sequence. Animation/time
changes prevent pixel-exact comparisons; `foreground-comparison.json` records
the crop and both sequences. These are rendered RGB values, not linear-light
measurements.

The production adapter adds the existing zoom-depth offset only to fragment
`c7[20].z`, in host uploads for profiled Park UBER shaders during outdoor play.
It preserves range, weather colours, intensity and native zoom. Invalid inputs
and compute-written blocks remain untouched. `NX_FREECAMERA_SCATTERING=0`
disables this correction for comparison. The profile generator now writes V2
with 1,351 vertex and 2,013 fragment hashes; the runtime also reads legacy V1
profiles, which retain only the previous vertex adapter. Regenerate/install
`mods/freecamera/view_fog_shaders.txt` together with the new runtime.

Camera math tests verify unchanged scattering weights before/at/beyond the
player at zoom .25, 1 and 3, plus invalid-input handling. Camera and background
scan tests pass. The final candidate was reviewed across low-angle headings at
3x zoom (`scattering-low-review.png`), native reset, close zoom, tent entry and
return outdoors (`scattering-native-reset_05761.png`,
`scattering-close-zoom_06601.png`, `scattering-interior_08581.png`,
`scattering-return_10021.png`). This covers the reproduced daytime zoom wash,
not every weather combination. Temporary layer-disable diagnostics are removed.

### Softer outdoor lighting (2026-11-15)

The remaining wash includes intentional game lighting. The user requested a
less washed-out look even where that changes the original art direction.
Separate layer tests identified the Park UBER directional glint as the largest
shallow-angle contributor; simply adjusting material Fresnel/rim strengths was
insufficient and those material edits were discarded.

The outdoor adapter now reduces the directional glint strength (`c7[76].w`)
to 25%, and below 45 degrees limits its centre-view response to the same
azimuth at 45 degrees. This covers the default mod camera as well as shallower
views. It never increases the glint. It also uses 75% of the atmospheric
scattering intensity (`c7[23].y`) and 60% of the dark-fog blend intensity
(`c5[10].w`). These are deliberate visual choices, not corrections of shader
arithmetic. Atmospheric colour, distance ramps and weather variation remain,
as do material albedo, wave normals, shadows and other reflection terms.

The changes are host-upload-only, scoped to the active outdoor free camera and
profiled Park UBER shaders. The V2 shader profile is required. Interiors and
UI are outside that scope. `NX_FREECAMERA_LIGHTING=0` restores the original
lighting strengths while retaining the camera-relative fog and zoom corrections.
`NX_FREECAMERA_GLARE=0` disables just the directional-glint adjustment.

The glint cap uses the centre ray, not a per-pixel highlight clamp. Tests cover
its reference response, rotation of both light and view, steep/away-facing
views and invalid inputs. Camera and background-scan tests pass.

The final default-zoom old/new/old sequence is in
`soft-lighting-default-images/manifest.json`. A fixed grass crop changed from
RGB (125,192,86) to (117,189,78), returning to (125,192,86) in the repeat;
`comparison.json` records crop coordinates and tree measurements. These are
rendered RGB averages, not linear-light measurements. The final production
executable was also reviewed on the title screen, at shallow zoomed-out angles,
and through an eight-frame orbit (`soft-lighting-title_01681.png`,
`soft-lighting-low_04141.png`, `soft-lighting-orbit-review.png`). Water retains
wave detail and highlights, with reduced broad white glare. The contact sheet's
ninth empty tile is padding. Weather/night coverage remains untested.

### Upper-corner surface coverage (2026-11-15)

Land and water submission now use the camera footprint for the default/reset
orbit as well as modified orbits. Camera303 already changes the default/title
camera, so the previous identity-orbit early returns retained undersized stock
surface rectangles on the title screen. Terrain, land and water footprints now
union with the native tile rectangle, preventing finite-distance camera bounds
from reducing native coverage at other angles.

The camera test passes with the traced title pose, both upper corners, rotated
coverage containment and a larger native rectangle. A separate hidden session
verified the title screen, outdoor entry, shallow orbit and reset; captures are
`private/freecamera/perf-test/shots/corners-title_01561.png`,
`corners-enter_03481.png` and `corners-orbit_04056.png`. The title trace confirms
land and water expansion without camera input. This covers the tested poses;
all islands and camera combinations have not been exhaustively checked.
