# ACNH 3.0.3 function port ledger

The first batch verified ten gameplay functions. See [the final batch replay](batch-verification.md).

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

## Batch 2

This batch verified ten ports on the same 3.0.3 build. The
[candidate queue](next-candidates.md) records the original selection.

| Main entry | Provisional function | Stage | Next action |
| --- | --- | --- | --- |
| `0x24699b8` | Initialize working action bits once | Verified | Original, opt-in, and default route validated in the notes below. |
| `0x1218220` | Read a tile record's bit flag | Verified | The extended-field trial replayed the promoted default. |
| `0x1146470` | Resolve a column using a 25-slot cache | Verified | The muted trial replayed the promoted default. |
| `0x113d890` | Test a bit in a cached record column | Verified | The muted trial replayed the promoted default. |
| `0x1632d80` | Initialize four inline string buffers and descriptors | Verified | The combined default trial matched 42 cases. |
| `0x5bd550` | Hash a terminated byte string with a seed | Verified | The combined default trial matched 257 cases. |
| `0x112b8ec` | Advance the gameplay RNG and draw below 100 | Verified | The combined default trial matched 518 cases. |
| `0x24642a8` | Set or clear an action-mask bit | Live scenario blocked | Retry when a player or NPC state reaches a known caller. |
| `0x1281c54` | Look up a half-tile record | Verified | The category-mask trial also exercised the promoted shovel lookup. |
| `0x3db4ed0` | Multiply a three-vector by a 3-by-3 matrix | Live scenario blocked | Retry when a scene reaches the matrix multiply at `main+0x3dafc64`. |
| `0x849898` | Test a tile's category bytes against a bit mask | Verified | The field-key trial also exercised the promoted category mask. |
| `0x1140fd0` | Pack four field category codes into a key | Verified | The final shovel trial exercised the default with no route overrides. |

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
matrix multiply. Both entries retain their original routes.

The paired shovel trial in `tree-shake-{original,cpp}-halve2/nx.log`
captured 24 calls on each route. The 503 total cases matched two original
executions with exact guest addresses, outputs and writes. The gameplay
snapshots include the tile grid and all four slots in the selected record.
Set `NX_ACNH_HALF_TILE_RECORD=original` to restore the stock route.

### Tile category masks

The additional candidate at `main+0x849898` has unwind size `0x118`.
It accepts an owner, coordinate record, and three-word mask. It resolves
a 0x34-byte tile record and returns whether any of its four category bytes
selects a set bit below category 73. It only writes to its own stack.
The probe in `tree-shake-original-spare1/console.log` recorded six calls at
each of `main+0x14c467c` and `main+0x14c46e4`. The probe SHA-256 is
`ee10a07d0c5cd5fbec69e6ff6ce19fd65b48dffdf30a7b2a91ef8d340a16a444`.
A constant-false port failed case 1066. The replacement matched all 768
synthetic cases against fresh reference executions. The native route is
opt-in via `NX_ACNH_TILE_CATEGORY_MASK=cpp`.

### Four-code field key

A broader shovel probe in `tree-shake-original-more1/console.log`
covered 41 entries and recorded 12 calls to `main+0x1140fd0` from
`main+0x1141810`. The trace SHA-256 is
`3482eefe2937866a365f528050f49054ca0dcfb5666c6878486b66f995d93974`.
Neither the action-bit writer nor matrix multiply appeared in that
probe. Those two candidates retain their synthetic comparisons and
original routes. The tile category mask and field key are their batch
replacements.

The field key's unwind size is `0x74`. It takes the low byte of each
of X0 through X3. If every code is 0, 1 or 2, it reads a three-byte
mapping at `main+0x40749e0` and packs the mapped values into W0.
Other inputs return zero. The no-op failed case 1. The replacement
matched 2,592 synthetic cases against two Unicorn runs, covering valid
and invalid codes, high argument bits and alternate mappings.
The opt-in run in `tree-shake-{original,cpp}-casekey1/nx.log` captured
12 calls on each route. All 2,616 cases matched against two Unicorn
runs. Use `NX_ACNH_FIELD_CASE_KEY=original` to restore the stock path.

```powershell
& private/venv/Scripts/python -m private.heading.compare_leaf field_case_key
```

### Final gameplay comparison

