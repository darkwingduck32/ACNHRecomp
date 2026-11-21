# ACNH gameplay coverage baseline

Windows baseline at `1d82d997d05ba96f33e5d1a3322c6464e7c6c8f6` on
3.0.3 main build `FF1D1C05670DB6021C85B624A710B963`. All runs used
the default route, a disposable fixture and one muted game at a time.

## Measurements

A scratch player sampled Windows thread instruction pointers at 47 Hz
inside the listed presented-frame intervals. Each sample contributes
once, to the module containing its exclusive instruction pointer.
The `game` thread is the Windows thread that calls `nx::boot`. The
`gNN` guest workers form a separate bucket. The presentation thread
is identified by the runtime's logged thread ID and counted separately.
The guest DLL's exported function table resolves exact native starts
to guest addresses. Ambiguous and unmatched addresses remain in the
DLL count. Sleeping threads are included in the denominators. These
are short-window samples of execution, with startup excluded.

| Activity | Frames, end exclusive | Game DLL samples / game thread | DLL samples resolved on game thread | Guest worker DLL / worker samples | Present DLL / present samples |
| --- | --- | ---: | ---: | ---: | ---: |
| Tree shake | 1980–2421 | 118 / 346 (34.1%) | 108 | 238 / 18,684 | 0 / 346 |
| Movement | 1980–2427 | 156 / 350 (44.6%) | 140 | 227 / 18,900 | 0 / 350 |
| Shovel field | 4560–5001 | 130 / 345 (37.7%) | 114 | 251 / 18,630 | 0 / 345 |

The shovel run matched the original survey, neighborhood and final
decision on all four focal calls at (40, 69). The tree run logged five
typed heading calls at the tree-shake site. The movement input used
only the left stick during its activity window. No sample ring dropped
records. The full DLL denominator includes generated code and any
support code linked into that DLL. These measurements describe the
fixed Windows workload and cannot be compared numerically to Linux
profiles collected with different threads or sampler settings.

The reusable discovery probe covers 83 of the 100 current queue
entries and emits at most 12 records per entry per process. In the
activity log slices, original BL decoding validated seven records for
tree shaking, five for movement, and one for shovel. Entry `0x1298524`
was reached at `0x129302c` in tree shaking (4) and `0x12933a0` in shovel
(1). Entry `0x22254c` was reached at `0x221bac` in tree shaking (3) and
movement (5). Startup can consume the probe's budget. Use these
records to establish caller reachability.

## Paired terrain-worker measurement

The pairs use runtime `09404c15337a1c7f5a03398a306d4d6ec9ce9398`,
the same fresh worker-dispatch DLL on both routes, and the original
fixture schedules and frame windows above. The only route toggle was
`NX_ACNH_WORKER_GRID_HEIGHT=original` versus its default. The scratch
player retained the same Vulkan cache seed on each launch by sharing
it read-only. It suppressed cache persistence on both routes to keep
all six saves and captures on disk. Worker, heading, survey and input
validation traces were disabled. The DLL's bounded discovery probes
were present on both routes.

| Activity | Worker route | Game DLL / game thread | Worker DLL / worker threads | Generated grid samples game / workers |
| --- | --- | ---: | ---: | ---: |
| Tree shake | Original | 140 / 345 | 209 / 18,630 | 3 / 2 |
| Tree shake | Default | 137 / 346 | 253 / 18,684 | 0 / 1 |
| Movement | Original | 142 / 350 | 235 / 18,900 | 3 / 5 |
| Movement | Default | 137 / 351 | 209 / 18,954 | 2 / 1 |
| Shovel | Original | 129 / 343 | 226 / 18,522 | 1 / 5 |
| Shovel | Default | 118 / 345 | 229 / 18,630 | 1 / 0 |

