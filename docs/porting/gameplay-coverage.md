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