The final muted shovel trial used `batch2_final_generated.dll`.
`tree-shake-default-batch2final/nx.log` has SHA-256
`5f349c0a5cad55e04cc24dfb3734915e9d7aede25139ff0325ba97561ac37a4f`.
It captured 24 half-tile calls, 12 category-mask calls, and 12 field-key
calls, all through their default routes. Two Unicorn runs agreed for all
503, 1,176 and 2,616 replay cases respectively. The original and opt-in
field-key captures matched with equal input frequencies. In the capped
default trace the repeats were distributed differently. Each of its
12 outputs matched an observed original call with the same input and an
independent replay of its exact captured state. The Release player passed
all 13 CTest tests and the replay and profile suites passed all nine tests.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
& private/venv/Scripts/python -m private.heading.compare_leaf field_case_key --live-default "$r/tree-shake-original-casekey1/nx.log" "$r/tree-shake-default-batch2final/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-half-tile "$r/tree-shake-original-catmask1/nx.log" "$r/tree-shake-default-batch2final/nx.log" --live-category-mask "$r/tree-shake-original-catmask1/nx.log" "$r/tree-shake-default-batch2final/nx.log"
```

## Active batch 3

The previous batch is complete. The discovery queue holds 100 candidates for verification on
main build `FF1D1C05670DB6021C85B624A710B963`. Verified so far, 33.

The discovery trace `tree-shake-original-more1/console.log` came from a
disposable shovel scenario and has SHA-256
`3482eefe2937866a365f528050f49054ca0dcfb5666c6878486b66f995d93974`.
An observed entry establishes reachability. Each port still needs a paired
original and opt-in replay, regressions, and a check of the promoted default.
All gameplay tests use the same muted single-instance launcher.

| Main entry | Unwind size | Provisional function | Observed caller | Stage |
| --- | --- | --- | --- | --- |
| `0x240f090` | `0xa0` | Resolve indexed record pointer | `0x220fd80`, `0x2215230`, `0x221f0c4`, `0x225b344` | Opt-in trial passed |
| `0x1148db0` | `0xd4` | Resolve an eight-slot cached field | `0x114169c`, `0x114182c`, `0x1141850`, `0x1141874` | Reference replay passed |
| `0x29cbe80` | `0x124` | Read cached row value, slot 12 | `0x29cd3d0` | Reference replay passed |
| `0x29cbfb0` | `0x124` | Read cached row value, slot 13 | `0x29cd3e0` | Reference replay passed |
| `0x29cc0e0` | `0x124` | Read cached row value from slot 14 | `0x29cd3f0` | Reference replay passed |
| `0x29cc210` | `0x124` | Read cached row value from slot 15 | `0x29cd400` | Reference replay passed |
| `0x29cc340` | `0x124` | Read cached row value from slot 16 | `0x29cd410` | Reference replay passed |
| `0x29cc470` | `0x124` | Read cached row value from slot 1 | `0x29cd420` | Reference replay passed |
| `0x29cc5a0` | `0x124` | Read cached row value from slot 2 | `0x29cd430` | Reference replay passed |
| `0x29cc6d0` | `0x124` | Read cached row value from slot 3 | `0x29cd440` | Reference replay passed |

The [discovery queue](batch-3-candidates.md) holds 100 candidates beyond
the implementation entries listed here. 22 queue entries have
observed direct callers.

Implementation priority is the [shovel field-query path](shovel-field-queries.md). The queue also holds observed candidates outside that path.

The indexed lookup takes an owner in X0 and an index in W1 and returns a
pointer or null in X0. It reads either a direct pointer array or a packed
record table, with no guest writes. First replay both branches and their
bounds, then use the existing traced-memory convention to capture the
shovel callers. For the shared-cache family, verify the hash and slot
from each original body and independently replay every entry, including
cold initialization and warm cache hits.

The independent NSO check found that `main+0x240f090` and the verified
`main+0x8807cc` have identical 0xa0-byte bodies. The indexed entry uses the
existing C++ implementation. It independently matched all 336 fixtures
twice against its own original entry. The Windows Release player passed
all 13 CTest tests. The native route is opt-in via
`NX_ACNH_INDEXED_RECORD=cpp`. The live trial uses the disposable shovel
fixture and captures the known caller at `main+0x221f0c4` in
`tree-shake-{original,cpp}-indexed1/nx.log`.

```powershell
& private/venv/Scripts/python -m private.heading.identify_batch3
& private/venv/Scripts/python -m private.heading.compare_leaf field_record --indexed
```

The indexed lookup opt-in trial matched 24 calls per route and all 384
cases against two reference executions. Its reported caller is inside
`main+0x221f070`, outside the proven shovel survey call chain. It remains
opt-in while implementation focuses on the shovel path.

The eight-slot lookup matched 363 independent replay cases. Each of the
eight cached row-value entries matched 122. Every no-op first failed on
cold initialization writes. The runtime routing draft is preserved under
`private/heading/field-value-cache-route.patch` for later grouping.


### Shovel implementation group

| Main entry | Unwind size | Query | Stage |
| --- | --- | --- | --- |
| `0x1281c54` | `0x15c` | Find a record in a field grid at a half-tile position | Verified through typed interface |
| `0x1285fe0` | `0x1c4` | Select a layer and test a bit on its half-tile record | Verified |

The typed interface uses field-grid, layer, half-tile and record types.
Guest register decoding and the original switch stay in the runtime
adapters. The stub failed at case 90 for the record lookup and case 546
for the layer query. The replacement matched 455 and 122 cases
respectively against two reference executions. The surrounding field
primitives still passed. Select the original layer route with
`NX_ACNH_HALF_TILE_LAYER_FLAG=original`.

```powershell
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives
```


The first shovel group used one muted original, opt-in C++, and default
gameplay run. The captures are `tree-shake-{original,cpp}-shovelqueries1`
and `tree-shake-default-shovelqueries2`. All six survey snapshots matched
in each run. The half-tile lookup matched 503 cases and the layer query
matched 170. The layer trace at `0x87c048` provided 12 identical starting
states per route. The capped terrain trace at `0x87737c` captured different
coordinates between processes. Every exact capture still matched the
independent original, including its observed output. The default trace
identified those terrain calls under `0x28732d4`, outside the survey.
The survey's call to that terrain branch needs a fixture with a diggable
target. The Release player passed all 13 CTest tests and the nine replay
and profile regressions.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-shovelqueries1/nx.log"
$c = "$r/tree-shake-default-shovelqueries2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-half-tile $o $c --live-layer-flag $o $c --match-layer-caller 87c04c --live-survey $o $c
```