Presentation had zero DLL samples on both routes. Its original/default
denominators were 345/346 for tree, 350/351 for movement, and 343/345
for shovel. Unresolved or ambiguous DLL samples on the game thread
were 8/12, 11/11, and 14/7. The corresponding worker counts were
10/16, 11/15, and 11/9. Original and default shovel runs also matched
all four final field decisions.

Counts are exclusive instruction-pointer samples at 47 Hz, with the
DLL numerator and full thread-role denominator shown separately. The
grid column counts exact samples of the generated `0x862700` body
across all its callers. Its remaining generated routes can still
contribute. Sample counts in these short windows do not establish a
speedup. See the ledger for the separate C++ route count on the
validated tree fixture.

The DLL SHA-256 is
`05159291c8595afa430d385a9adf9e0b7560b1c3bbade0f7e07f80f32d4069bd`.
The local capture tags are `paired-{tree,movement,shovel}-{original,default}1`.
The code-only summary command is
`python -m private.heading.worker_grid_profile_pairs <run root> <DLL> <cache seed>`.

## Shared generated work to capture

| Body | Thread | Tree | Movement | Shovel |
| --- | --- | ---: | ---: | ---: |
| `0x38ea910` | Game | 9 | 12 | 10 |
| `0x146b080` | Game | 2 | 9 | 4 |
| `0x15eb1a0` | Game | 1 | 3 | 2 |
| `0x11fc610` | Worker | 14 | 20 | 28 |
| `0x146ab80` | Worker | 18 | 15 | 12 |
| `0x146a660` | Worker | 11 | 12 | 11 |
| `0x11fcb00` | Worker | 6 | 6 | 3 |

The next port group starts in render-state selection. The game-thread
body at `0x38ea910` has the highest sample count in all three activities.
It has 50 statically decoded direct callers. A repeat of the tree
window with native stack sampling found 54 samples in that body. All
54 unwound through the direct calls at `0x38ea18c` or `0x38facf8`,
27 samples per caller.

A bounded generated-route trace at those two callers completed 93
calls, all with W0=1. The selected resource pointer matched the cache
in 27 captures and changed in 66. The matching-resource path made
8–12 callbacks. The changed-resource path made 9–29. The trace
retained up to 24 ordered call sites and targets per invocation and
checked those sites against the original instructions. The live fixture
did not reach the empty-resource return. Twenty synthetic controls
executed that return twice in AArch64 with W0=0 and no external writes
or callbacks.

A detailed capture at `0x38ea18c` produced a completed matching-resource
invocation. The fixture reconstructs the memory the parent touched,
including the caller's stack-backed state passed in X0. Two independent
executions of the original parent matched W0=1, 24 ordered external
stores and nine ordered callbacks. The replay supplied the captured
callback returns. There were no read-visible byte changes to inject.
The callback bodies and their graphics effects still need validation.
The render selector remains on the generated route.

The shared field query chain is the next worker-thread group. The original
`0x11fc610` and `0x11fcb00` call `0x146a660` at five direct sites.
`0x146a660` calls `0x146ab80` at `0x146aa88`. Both worker parents also
call grid height `0x862700`, which already has a typed path at other
sites and still appears in generated samples here. Begin by pinning the
two worker entry routes and capturing the shared child call at one
completed original invocation per parent. Record callback targets and
order, arguments, atomic state transitions, and output writes before
selecting a typed boundary. `0x146b080` is a related game-thread
capture candidate. Its four direct sites are in `0x1461a34`.

