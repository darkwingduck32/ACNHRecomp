# ACNH 3.0.3 function port ledger

The batch targets 10 verified gameplay functions. All ten are verified. See [the final batch replay](batch-verification.md).

The `port/acnh-engine` branch is checked out at
`C:/Users/bettr/.codex/worktrees/bb9b/ACNHRecomp` and started from
`e15b0c3cd9f6aaaaecfced82b73c8d284ee6987f`. The original files are under
`H:/ACNH/ACNHRecomp/private/game`. Main NSO build ID
`FF1D1C05670DB6021C85B624A710B963` matches 3.0.3 in `games/acnh/versions.txt`.

| Module and entry | Provisional name | Stage | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `FF1D1C05670DB6021C85B624A710B963:0x5f0400` | Heading from horizontal direction | Verified | [Pilot](heading.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x5f05f8` | Shortest signed angular difference | Verified | [Evidence](angle-difference.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x3db4b60` | Step a position toward a target | Verified | [Evidence](approach-position.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x8807cc` | Resolve a field table row | Verified | [Evidence](field-record.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x8823a0` | Resolve a field table column | Verified | [Evidence](field-column.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x5f06dc` | Cosine of the shortest angular separation | Live scenario blocked | [Evidence](angular-cosine.md) | Retry when gameplay reliably hits either known caller. |
| `FF1D1C05670DB6021C85B624A710B963:0x15e0e30` | Check a field flag near a point | Live scenario blocked | [Evidence](field-flag.md) | Retry with a field interaction that reaches a known caller. |
| `FF1D1C05670DB6021C85B624A710B963:0x23255e4` | Accept record category 2 or 3 | Verified | [Evidence](record-category.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x2325688` | Accept record category 2 or 4 | Verified | [Evidence](record-category.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x232572c` | Accept record category 2 or 5 | Verified | [Evidence](record-category.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x23257d0` | Accept record category 2 or 6 | Verified | [Evidence](record-category.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x2325874` | Accept record category 2 or 7 | Verified | [Evidence](record-category.md) | Batch complete. |
| `FF1D1C05670DB6021C85B624A710B963:0x1a03610` | Find index for auxiliary item type | Discovery | Exact unwind range of 0xec bytes. Called at `main+0x267d768`; reads a runtime table from the main image. | Obtain a live snapshot of the table before replay. |
| `FF1D1C05670DB6021C85B624A710B963:0x267d6c4` | Resolve field item ID | Discovery | Exact unwind range of 0xf0 bytes. Called at `main+0x21f76b0` and `main+0x23dfe18`; calls the preceding row on one branch. | Establish the leaf dependency and its capture prerequisites. |

Count a row after independent whole-function comparison, a matching gameplay run
that hits the replacement, passing relevant regressions, and a replay through
the promoted default. Keep the original selectable.

## Active batch 2

This batch targets ten verified ports on the same 3.0.3 build, starting
with the [candidate queue](next-candidates.md). Verified so far, 7.

| Main entry | Provisional function | Stage | Next action |
| --- | --- | --- | --- |
| `0x24699b8` | Initialize working action bits once | Verified | Original, opt-in, and default route validated in the notes below. |
| `0x1218220` | Read a tile record's bit flag | Verified | The extended-field trial replayed the promoted default. |
| `0x1146470` | Resolve a column using a 25-slot cache | Verified | The muted trial replayed the promoted default. |
| `0x113d890` | Test a bit in a cached record column | Verified | The muted trial replayed the promoted default. |
| `0x1632d80` | Initialize four inline string buffers and descriptors | Verified | The combined default trial matched 42 cases. |
| `0x5bd550` | Hash a terminated byte string with a seed | Verified | The combined default trial matched 257 cases. |
| `0x112b8ec` | Advance the gameplay RNG and draw below 100 | Verified | The combined default trial matched 518 cases. |
| `0x24642a8` | Set or clear an action-mask bit | Synthetic comparison | Find a player or NPC state that reaches a known caller. |
| `0x1281c54` | Look up a half-tile record | Live trial | Compare both routes during the shovel survey. |
| `0x3db4ed0` | Multiply a three-vector by a 3-by-3 matrix | Synthetic comparison | Find a scene that reaches the matrix multiply at `main+0x3dafc64`. |

The action-mask no-op candidate failed case 0. The C++ implementation
matched all 228 fixtures against two fresh Unicorn runs, including the
ordered 8-byte copy, 4-byte bit clear and one-byte initialization write.
C++ is the default on the validated build. Set `NX_ACNH_ACTION_MASK_TRACE=1` to capture the object
fields around each call.

```powershell
& private/venv/Scripts/python -m private.heading.compare_leaf action_mask
```

The original and opt-in routes matched the one observed call from
`main+0x2373540`. The default route hit the C++ initializer on the same
call in `tree-shake-original-tile1/nx.log`, which selects the original
route for the tile query only. Each comparison matched two Unicorn
executions for all 229 cases. The isolated Windows Release player passed
all 13 CTest tests. The live scenario covered initialization. Synthetic
cases also cover a set bit 30 and already-initialized objects.

Run from this worktree to repeat the comparison against the local captures.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
& private/venv/Scripts/python -m private.heading.compare_leaf action_mask --live "$r/tree-shake-original-mask1/nx.log" "$r/tree-shake-original-tile1/nx.log"
```

Use `NX_ACNH_ACTION_MASK=original` for the stock path.

### Tile record and extended cache

The tile query's constant-false candidate failed case 1. All 242
synthetic cases and 24 paired live snapshots passed against two fresh
original executions. Both opt-in gameplay runs hit the query 12 times
from `main+0x14c4a70`. The default also matched all 266 cases against the original capture.
Its capture is "tree-shake-original-ext1/nx.log". `NX_ACNH_TILE_RECORD_FLAG=original` selects the original.
The captures are `tree-shake-original-tile1/nx.log` and
`tree-shake-cpp-tile1/nx.log` in the campaign artifact directory.

The shared cache helper now supports `main+0x1146470` and
`main+0x113d890`. The no-op candidates failed on cold initialization.
The replacements matched 241 and 242 synthetic cases including ordered
writes, and the 14-slot query still passed all 321 synthetic cases.
Each entry hit its replacement 12 times in the opt-in trial and 12 times
through the default route. The muted comparison used
`tree-shake-original-state2/nx.log` and `tree-shake-cpp-state2/nx.log`,
where the latter did not set either extended-field override. All 265
column lookup cases and 266 bit query cases matched two Unicorn runs,
including the full cache state before and after each call. Switch back
with `NX_ACNH_FIELD_COLUMN_25=original` and `NX_ACNH_CACHED_FIELD_BIT=original`.

```powershell
& private/venv/Scripts/python -m private.heading.compare_leaf tile_record_flag
& private/venv/Scripts/python -m private.heading.compare_leaf field_column_25
```

### Inline string initialization, gameplay RNG and string hash

The no-op string initializer, RNG and hash candidates failed their
first differing reference cases. The native replacements matched 18,
512 and 209 synthetic inputs respectively. The muted tree-shake trial
in `tree-shake-{original,cpp}-state2/nx.log` captured 12 string
initializations and three RNG draws per route, for 42 and 518 total
cases against two Unicorn executions. The replay uses each exact
observed seed and allocated object snapshot. The RNG output and all
ordered state writes matched. The constructor replay compared all
512 object bytes and descriptor writes.

The terminated hash comparison of `tree-shake-{original,cpp}-hash1/nx.log`
matched 48 calls covering the startup and field-key callers, all with a zero
seed. The 257 total cases include synthetic nonzero seeds, alignment,
and lengths around four-byte boundaries. Redirecting the 9,107 static
hash call sites added about 75 KiB to the full generated DLL. Both routes
completed the live scenario. The Release build passed all 13 CTest tests,
and the dispatch, hook profile and replay suites passed nine tests.
The single-instance default trial in `tree-shake-default-hash3/nx.log` hit all three replacements without route overrides and matched the original captures.

The `treewhole` fixture's `main.dat` SHA-256 is
`070c4b0ea8cb6078fa1e1132b7e3106860d67db6c66fdd4089ca261728e6b0d3`.
Use `NX_ACNH_INLINE_STRINGS=original`, `NX_ACNH_GAMEPLAY_PERCENT=original`
or `NX_ACNH_TERMINATED_HASH=original` to select each original function.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
& private/venv/Scripts/python -m private.heading.compare_leaf field_state --live "$r/tree-shake-original-state2/nx.log" "$r/tree-shake-cpp-state2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf terminated_hash --live "$r/tree-shake-original-hash1/nx.log" "$r/tree-shake-cpp-hash1/nx.log"
```

### Half-tile lookup, action bit updates and vector multiplication

The synthetic no-op candidates left expected writes missing or returned
null for a populated record. Two reference runs agreed with the native
implementations on 455 half-tile, 90 action-bit and 256 finite-vector
cases. The offline vector cases cover input and output aliasing.

A muted shovel survey using the disposable `half` fixture recorded 12
calls to `main+0x1281c54` from `main+0x21f787c` in
`tree-shake-original-shovel1/console.log`. Its `main.dat` SHA-256 is
`5e2c0538fdd036a28d6fbe733723002a7ad75cb7e37134cdb7a8cfaf9c34fe4f`.
The test runner sets `NX_MUTE=1` and serializes game launches. The paired shovel trial uses
`PORT_FIXTURE=half`, `PORT_SCENARIO=shovel` and `PORT_TRACE_HALF_TILE=1`.
The opt-in path is `NX_ACNH_HALF_TILE_RECORD=cpp`.

```powershell
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives
```

The tree-shake and shovel probes did not reach the action-bit writer or
matrix multiply. The original behavior remains active for those entries.
The next discovery probe includes nearby field predicates and both
outstanding call sites.