### Next shovel group

| Main entry | Query | Stage |
| --- | --- | --- |
| `0x880870` | Read the diggable terrain property | Verified |
| `0x880a10` | Read the alternate terrain property | 96 reference cases passed, caller retry |
| `0x127c6ec` | Initialize the survey scratch records | Verified |

The scratch initializer is called at `0x21f759c` and `0x21f75a4`.
The replay checks the whole ordered write sequence, including when the
object aliases the empty-record template. The terrain predicates reuse
the verified packed row and column lookups. Their original switches are
`NX_ACNH_TERRAIN_DIGGABLE=original` and
`NX_ACNH_TERRAIN_ALTERNATE=original`. Use
`NX_ACNH_SHOVEL_SURVEY_INIT=original` for the scratch initializer.
The terrain captures include the parent return address so an ambient
terrain call cannot be mistaken for a shovel-survey call.

The scratch initializer ran from both survey call sites in the three
muted `whole` fixture runs. The original, opt-in and default captures are
`tree-shake-original-shovelgroup1`, `tree-shake-cpp-shovelgroup1` and
`tree-shake-default-shovelgroup2`. Each comparison matched 58 reference
cases. At the focal tile, all four survey outcomes matched, including the
initial selected item, hole and empty states. Later movement in some runs
caused an extra survey at the adjacent tile. The replay checks every
scratch snapshot and compares the focal survey independently. The default
terrain trace also reached `main+0x880870` under survey return site
`main+0x21f778c`. The alternate property had no hit in these runs.

The field item resolver and its auxiliary leaf have passed 960 and
72 independent synthetic cases. The runner executes the actual declared
AArch64 resolver-to-auxiliary BL target and still rejects undeclared
calls. Both runtime replacements are opt-in. The live group captured the neighborhood gather's item resolution and the diggable terrain predicate.