The worker-grid checkpoint routes `0x11fca98`, `0x11fcac0` and
`0x11fd70c` to the typed height sampler. The worker parents,
`0x146a660`, and `0x146ab80` still run generated. The next capture
will identify the callback and enqueue effects of one complete
`0x146a660` invocation. See the [verification ledger](ledger.md#terrain-worker-grid-sampling).

Both read-only culler calls now have a repeatable Windows fixture.
Two muted tree runs with identical input schedules each captured
192 calls per body during field loading. At `0x146a8ac`, the
`0x63e274` capture uses the context from X25, a point at SP+8,
and radius S0 loaded from SP+0x14. At `0x146ab38`, the
`0x14e9d34` capture uses the derived context, point at SP+8 and
vector at SP+0x18. All captured results were W0=0 and W0=1
respectively. The first-192 capture budgets were exhausted
before the action windows.

Private typed candidates matched two original AArch64 executions
per case for 1,012 portable controls and 768 captured inputs,
including exact S0 bits and zero external writes. The portable
controls reached both boolean outcomes for both functions.
[The verified port](ledger.md#terrain-worker-cull-predicates) now uses
typed C++ at these two calls. The full worker culler, enqueue and
atomic worker paths remain generated.

The private runs are `worker-cull-original1` and
`worker-cull-original2` under `coverage-runs`. Both use the same
one-shard culler trace library. The second run reused the read-only
Vulkan cache setup. The local differential command is
`python -m private.heading.worker_cull_predicates --live <console logs>`.

The terrain-clearance predicate `0x15eb1a0` has a Windows original-route
fixture covering center hits and full ring misses. Two sites reached both
boolean results across each fixture. Three more sites reached full ring
misses during both the movement and shovel action windows. The capture
preserves the point and radius bits, selector, heading step and trig rows,
ordered half-tile arguments, and terrain flag results. Samples are capped
per caller, selector, and result and establish call-site coverage.
The five observed sites now use typed clearance and flag queries.
The dispatch at other sites, rendering, worker claim and publication
remain generated. The sampled gameplay gates establish the execution
of the typed path. See the [verification ledger](ledger.md#terrain-clearance-and-flag-queries).

The shared worker culler now has a repeatable generated-route capture
at five links. The tree action window reached both warm visibility
outcomes after the volume miss, including ten-plane checks and the
second predicate when all planes passed. The warm loop at three worker
links now uses C++ after the first plane. The default tree trace supports
a lower bound of 8,470,528 typed tail row tests. The selector, first
plane, byte stores, and enqueue still run generated. See the
[worker expansion checkpoint](ledger.md#warm-plane-loop-at-two-additional-worker-calls).

Terrain draw at `0x146b080` repeats its masked instance rejection
across all three activities. The three common callers each contribute
roughly one-third of the draw invocations. Passes 0 and 2 dominate
the rejections. The original tree chain capture adds 3,166,114
rejections after the first in a run and 24 repeatable chain replays.
List publication and reset remain the focus of the producer audit.
See the [draw chain checkpoint](ledger.md#terrain-draw-repeated-rejection-chains).

The alternate shovel leg remains on the generated route pending a
repeatable trigger.

Validate each chosen function independently against original AArch64.
Then share one original, one opt-in C++, and one default run per
coherent group, with per-function call and result evidence. Rerun the
same three activity windows after promotion to measure the change in
generated execution. The existing typed shovel query, first-axis
traversal, scoped terrain/height/collision/stage paths, and tree heading
remain selectable against their generated routes.

## Activity recipes

Start each scenario from a fresh local save copy with a fixed in-game
time, offline play, mute enabled and guest input tied to presented
frames. Use a 440–447 frame action window after loading the field.

- **Tree shake.** Use a save with an unshaken fruit tree just below
  the player. Finish the title sequence, walk down for 12 frames, wait
  for the approach to settle, then press A once. Confirm the
  tree-shake heading call or fruit drop. The Windows action started
  around frame 2071.
- **Movement.** Use the same ready unshaken-tree save. Starting at
  approximately frame 2071, walk left, up, right and down for 64 frames
  each, with a 20-frame settle between directions. Leave the action
  button alone throughout the window.
- **Shovel/field.** Use a save with a shovel available and a buried
  fossil next to the player's approach. For the deeper Windows fixture,
  four rose records surround the target. Load the field, equip the
  shovel, walk down for 12 frames, wait for the approach to settle,
  then press A once. Start sampling after the shovel is equipped and
  before the final approach. Confirm the focal field survey and final
  decision. The Windows action started around frame 4651.

Private run labels are `coverage-tree3`, `coverage-movement2`,
and `coverage-shovel2`, under the campaign's `tree-shake-default-*`
directories. The private scratch scripts are `activity_coverage`,
`activity_rank`, `activity_samples` and `build_activity_profiler`.
The guest DLL SHA-256 is
`44ad0a959a23e1c9eb56c7fa9edf5356bfbe273264e835d7db80263d22865d76`.
Recorded inputs, saves, binary files and raw samples remain local.

The render-only Windows reruns use `render-tree1` for stack samples,
`render-state-original1` for caller and callback shapes, and
`render-state-detail1` for the replay fixture in the private
`coverage-runs` directory. The local replay command is
`python -m private.heading.replay_render_selector_capture <console.log>`.

The matching packed resource prefix now runs in C++ at the two
witnessed render call sites. The muted default tree gate captured
136 such continuations, each skipping 27 generated guest
instructions. The disjoint descriptor transfer also runs in C++
for two witnessed shapes at those callers. The default tree gate
captured 32 kind-2 joins skipping 48 generated instructions each
and 32 kind-5 joins skipping 15 each. These are bounded trace
counts. The generated selector still owns unsupported shapes,
context reset, descriptor choice, binding and draw effects.
See [the render selector checkpoint](ledger.md#matching-packed-render-resource-selection)
and [the descriptor transfer checkpoint](ledger.md#render-descriptor-transfer).

## Paired render coverage at `aecbe8f`

Six muted runs used one Windows player, guest DLL and immutable
pipeline cache. Each activity pair used the same fixture, input
schedule and frame window. The original run set both render
route switches to `original`. The default run used both scoped
C++ paths. Validation traces were disabled. Game-thread samples
were collected at 47 Hz with zero ring drops.

| Activity | Original DLL leaves | Default DLL leaves | Original → default selector leaves | Original → default terrain draw leaves |
| --- | ---: | ---: | ---: | ---: |
| Tree shake | 132/347 | 136/345 | 7 → 9 | 5 → 6 |
| Movement | 141/350 | 123/350 | 14 → 7 | 5 → 6 |
| Shovel | 112/345 | 118/345 | 8 → 14 | 4 → 3 |

Of the DLL leaves, 112 and 121 in tree, 131 and 112 in
movement, and 99 and 105 in shovel resolved uniquely to
generated guest functions on the original and default routes
respectively. The other DLL samples are unresolved or
ambiguous. Host leaves include runtime and C++ work and do
not have a reliable typed-code total.

The selector at `0x38ea910` remains first in both rankings
by activity breadth and mean exclusive leaf share. Terrain
draw `0x146b080` remains second. The default selector caller
`0x38facb0` follows at 2, 2 and 4 leaves across the three
activities. The sample counts vary across these single
pairs and provide no speed estimate. The traced instruction
counts above show exactly which work moved into C++.

The next bounded render group is the binding-cache decision
at `0x38ead64`. One original detailed tree capture read
both IDs as `0xffffffff` and reached `0x38eadf4` without
a binding refresh. Independent AArch64 controls also reach
the first and second refresh boundaries. Next capture which
cases occur at the two verified callers in the fixed activity
windows. The calls to `0x380d290` and their state effects
stay generated. Draw list traversal also stays generated
while its ownership is unresolved.

The private paired run labels are
`render-paired-{tree,movement,shovel}-{original,default}1`.
The private verifier is `private.heading.render_profile_pairs`.
The shared DLL SHA-256 is
`df38820aa8d2eb06cf1ebf20fe4f4400f9aa4222000679c3a4478f5e25371147`.

The two scoped callers took the enabled, empty binding-cache
route 1,787,992 times across the three original capture windows.
The verified default tree window counted 575,921 C++
continuations through this decision. Both binding refreshes
and the submit wrapper remain generated.
See [the binding cache checkpoint](ledger.md#empty-render-binding-cache).