The item and terrain group used `tree-shake-{original,cpp}-shovelitems1`
and `tree-shake-default-shovelitems2`. Both comparisons captured 19
item-ID calls, including direct calls from the shovel survey and its
neighborhood gather, and 24 terrain calls under the survey's return site
`0x21f778c`. The 998 item-ID cases and 144 terrain cases matched two
independent reference runs. The four focal survey outcomes matched.
The original replay executed 135 declared auxiliary calls in the
synthetic item cases. The auxiliary and alternate terrain entries were
missed in gameplay, so both retain their original defaults and are
queued for smaller targeted retries. Use `NX_ACNH_FIELD_ITEM_ID=original`
for the original item resolver.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-shovelitems1/nx.log"
$c = "$r/tree-shake-default-shovelitems2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-item $o $c --item-entry 267d6c4 --live-terrain $o $c --terrain-entry 880870 --terrain-parent 21f778c --live-survey $o $c --survey-tile 40 69
```

The new layer-bounds candidate `main+0x194f798` is called by the generated
field-cell lookup at `0x195036c`. Its 1,600 independent cases cover
layer and channel fallback, unavailable layers, and inclusive signed
bounds. Its opt-in trial is `tree-shake-{original,cpp}-shovelbounds1`.

The layer-bounds check is verified through its original, opt-in and default
routes. The captures are `tree-shake-{original,cpp}-shovelbounds1` and
`tree-shake-default-shovelbounds2`. Each route captured 32 calls from the
generated field-cell lookup under the gather. The default matched 1,665
independent replay cases and all four focal survey outcomes. This includes
the sparse case in which the original returns for out-of-range X before
reading Z. Select its original route with
`NX_ACNH_SHOVEL_FIELD_BOUNDS=original`.

The item-ID replay now also checks the ordered selector argument of every
declared auxiliary call against the typed callback. Runtime coverage of
the original auxiliary fallback still needs a matching gameplay state.

The `0x195033c` field-cell candidate has passed 1,280 independent cases.
The pinned AArch64 body calls the real bounds predicate and deterministic
stand-ins for the generated item initializer and provider. The replay
compares dependency calls and ordered result writes, including 80
provider calls with and without a resolved-position output. Its
`NX_ACNH_SHOVEL_FIELD_CELL=cpp` route is scoped to the gather's four
call sites. The paired `shovelcell1` trial uses the real generated
initializer and provider in the disposable shovel scenario.

```powershell
& private/venv/Scripts/python -m private.heading.compare_leaf field_bounds
& private/venv/Scripts/python -m private.heading.compare_leaf field_cell
```

### Cell lookup and item block

The field-cell lookup at `0x195033c` is verified on the shovel gather's four
call sites. The paired `tree-shake-{original,cpp}-shovelcell1` runs
and `tree-shake-default-shovelcell2` each captured 32 calls. Location,
optional resolved-position output and item contents matched on all routes,
and the focal shovel survey matched all four outcomes. The generated item
initializer and virtual provider run through the typed runtime boundary.
The original selector is `NX_ACNH_SHOVEL_FIELD_CELL=original`.

The item block initializer at `0x127c524` is verified on the shovel survey
call site. Its original body executes 35 initializer calls and one final
tail call. The 32 independent cases compare each dependency call and its
destination and every ordered store. Use `NX_ACNH_SHOVEL_SURVEY_ITEMS=original`
to restore the generated path. The baseline in
`tree-shake-default-shovelcell2` and opt-in and default runs
`tree-shake-cpp-shovelprep1` and `tree-shake-default-shovelprep2` each
captured five identical initialized item blocks. The four focal survey
snapshots agreed on the complete 36-record neighborhood and occupancy mask.

The generated neighborhood gather at `0x127cab0` remains the next
dependency. The disposable shovel run in `tree-shake-default-shovelprep2`
captures its complete scratch input and output around the call from
`0x21f7614`. The new independent AArch64 reference runs 24 controlled
cases covering empty, mismatched-anchor, actor-table, metadata and
extent branches across negative and positive coordinates. The reference
uses explicit dependency responses and checks the full 36 records and
occupancy mask. The stage selection and live generated callbacks still
need equivalence testing before this gather can become a default.

```powershell
& private/venv/Scripts/python -m tests.acnh_shovel_gather_reference H:/ACNH/ACNHRecomp/private/game/exefs/main
```

### Neighborhood gather

The gather at `0x127cab0` is verified and defaults to handwritten C++ at
the shovel survey's call site. It owns stage selection, coordinate
conversion, the neighborhood traversal, and occupancy updates. The
runtime boundary supplies the generated stage lookup, cell provider,
metadata and extent calls. Set `NX_ACNH_SHOVEL_GATHER=original` to
select the original.

The independent harness checks 30 cases, including two explicit
stage-selection variants. Each case compares ordered dependency calls
and writes against two runs of the pinned AArch64 body. The muted
original trace in `tree-shake-default-shovelprep2` and opt-in and default
traces in `tree-shake-cpp-shovelgather1` and
`tree-shake-default-shovelgather2` captured five matching full gathers.
The four focal survey outcomes also matched before and after selection.
All 13 CTest tests and 13 replay and profile tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-default-shovelprep2/nx.log"
$c = "$r/tree-shake-default-shovelgather2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_gather --live $o $c
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-neighborhood $o $c --live-survey $o $c --survey-tile 40 69
```

The next discovery run, `tree-shake-default-shovelprovider1`, records the
actual generated targets from the `0x11e4f58` provider under the shovel
gather. This is the main remaining generated dependency inside the
neighborhood query. Other pending paths include the alternate terrain
predicate, the auxiliary item selector and the survey's outer
classification routine.

### Field provider methods

The muted target probe in `tree-shake-default-shovelprovider1` observed
318 virtual dispatch edges from 24 field-provider calls under the shovel
gather. Five observed methods now default to handwritten C++ on this
path. Set `NX_ACNH_PROVIDER_METHODS=original` to select the generated
methods. The table accessor preserves its runtime-loaded tail target.

| Entry | Live role | Independent replay cases | Captures per route |
| --- | --- | ---: | ---: |
| `0x11ac1e8` | Resolve the provider table | 27 | 12 |
| `0x11ac290` | Report reader readiness | 27 | 12 |
| `0x261157c` | Report table width | 27 | 12 |
| `0x2611584` | Report table height | 27 | 12 |
| `0x261158c` | Calculate the field record displacement | 74 | 12 |

The original and opt-in runs are `tree-shake-original-providermethods1`
and `tree-shake-cpp-providermethods1`. The default run is
`tree-shake-default-providermethods2`. Each replay compares the native
result with two runs of the pinned AArch64 function. The table accessor
cases also check the dynamic target and argument forwarding. The
normalized comparisons matched the full neighborhood and four shovel
outcomes at tile (40, 69). All 13 CTest tests and all 14 replay and
profile tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providermethods1/nx.log"
$c = "$r/tree-shake-default-providermethods2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_methods --live $o $c
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-neighborhood $o $c --live-survey $o $c --survey-tile 40 69
```

### Field provider orchestration

The provider at `0x11e4f58` defaults to C++ on the four neighborhood
field-cell call paths. `NX_ACNH_PROVIDER_CELL=original` selects the
generated body. The typed provider owns bounds, masking, item selection
and resolved coordinates. The guest boundary still calls the virtual
context callbacks and the generated continuation resolver and cleanup.

The independent replay compared 60 cases against two stock executions.
The replay checked ordered dependency calls and guest writes, including
temporary changes to the provider counter. The three muted live trials
`tree-shake-original-providercell3`, `tree-shake-cpp-providercell3`
and `tree-shake-default-providercell4` matched 24 calls from all four
gather sites, including a continuation that resolves (78, 138) to
(78, 137). They also matched the four focal outcomes and full
neighborhood snapshots at (40, 69). The first launcher attempt omitted
the shovel scenario and the second used an older gameplay DLL. Those
attempts yielded no provider evidence. The original and opt-in counter
values differed at entry (0x12 and 0x9). The trace comparison checks
preservation of each incoming value. All 13 Release CTest tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providercell3/nx.log"
$c = "$r/tree-shake-default-providercell4/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_cell --live $o $c
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-neighborhood $o $c --live-survey $o $c --survey-tile 40 69
```

### Shovel terrain dispatcher and shared record reader

`0x877130` and `0x862530` are verified defaults on the shovel terrain
path. The first converts the tile into half-tile bounds, checks flags,
and dispatches the material and layer checks. The second composes all
four ground and overlay samples. The runtime preserves the original
selectors `NX_ACNH_SHOVEL_TERRAIN_DISPATCH=original` and
`NX_ACNH_SHOVEL_TERRAIN_SAMPLE=original`.

The dispatcher replay matched 40 cases, including ordered calls,
coordinate arguments and results. The sample replay matched 328 cases,
including the order of record writes. The muted original and opt-in
`terraindispatch1` trials and the default `terraindispatch2` trial each
captured two terrain decisions and 16 samples (eight from each helper).
The material records, full neighborhoods and four outcomes at
(40, 69) matched. Both live terrain decisions were accepted. The
independent dispatcher replay covers rejection cases. The next
group replaces the two remaining terrain helpers `0x871f80` and
`0x848e68`.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-terraindispatch1/nx.log"
$c = "$r/tree-shake-default-terraindispatch2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain --live $o $c
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_sample --live $o $c
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-neighborhood $o $c --live-survey $o $c --survey-tile 40 69
```

### Shovel rectangle height and quadrant materials

`0x871f80` and `0x848e68` default to C++ under the shovel terrain
dispatcher. The former checks cell flags and the height range. The
latter selects all four quadrant materials, using the previously verified
sampler and generated world-position fallbacks `0x87a980` and
`0x87aa70`. Set `NX_ACNH_SHOVEL_TERRAIN_LEVEL=original` or
`NX_ACNH_SHOVEL_TERRAIN_MATERIALS=original` to select the original.

Each helper matched two original AArch64 executions on 224 independent
cases, including overlay and fallback call order. The muted
`terrainhelpers1` original and opt-in trials and the
`terrainhelpers2` default trial captured two rectangle checks and
eight quadrant checks each. Four focal shovel outcomes and their complete
neighborhood records at (40, 69) matched. The Release suite passed all 13
CTest tests.

The whole-tile fixture `shovelclassify1` missed item classification.
The smaller half-tile retry `shovelclassifyhalf1` captured 24 calls into
`0x127d05c`, 24 calls to its first gate `0x267da10` at `0x127d074`,
and 12 to `0x267e024` at `0x127d080`. Neither attempt observed the
alternate terrain route. The whole-tile probe separately observed
the stage predicate `0x16baf44` and one metadata predicate
`0x19ee3b4` under the stock neighborhood gather.

### Shovel item classification

The typed classifier `0x127d05c` and its two entry checks `0x267da10`
and `0x267e024` default to C++ through the shovel survey. The shared
selector `NX_ACNH_SHOVEL_ITEM_CLASS=original` selects their generated
routes. Descriptor construction and deeper category and metadata
callbacks remain at the guest boundary.

Their independent AArch64 replays match 420, 40 and 30 cases,
respectively. The muted half-tile trials `itemquery1` (original and
opt-in) and `itemquery2` (default) each recorded 24 classifier calls, 24
first-gate calls and 12 alternate-gate calls. All 60 input records
and results matched across routes, as did six focal neighborhoods
and survey outcomes. All observed classifier outcomes were false.
Synthetic replay covers accepted examples and deeper metadata and
category routes. All 13 Release CTest cases passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-itemquery1/nx.log"
$c = "$r/tree-shake-default-itemquery2/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_item_queries --live $o $c
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-neighborhood $o $c --live-survey $o $c --survey-tile 40 69
```

### Neighborhood stage filter

The stage filter `0x16baf44` and predicates `0x16f37d0` and
`0x16f3a18` default to C++ inside the shovel neighborhood gather.
They use sorted key sets and retain the original evaluation order.
`NX_ACNH_SHOVEL_STAGE=original` selects the generated bodies.
The virtual stage source still supplies the stage name at the boundary.

The individual AArch64 replays matched 1509, 198 and 109 cases
respectively, including the live stage key, both helper paths,
accepted keys and adjacent rejected keys. The muted `shovelstage1`
original and opt-in trials and `shovelstage2` default trial each
captured five calls per entry. All observed decisions were false.
Four focal surveys and all neighborhood records matched, along with
the final action flags, action code and return register at
`0x21f78dc`. All 13 Release CTest cases passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-shovelstage1"
$c = "$r/tree-shake-default-shovelstage2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_stage --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

### Composed shovel field-query checkpoint

The `shovelpathwhole1` and `shovelpathhalf1` series each used one muted
original, one explicit C++, and one default gameplay run. The original
runs select the generated routes for the promoted shovel field-query
functions. The setup, saves, and existing fractional placement hook
are shared by all runs.

All five whole-tile and six half-tile decisions matched across their
three routes, including every 36-record neighborhood, occupancy mask,
selected record, final action code, flags and boolean result. The
whole-tile runs included returned action codes 0, 1 and 8 with true
results. The half-tile runs returned action code 9 with a false result.
The comparison includes every captured tile.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
foreach ($fixture in 'whole', 'half') {
    $o = "$r/tree-shake-original-shovelpath${fixture}1"
    foreach ($route in 'cpp', 'default') {
        $c = "$r/tree-shake-$route-shovelpath${fixture}1"
        & private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log"
    }
}
```

### Neighborhood continuation, scratch items and metadata

The five entries below now default to handwritten C++ on the shovel
neighborhood path. Each counts only for the observed scope.

| Entry | Responsibility | Independent replay cases | Captures per route |
| --- | --- | ---: | ---: |
| `0x11e4578` | Resolve a continuation anchor and acquire its context | 24 | 16 |
| `0x2540184` | Clean up the continuation context | 24 | 16 |
| `0x267d4ac` | Initialize an empty survey item | 48 | 80 |
| `0x267f248` | Finalize a sentinel item | 128 | 80 |
| `0x19ee3b4` | Classify item metadata for the gather | 720 | 1 |

The original and opt-in runs were `neighborhoodsupp1`. The default was
`neighborhoodsupp2`. All captured function data matched. The original
and C++ runs for this group each matched the four focal decision codes,
results, records and occupancy masks at (40, 69). The opt-in fixture
also took one extra step to (40, 70), so the initial unfiltered
comparison saw one extra decision. The earlier composed checkpoint
covers (40, 70) across all three routes.

The 16 fixture cases with a catalog entry for the empty sentinel stayed
on the generated initializer route. The metadata trial used a populated
row and returned zero. Replay also checked nonzero classifications and
generated metadata callback order. The original routes remain available
via `NX_ACNH_PROVIDER_ANCHOR=original`,
`NX_ACNH_SHOVEL_EMPTY_ITEM_INIT=original` and
`NX_ACNH_SHOVEL_METADATA=original`. All 13 Release CTest checks passed.

The next set is the frequently invoked provider table resolver
`0x1267360` and both context dispatch thunks `0x260b860` and
`0x260b934`. All 12 inspected table resolutions used this same
destination. Their real virtual callbacks remain selectable through the
guest boundary.

### Provider binding and context dispatch

`0x1267360`, `0x260b860` and `0x260b934` default to C++ for the
observed shovel provider. The table resolver owns the binding selection
and final pointer adjustment. Context receiver adjustment and member
dispatch remain in the runtime adapter. The original group is
selectable through `NX_ACNH_PROVIDER_BINDING=original`.

Independent replays matched 15, 60 and 60 cases respectively. They
exercised null bindings, both resolver branches, missing context,
direct and virtual member dispatch, and signed receiver adjustment.
The original, opt-in and default gameplay captures had 12 calls per
entry and matched their selected methods, table type and read results.
The four focal final decisions and neighborhood records matched.
All 13 Release CTest checks passed.

All observed provider binding calls used the storage accessor
`0x260bd50` at `0x1267390`. The context thunks select the stateful
runtime callbacks `0x25ee660` and `0x25ee758`, which remain generated.
The next group removes the storage accessor on the observed shovel
route while preserving the context callbacks.

The smaller `shovelmissretry1` trial used the disposable `exactfixed`
fixture. It captured 48 diggable property calls and 19 item-ID
calls. It did not reach the deferred alternate terrain
`0x880a10` or auxiliary item index `0x1a03610`.

### Provider storage access

`0x260bd50` now defaults to C++ under the observed table resolver.
The original route is selectable with `NX_ACNH_PROVIDER_STORAGE=original`.
Both AArch64 replays matched all 12 synthetic cases and the 48 live
pointer snapshots in each original comparison. The original, opt-in and
default runs captured 24 calls per route. The complete focal surveys,
neighborhoods and final decisions matched.

The same runs observed stage-name accessor `0x5cb0a0` at the gather's
virtual call `0x127cb2c`; every recorded call used stage index 1.
That fixed-stride lookup is the next eligible stage dependency.
