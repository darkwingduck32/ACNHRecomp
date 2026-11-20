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

## Completed batch 3

Batch 3 verified 100 functions on main build
`FF1D1C05670DB6021C85B624A710B963`. The rolling discovery queue
continues in [batch 4](#active-batch-4).

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
| `0x29cbe80` | `0x124` | Read cached row value, slot 12 | `0x29cd3d0` | Verified |
| `0x29cbfb0` | `0x124` | Read cached row value, slot 13 | `0x29cd3e0` | Verified |
| `0x29cc0e0` | `0x124` | Read cached row value from slot 14 | `0x29cd3f0` | Verified |
| `0x29cc210` | `0x124` | Read cached row value from slot 15 | `0x29cd400` | Verified |
| `0x29cc340` | `0x124` | Read cached row value from slot 16 | `0x29cd410` | Verified |
| `0x29cc470` | `0x124` | Read cached row value from slot 1 | `0x29cd420` | Verified |
| `0x29cc5a0` | `0x124` | Read cached row value from slot 2 | `0x29cd430` | Verified |
| `0x29cc6d0` | `0x124` | Read cached row value from slot 3 | `0x29cd440` | Verified |
| `0x29cc800` | `0x124` | Read cached row value from slot 4 | `0x29cd450` | Verified |
| `0x29cc930` | `0x124` | Read cached row value from slot 5 | `0x29cd460` | Verified |
| `0x29cca60` | `0x124` | Read cached row value from slot 6 | `0x29cd470` | Verified |
| `0x29ccb90` | `0x124` | Read cached row value from slot 7 | `0x29cd490` | Verified |

The [archived discovery queue](batch-3-candidates.md) records 100 candidates beyond
the implementation entries listed here. The queue marks entries with
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

### Stage-name lookup

The gather's observed stage-name accessor `0x5cb0a0` now defaults to
typed C++. `NX_ACNH_SHOVEL_STAGE_NAME=original` selects the stock body.
The replay compared 24 synthetic cases and 10 live snapshots per
comparison against two stock AArch64 runs. It covered signed W1 index
bounds and multiple module bases. The original, opt-in and default
runs `stagename1` and `stagename2` each captured five lookups.
All returned the same signed-index displacement.

The latest whole-tile default also matched the fully original baseline
`shovelpathwhole1` at (40, 69). The four neighborhoods and final
decisions matched there. The action engine still enters from
`0x21f6fe0`. It calls typed stage and terrain queries, the neighborhood
gather, cell and continuation lookups, and item classification through
the runtime boundary.

The `whole` and `half` disposable input fixtures each have 11 files.
The fixture digests hash the sorted relative filenames, a tab and
each file's SHA-256, with one line per file.

| Fixture | SHA-256 |
| --- | --- |
| `whole` | `3f35453418024aa09f214b67321504ee0ffd5b3a90ab29b7350eaf50c4804b21` |
| `half` | `dabb6fb127d8128947cea24c419da1065263397e5a8901d882a2789dc90956c5` |

The final half-tile default `finalshovelhalf` matched all six
neighborhoods and final decisions from `shovelpathhalf1` with the
original route selected. All 13 Release CTest checks and 14 replay
and profile tests passed. The discovery queue stays at 100 entries.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-stagename1"
$c = "$r/tree-shake-default-stagename2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_stage_lookup --live "$o/nx.log" "$c/nx.log"
$o = "$r/tree-shake-original-shovelpathwhole1"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
$o = "$r/tree-shake-original-shovelpathhalf1"
$c = "$r/tree-shake-default-finalshovelhalf"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log"
```

Next implementation group is the observed provider-context pair
`0x25ee660` and `0x25ee758`. Capture their active synchronization branches
before selecting any lower-level calls to replace.


### Provider context acquisition and release

| Entry | Scope | Independent AArch64 cases | Captures per route |
| --- | --- | ---: | ---: |
| `0x25ee660` | Acquire context for an ordinary read or continuation | 864 | 24 |
| `0x25ee758` | Release context after an ordinary read or continuation | 864 | 24 |

Both default to typed C++ on the shovel path.
`NX_ACNH_PROVIDER_CONTEXT=original` selects the generated bodies.
The independent replay checks acquire results, ordered callback targets and
receiver arguments, and counter writes against two AArch64 executions
per case. It covers missing handles, a disabled context, guard choices,
and both handle modes. Lower-level synchronization still runs through
the guest boundary.

The disposable `whole` fixture reached mode 1 with both handles present
and the guard disabled. Each of the original, opt-in and default runs
recorded 19 ordinary and five continuation calls per entry. The four
focal shovel outcomes at (40, 69), neighborhood records, masks, and
final decisions matched. The fixture digest is listed above. The build
passed 13 Release CTest checks and 14 replay and profile tests.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providercontext1"
$c = "$r/tree-shake-default-providercontext2"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_context --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The first remaining call in the observed acquire path is the generated
context-flag lookup `0x2601110`. Capture its thread-selection branch
before continuing to the paired-handle helper `0x262e9bc` and release
helper `0x37568c0`.


### Provider context-flag lookup

The lookup at `0x2601110` defaults to C++ inside native shovel context
acquisition. The generated route is selectable with
`NX_ACNH_PROVIDER_CONTEXT_FLAG=original`. The import at `0x3f033a0`
and virtual name refreshes remain at the guest boundary.

The independent replay matched two AArch64 executions in each of 64
cases, including manager and thread selection, pointer equality, string
matching, and callback effects that clear the manager slot or switch
the vtable. The `providerflag1` original and opt-in runs and
`providerflag2` default run each captured 24 lookups (19 ordinary
reads and five continuation reads), all returning flag offset `0x450`
with value zero. Each run matched all four focal shovel surveys,
neighborhoods, and final decisions at (40, 69). All 13 Release CTest
checks passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providerflag1"
$c = "$r/tree-shake-default-providerflag2"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_context_flag --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

Next, exercise the paired-handle helper `0x262e9bc`. The live mode-1
acquire route with a clear flag and guard reaches it after thread
resolution. The intermediate target `0x3f033a0` is a guest import thunk.


### Paired provider-handle acquisition

The helper at `0x262e9bc` defaults to C++ under native shovel context
acquisition. `NX_ACNH_PROVIDER_CONTEXT_PAIR=original` selects the
generated body. It updates the second handle first, reloads the first
handle after a counter callback, and then updates the first handle.
The nested acquisition route shares the same counter update logic.

The replay matched both AArch64 executions in each of 288 cases,
including absent handles, a callback that replaces the first handle,
and signed counter bounds. Original and opt-in `providerpair1` and
default `providerpair2` each captured 24 acquisitions, with both
handles of kind 2. All four focal neighborhoods and shovel decisions
matched. All 13 Release CTest checks and 14 replay and profile tests
passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providerpair1"
$c = "$r/tree-shake-default-providerpair2"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_context_pair --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

Both live mode-2 handles reach record selection at `0x37567d0` inside the
mode-1 pair. It precedes the release helper `0x37568c0` on this path.


### Provider-handle record lookup and release

| Entry | Scope | AArch64 replay cases | Gameplay captures per route |
| --- | --- | ---: | ---: |
| `0x37567d0` | Find or claim the current thread's handle record | 160 | 48 |
| `0x37568c0` | Decrement the record and release the handle | 360 | 48 |

Both default to C++ under the verified shovel provider. Set
`NX_ACNH_PROVIDER_HANDLE=original` to select the generated bodies.
The runtime keeps thread resolution, lock and wake calls at the guest
boundary. The release path uses the same exclusive-monitor helpers
and barrier as the generated runtime.

Each replay case matched two AArch64 runs. The lookup fixtures include
a manager change between thread resolutions and a table replacement
under the record guard. The release fixtures cover the shared and
exclusive branches, ordered writes, and counter overflow boundaries.
Gameplay record selection matched the selected slot and count. Release
captures establish caller and slot capacity, with release effects
compared in replay and at the four focal shovel decisions at (40, 69).
Original and opt-in `providerhandle1` and default `providerhandle2`
used the same muted fixture, one instance at a time. All 13 Release
CTest checks and 14 replay and profile tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providerhandle1"
$c = "$r/tree-shake-default-providerhandle2"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_context_handle --acquire --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_context_handle --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

### Provider counter initialization

The counter initializer at `0x37e2e80` defaults to C++ under the
shovel provider. `NX_ACNH_PROVIDER_HANDLE_PREPARE=original` selects the
generated body. Guest wait and wake imports at `0x3f05820` and
`0x3f05830` remain at the boundary. The handwritten body uses the
runtime's guest exclusive-monitor operations.

All 64 independent cases matched two AArch64 runs. Original and opt-in
`providerprepare1` and default `providerprepare2` each captured 48
initializations, 24 per pair caller, with the counter changing from
zero to one. All four focal surveys, neighborhoods, and final shovel
decisions matched at (40, 69). The runs used one muted game instance
at a time. All 13 Release CTest checks and 14 replay and profile tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-providerprepare1"
$c = "$r/tree-shake-default-providerprepare2"
& private/venv/Scripts/python -m private.heading.compare_leaf provider_context_handle --prepare --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The first generated dependency observed by the scoped trace was the
alternate descriptor kind query `0x19f93e8` at `0x19ee424`. The
descriptor predicate `0x19e59b0` followed at `0x19ee440`,
`0x19ee44c`, and `0x19ee46c`. These two candidates share the item
metadata path. The terrain fallbacks `0x87a980` and `0x87aa70` were
also observed later in the same run.

### Host and deeper item classification checkpoint

The `acnh_shovel_field_query` Release CTest drives the typed C++ path
with in-memory field state. It compares the stage, all 36 records,
the resolved continuation and selected item, metadata-driven
occupancy, item suitability, and both the material and layer checks
for shovel terrain. Generated dependencies receive fixture responses
at the typed interface. All 14 Release CTest checks passed.

The first gameplay attempt, `classdeep1`, put roses directly on the
target. All three routes stopped at early rejection code 9 at
(40, 68). No deeper classification was observed in those runs.

The retry, `classdeep2`, retained the fossil on the target and put
the roses in its neighborhood. It copied `whole` to
`H:/ACNH/NHSE/Save.Game/interaction-20261119-232838/shovelclassdeep2/device_save`
and changed four empty layer-0 records
at main plaintext offsets `0x24e460`, `0x24e468`, `0x24ea60`, and
`0x24ea68` to `0x0b77`, `0x1000b770000fffd`, `0x10b770000fffd`,
and `0x1010b770000fffd` respectively. The saved checksums were
recomputed and independently checked. The 11-file fixture digest is
`b5e186e0edb4642de3b148bfa0cc440d57b6c77a9d985fbd3fa481d20bdbeafc`.

The original, opt-in, and default routes matched all four neighborhoods,
occupancy masks, selected records, and final decisions at (40, 69).
The opt-in and default runs each recorded an additional survey and
decision at (40, 70). The three-route comparison used (40, 69).

The generated gather calls its own metadata body directly, so the
whole original route bypasses the runtime metadata trace. The focused
`classdeepmeta1` run selected the original metadata function with the
default gather for a direct comparison. Each route matched five
populated metadata results at (40, 69), four for the roses and one for
the fossil. All 720 independent metadata cases matched two AArch64
runs. The gameplay runs used one muted game instance at a time.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-classdeep2"
$m = "$r/tree-shake-original-classdeepmeta1"
foreach ($route in 'cpp', 'default') {
    $c = "$r/tree-shake-$route-classdeep2"
    & private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
    & private/venv/Scripts/python -m private.heading.compare_leaf shovel_item_metadata --live "$m/nx.log" "$c/nx.log" --survey-tile 40 69 --require-row-item 0x0b77
}
```

The next group is the alternate descriptor kind query `0x19f93e8`
and descriptor predicate `0x19e59b0`. The probe encountered the kind
query first, then the predicate. Each focal C++ route reached the kind
query five times and the predicate eleven times, from the same shovel
metadata call sites. These remain generated. The kind query can call
`0x19f4c7c` on its special-kind branch. The guest synchronization
imports, deeper item category checks, and quadrant terrain fallbacks
remain at the boundary. Discovery still contains 100 candidates.

### Descriptor kind and predicate

Both queries default to handwritten C++ under the shovel neighborhood
metadata classifier. Their generated bodies remain selectable
individually.

| Entry | Original route switch | Independent AArch64 cases | Calls per route at (40, 69) |
| --- | --- | ---: | ---: |
| `0x19f93e8` | `NX_ACNH_SHOVEL_ALTERNATE_KIND=original` | 270 | 5 |
| `0x19e59b0` | `NX_ACNH_SHOVEL_METADATA_PREDICATE=original` | 1920 | 11 |

The alternate kind replay compared the special-kind callback arguments
and result handling. The predicate replay checked both the return value
and ordered cache writes across packed and indexed tables and cold and
warm caches. The composed host test exercises both handwritten queries
and verifies the resolved cache entry. All 14 Release CTest and 14 replay
and profile tests passed. The existing 14-slot and 25-slot cache replays
also passed.

Original and opt-in `metadataleaves1` and default `metadataleaves2`
matched each query's item record, row presence, caller and result at
(40, 69). The fossil
returned kind `0x2b` and predicate false. The roses returned kind
`0xb9` and predicate true. All four focal surveys, neighborhoods and
final decisions matched, including the default against the original
whole-path `classdeep2` capture. Gameplay ran one muted instance at a
time.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-metadataleaves1"
$c = "$r/tree-shake-default-metadataleaves2"
foreach ($variant in '--alternate', '--predicate') {
    & private/venv/Scripts/python -m private.heading.compare_leaf shovel_item_metadata $variant --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69 --require-row-item 0x0b77
}
$o = "$r/tree-shake-original-classdeep2"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The generated special-kind callback `0x19f4c7c` stays at the boundary.
The roses and fossil did not enter that branch. The next remaining
generated calls in trace order are `0x87a980` and `0x87aa70`, from the
quadrant material helper. Both consult the same stage and terrain state.
The second also reaches queued field-record predicate `0x1285040` at
`0x87aae4`. Keep it with the terrain fallback group and use gameplay
to determine whether their deeper material lookups are reached.

### Quadrant terrain fallbacks

Both world-position fallbacks and the secondary fallback's field
material predicate default to handwritten C++ on the scoped shovel
survey path. The virtual stage lookup and material overrides remain
at the guest boundary. The predicate retains its generated route
for calls outside the shovel scope.

| Entry | Original route switch | Independent AArch64 cases | Calls per route at (40, 69) |
| --- | --- | ---: | ---: |
| `0x87a980` | `NX_ACNH_SHOVEL_TERRAIN_PRIMARY=original` | 80 | 16 |
| `0x87aa70` | `NX_ACNH_SHOVEL_TERRAIN_SECONDARY=original` | 480 | 16 |
| `0x1285040` | `NX_ACNH_SHOVEL_TERRAIN_FIELD_MATERIAL=original` | 42 | 16 |

The replays compared return values, callback order and arguments, and
caller-visible writes. They exercised absent and refreshed stage
contexts, material overrides, world-to-half-tile conversion, and
primary, secondary and fallback field records. The secondary fallback
replay ran its original field predicate as part of the original route.

The initial trace included predicate calls before gameplay. The trial
was stopped, and the predicate adapter now verifies the quadrant
helper and shovel survey frames. The `terrainfallbacks2` original and
opt-in captures and the `terrainfallbacks3` default capture matched
each function at (40, 69). In those calls both fallbacks returned 7,
both retained their input positions, and the predicate returned false.
The game did not enter the deeper stage material overrides in this
fixture. All four focal surveys, neighborhoods and final outcomes
matched, including the default against the whole original
`classdeep2` route. All 14 Release CTest and 14 replay and profile tests
passed. Gameplay used one muted instance at a time.

The test overlay routes the generated BL at `0x87aae4` through the
runtime dispatcher so the original run captures the child predicate
on its actual call path. The overlay is
`generated-dll/shovel_terrain_fallbacks_generated.dll` under the
private campaign directory. Discovery replenished the verified
`0x1285040` row with `0x2a673a0` and still holds 100 candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-terrainfallbacks2"
$c = "$r/tree-shake-default-terrainfallbacks3"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_fallbacks --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
$o = "$r/tree-shake-original-classdeep2"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The next direct dependencies came from the generated survey
`0x21f6fe0`. The muted `tree-shake-default-surveycalls1/console.log`
probe observed target selection `0x15f36e0` at `0x21f70d4` first,
followed by pre-survey terrain setup `0x87b460` at `0x21f74fc`.
Each ran once per survey, five times in total. The target selection
body still calls `0x15e026c` and `0x15f3ed0`. Capture those call sites
to identify which parts of target selection to port together. The
same probe also observed `0x267e024` from the outer survey and later
generated calls to `0x19fb044` and `0x2878a24`.

### Shovel target selection and terrain setup

| Entry | Original route switch | Independent AArch64 cases | Calls per route at (40, 69) |
| --- | --- | ---: | ---: |
| `0x15f36e0` | `NX_ACNH_SHOVEL_TARGET=original` | 420 | 4 |
| `0x87b460` | `NX_ACNH_SHOVEL_TERRAIN_SETUP=original` | 336 | 4 |
| `0x1284ee0` | `NX_ACNH_SHOVEL_LAYER_ONE=original` | 42 | 4 |

The selector now runs its coordinate adjustment and search in
handwritten C++ on the shovel survey. The replay compared the
request's final bytes and each probe's input coordinates and call
order. Terrain setup retains its virtual stage lookup and selects a
material check. Its replay ran the original layer-zero and layer-one
predicates for their respective branches. Layer one also passed its
own independent replay.

The original and opt-in `targetport1` and default `targetport2` runs
matched the selector's captured coordinates and result, plus the
terrain setup and layer-one predicate results. The fixture reached
layer one at each focal call. The default matched all four focal
neighborhoods, surveys, and final results from `original-classdeep2`.
The three routes ran as sequential muted instances. All 14 Release
CTest checks and 14 replay and profile tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-targetport1"
$c = "$r/tree-shake-default-targetport2"
foreach ($group in 'shovel_target', 'shovel_terrain_setup') {
    & private/venv/Scripts/python -m private.heading.compare_leaf $group --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
}
$o = "$r/tree-shake-original-classdeep2"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

Height sampling `0x15e026c` and point classification `0x15f3ed0`
remain generated. The default run's `SHOVEL_TARGET_CHILD` trace
encountered the height calls first. It reached the samplers
`0x83b024` and `0x83a950`, the parameter accessor `0x241e98c`, and
the virtual float accessor `0x60cba4`, in that order. This is the
next group for moving height sampling off the generated path.

### Shovel height sampling

The height dispatcher, ground sampler, overlay sampler, and two
parameter accessors default to C++ under the shovel target selector.
Other callers keep their generated routes. The overlay retains the
original lock and unlock imports. The parameter accessor retains
its guest virtual type check and static initialization routines.

| Entry | Original route switch | Independent AArch64 cases | Calls per route at (40, 69) |
| --- | --- | ---: | ---: |
| `0x15e026c` | `NX_ACNH_SHOVEL_HEIGHT=original` | 576 | 10 |
| `0x83b024` | `NX_ACNH_SHOVEL_GROUND_HEIGHT=original` | 128 | 10 |
| `0x83a950` | `NX_ACNH_SHOVEL_OVERLAY_HEIGHT=original` | 128 | 10 |
| `0x241e98c` | `NX_ACNH_SHOVEL_HEIGHT_PARAMETERS=original` | 30 | 20 |
| `0x60cba4` | `NX_ACNH_SHOVEL_HEIGHT_FLOAT=original` | 3 | 30 |

The first trial captured heights from other target-selector callers.
The adapters now verify the shovel survey's saved return address.
The first scoped opt-in run faulted in column lookup because the
port treated the value in X3 as the cache address. The
original helper uses the fixed cache at `main+0x4bfab80`. The corrected
replay executes the actual AArch64 row and column routines and checks
the resulting cache bytes with that register set to zero.

The original `heightport2`, opt-in `heightport3`, and default
`heightport4` runs matched each function's focal trace. The four
neighborhoods, surveys, and final decisions also matched, including the
default against the whole original `classdeep2` route. The runs used
one muted instance at a time. All 14 Release CTest checks and 14
replay and profile tests passed.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-heightport2"
$c = "$r/tree-shake-default-heightport4"
foreach ($group in 'shovel_height', 'shovel_height_dependencies') {
    & private/venv/Scripts/python -m private.heading.compare_leaf $group --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
}
$o = "$r/tree-shake-original-classdeep2"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The first generated dependency in the tightened default trace is
the grid-height routine `0x862700`. Both samplers call it, at
`0x83b0c8` and through virtual dispatch at `0x83aa74`. Start the
next group there. The height samplers also call generated copies of
the world-position fallbacks `0x87a980` and `0x87aa70`, whose C++
routes are already verified under the quadrant helper. The original
parameter type test `0x22688e4` and point classifier `0x15f3ed0`
remain at the guest boundary.

The private `shovel-grid-height-reference.json` contains 336 stable
AArch64 fixtures for `0x862700`, including both sampler modes,
ground and overlay materials, tile edges, and out-of-bounds points.
The source fixture is `private.heading.shovel_grid_height_reference`.

### Shovel grid height

The typed grid-height query at `0x862700` defaults to handwritten C++
under both shovel height samplers. It selects the tile sector and
terrain layer, computes the height, and writes the material and
overlay flag. The ground sampler calls it at `0x83b0c8`. The overlay
sampler reaches it through virtual dispatch at `0x83aa74`.
Set `NX_ACNH_SHOVEL_GRID_HEIGHT=original` for the generated route.

The independent replay matched 672 cases against two original
AArch64 executions, including exact float bits, retained output on
out-of-bounds queries, and the order and widths of the output writes.
The first live attempt, `gridheight1`, reached both call sites but
failed the exact input comparison. Movement changed two probe
positions per caller between runs. The retry runs the original on the
same input when tracing a C++ call. It restores both output values
before executing C++ and compares the height bits, material, and flag.
With tracing disabled the C++ route executes the typed query directly.

The original and opt-in `gridheight2` runs and the default
`gridheight3` run matched 10 focal calls at each caller. Both C++
routes matched all same-input checks. All four neighborhoods,
surveys, and final decisions matched, including the default against the
whole original `classdeep2` route. The runs used one muted instance at
a time. All 14 Release CTest checks and 14 replay and profile tests
passed. The discovery queue holds 100 entries.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-gridheight2"
$c = "$r/tree-shake-default-gridheight3"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_grid_height --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
$o = "$r/tree-shake-original-classdeep2"
& private/venv/Scripts/python -m private.heading.compare_leaf field_primitives --live-final "$o/console.log" "$c/console.log" --live-neighborhood "$o/nx.log" "$c/nx.log" --live-survey "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The original direct ground call was routed through the dispatcher in
the private `generated-dll/shovel_grid_height_port_generated.dll`.
The height samplers still call generated material fallbacks
`0x87a980` and `0x87aa70`. Verify the existing C++ implementations
in this new scope, including the secondary fallback's material test
`0x1285040`, then handle the generated parameter type check
`0x22688e4`. The target selector still calls the generated point
classifier `0x15f3ed0`.

### Terrain fallbacks under the height samplers

The ground and overlay height samplers now use the same typed C++
material fallbacks as the quadrant helper. Each original function
matched its independent replay and then exercised the new scope in
the original and opt-in samplerterrain1 runs and the default
samplerterrain2 run.

| Entry | AArch64 cases | Focal calls per route | Call sites |
| --- | ---: | ---: | --- |
| `0x87a980` | 80 | 20 | `0x83b0e0`, `0x83ab0c` |
| `0x87aa70` | 480 | 10 | `0x83ab28` |
| `0x1285040` | 42 | 10 | `0x87aae4` under the overlay sampler |

Both fallbacks returned material 7 and retained their input positions.
The field predicate returned false on the captured half tiles. The
default matched the whole original classdeep2 route on all four
neighborhoods, surveys and final decisions. The runs used one muted
instance at a time. All 14 Release CTest checks and 14 replay and
profile tests passed. The discovery queue remains at 100. These three
entries were already verified in the quadrant scope and are counted once.

Set `NX_ACNH_SHOVEL_SAMPLER_TERRAIN=original` to select the original
sampler fallbacks. The individual fallback switches also apply on
the sampler route. Virtual stage lookup and stage-specific material
overrides remain at the guest boundary. The next verified caller
reaches the generated parameter type check `0x22688e4` 26 times in
the default run. The generated point classifier `0x15f3ed0` is next
in the target selector.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-samplerterrain1"
$c = "$r/tree-shake-default-samplerterrain2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_fallbacks --sampler-live --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

### Height parameter type check

The parameter object's virtual type check `0x22688e4` now runs in
handwritten C++ under the shovel height dispatcher. The typed query
checks two guarded type descriptors and calls the generated base
type matcher `0x6521f0` on a miss. The static initialization guard
imports stay at the guest boundary. It stays selectable through
`NX_ACNH_SHOVEL_HEIGHT_PARAMETER_TYPE=original`.

The 180-case independent replay matched the original AArch64 result,
ordered external calls, and writes for initialized and uninitialized
descriptors, guard acquisition outcomes, and the base-type branch.
The no-op candidate failed the first requested-type case. The
original and opt-in heighttype1 runs and the default heighttype2 run
each matched 20 focal calls from the accessor at `0x241e9e4`. Both
dispatcher call sites requested the first initialized type and returned
true. Gameplay used one muted instance at a time.

All four neighborhoods, surveys, and final decisions matched the
whole-original classdeep2 route. All 14 Release CTest checks and 14
replay and profile tests passed. The discovery queue remains at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-heighttype1"
$c = "$r/tree-shake-default-heighttype2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_height_parameter_type --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The default run did not take the generated base-type branch. The
next generated entry reached by the target selector is the point
classifier `0x15f3ed0`. Start with entry and exit request snapshots
and the ordered child results from the observed shovel gameplay.

### Shovel point classifier

Point classification `0x15f3ed0` now runs in typed C++ for the
shovel target selector. It chooses a height and stage, classifies
the selected record against the appropriate item catalog, and
updates the request. The ABI adapter retains the generated child
calls and their guest scratch layouts. Set
`NX_ACNH_SHOVEL_POINT_CLASSIFIER=original` for the generated route.

The fixture captured 104 original invocations on build
`FF1D1C05670DB6021C85B624A710B963`. Each was run twice using
the original AArch64 body, captured memory and deterministic
callees. The classifier matched return values, final request
bytes, ordered external writes, and child calls. The no-op
candidate failed capture 1 on its child calls and output bytes.
Twenty-nine portable synthetic cases also passed. They cover
raised terrain, category gates, and paths that leave the previous
request result intact. The actual gameplay fixture produced
results 0 and 10.

The original and opt-in pointclass1 runs and the default pointclass2
run matched 40 focal calls each, at 12 observed selector sites.
The `0x15f3b40` radius probe was outside this fixture. All four
neighborhoods, surveys, and final decisions matched, including
the default against the whole-original classdeep2 route. Gameplay
used one muted instance at a time. All 14 Release CTest checks
and 14 replay and profile tests passed. The discovery queue
remains at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-pointclass1"
$c = "$r/tree-shake-default-pointclass2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_point_classifier --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The synthetic cases run with the same replay command and no capture
argument. The original gameplay capture used the private overlay
`generated-dll/shovel_point_capture_generated.dll`. The NSO, overlay
and captures stay local.

The first new generated height dependency is `0x15e06d0`.
The same classifier route reaches `0x15e026c` outside its verified
target-selector scope, then `0x15ea82c` for obstruction and
`0x15f0c64` for step geometry. The classifier also calls generated
stage lookups and filters, field-record lookup `0x195190c`, and
descriptor metadata on some paths. Classifier-origin verification
of the height group would remove the main dependency before the
record lookup and deeper classification.

### Surface and obstruction checks under the classifier

The point classifier uses the existing handwritten surface height
dispatcher `0x15e026c` at `0x15f4010`. Its obstruction check
`0x15ea82c` at `0x15f4024` also defaults to C++. The obstacle query
checks the center material and, when the radius is larger than
float epsilon, samples eight points in order. It retains the
overlay sampler at the guest boundary.

| Entry | Independent AArch64 cases | Focal calls per route |
| --- | ---: | ---: |
| `0x15e026c` | 768 | 40 |
| `0x15ea82c` | 840 | 40 |

The dispatcher replay now compares ordered writes. It includes
192 cases for the classifier's null output, zero flags and zero
bias, independently checked first on k3s at
`3a61aa2c1a2ca3f8b710beb373586e8f5b5edce5`.
The obstacle replay checks the sampled positions and incoming
material at each call. The no-op candidate failed on the first
center obstruction. Gameplay exercised radius zero. The synthetic
replay includes positive and negative radii and each angular probe.

The original and opt-in pointheight1 runs and the default
pointheight2 run matched both focal traces. All four neighborhoods,
surveys and final decisions matched the whole-original classdeep2
route. Gameplay used one muted instance at a time. All 14 Release
CTest checks and 14 replay and profile tests passed. The discovery
queue remains at 100. The existing dispatcher retains its earlier
verified ledger entry.

Set `NX_ACNH_SHOVEL_POINT_HEIGHT=original` for both original calls
under the classifier, or `NX_ACNH_SHOVEL_POINT_OBSTACLE=original`
for the obstacle check alone. The existing dispatcher switch also
applies at the classifier call.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-pointheight1"
$c = "$r/tree-shake-default-pointheight2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_height --classifier-live --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_obstacle --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

Original stage-specific height `0x15e06d0` and step geometry
`0x15f0c64` remain the main height dependencies. The private
pointheightcapture1 run captured 104 invocations of each and
replayed their original bodies twice against the captured child
effects. The next group starts with the stage-specific height
decision and its two principal callbacks `0x15e80f0` and
`0x15e7980`.

### Stage-specific height under the classifier

The stage-specific height decision at `0x15e06d0` now defaults to
C++ under the point classifier. It samples the base height, looks
up the relevant field item, and applies catalog and stage rules
for its height and material. The boundary retains the base sampler,
record lookup, and the generated descriptor and stage callbacks.
Set `NX_ACNH_SHOVEL_STAGE_HEIGHT=original` for the original route.

The independent replay matched 104 captured invocations across
three call sequences and 20 portable synthetic cases. It compared
exact float results, output material, ordered writes and child calls.
The no-op trial failed its first captured height and call sequence.
The synthetic cases exercise output material retention, distance
checks, the descriptor column lookup, and the primary and secondary
stage overrides.

The original and opt-in stageheight1 runs and the default stageheight2
run matched 40 focal calls each at `0x15f3ff8`. The gameplay call uses
stage 1, forces the near-item check, and omits the material output.
All four neighborhoods, surveys and final decisions matched the
whole-original classdeep2 route. Gameplay used one muted instance at
a time. All 14 Release CTest checks and 14 replay and profile tests
passed. Discovery remains at 100 entries.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-stageheight1"
$c = "$r/tree-shake-default-stageheight2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_stage_height --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The synthetic cases run without the capture argument. The default
trial also captured the generated child calls `0x15e80f0` and
`0x15e7980` in the private stage lookup overlay. Fifty-two paired
captures shared the same caller frame and stage context. Both
child bodies reproduced their captured results and ordered calls
in two independent AArch64 runs. The base sampler reaches the
current-stage overlay through a tail call, and the record lookup
reaches `0x195190c`. Start the next group with those dependencies.

### Stage base sampling and field-record lookup

The stage base sampler, stage item resolver, and field-record
selection now run in typed C++ on the shovel height and classifier
routes. They choose the stage view, apply the field query flags,
and produce the record and root half tile. Their original routes
remain selectable with `NX_ACNH_SHOVEL_STAGE_CONTEXT=original`.

| Entry | Independent AArch64 cases | Focal calls per route | Observed calls |
| --- | ---: | ---: | --- |
| `0x15e80f0` | 59 | 40 | `0x15e0720` |
| `0x15e7980` | 73 | 40 | `0x15e0794` |
| `0x195190c` | 240 | 80 | `0x15e7bf0`, `0x15f462c` |

For the first two entries, each replay includes 52 scoped gameplay
captures from stageheight2. The shared harness also provides 7, 21
and 240 portable synthetic cases respectively. The comparisons
check the return value, output material or root bytes, ordered
writes, and child calls. The no-op controls failed the initial
base-sampler case and the initial field-cell case.

The original and opt-in stagecontext1 runs and default stagecontext2
run matched all focal calls. Their neighborhoods, surveys and final
decisions matched the four outcomes from the whole-original
classdeep2 route. Gameplay used one muted instance at a time.
All 14 Release CTest checks and 14 replay and profile tests passed.
The queue remains at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-stagecontext1"
$c = "$r/tree-shake-default-stagecontext2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_stage_context --capture "$r/stage-height-children-live.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The portable cases run without the capture argument. The private
`generated-dll/shovel_stage_context_port_generated.dll` routes
the observed direct record calls through runtime dispatch.

The current-stage overlay call `0x83a950`, stage-name and filter
callbacks, record acceptance `0x194f798` and provider lookup
`0x11eb21c` still cross the guest boundary. Step geometry
`0x15f0c64` remains a generated classifier dependency. Its input,
result, and callback shapes are the next verification target.

### Shovel step geometry

The classifier's step check `0x15f0c64` now defaults to C++.
The typed routine calculates the projected step, checks the
endpoint and intermediate positions, and delegates collision
and traversal queries through the ABI boundary. Set
`NX_ACNH_SHOVEL_STEP=original` to select the original step.

The independent replay matched 104 captured calls and 320
portable synthetic cases. It compared return values, position
writes, and the exact input words at the heading, collision,
intermediate point and traversal callbacks. The first candidate
changed the intermediate probe height by the horizontal offset.
The original retains the starting height for those probes, as
confirmed by the first captured AArch64 argument trace.

The original and opt-in step1 runs and the default step2 run each
matched 40 focal calls at `0x15f418c`. Gameplay accepted the first
intermediate point. The synthetic cases cover endpoint rejection,
later probes, alternate distance and heading modes, and the
generated traversal fallback. All four neighborhoods, surveys
and final decisions matched the whole-original classdeep2 route.
The runs used one muted gameplay instance at a time. All 14
Release CTest checks and 14 replay and profile tests passed.
Discovery remains at 100 candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-step1"
$c = "$r/tree-shake-default-step2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_step --capture "$r/tree-shake-default-pointheightcapture1/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The portable cases run without the capture argument. The step
retains generated collision `0x15ed290`, intermediate acceptance
`0x15e9c80`, and traversal fallback `0x15f10f0`. The step2
default run captured 52 intermediate acceptance calls and 104
provider lookup calls to `0x11eb21c`. The next field group can
remove the underlying provider lookup and stage acceptance.

### Stage bounds and provider record leaf

The provider record lookup `0x11eb21c` now defaults to C++ on the
shovel stage-record path. It reads the selected provider view and
resolves a continuation record to its anchor. The earlier C++
bounds predicate `0x194f798` also passed verification at the stage
record's new call site. Its typed call now explicitly passes the
half-tile address to the ABI adapter.

| Entry | Independent AArch64 cases | Focal calls per route |
| --- | ---: | ---: |
| `0x11eb21c` | 344 | 80 |
| `0x194f798` | 464 | 80 |

Both totals include 104 captured invocations. The portable fixtures
exercise layer and channel fallback, inclusive bounds, missing
records, continuation anchors and absent root output. The first
provider candidate returned the fixed empty record for every bounds
failure. Case 411 exposed the configured empty record returned by
the original on Z-range and record-count rejections. The corrected
candidate matched both the returned record and ordered root writes.

The original generated bounds capture initially saw no calls because
the existing adapter already selected C++. Selecting
`NX_ACNH_SHOVEL_FIELD_BOUNDS=original` in recordleaf1 captured the
original at `0x1951934`. The provider is a tail call from
`0x19519ec`, reached from both `0x15e7bf0` and `0x15f462c`.
Set `NX_ACNH_SHOVEL_STAGE_PROVIDER=original` for the original
provider lookup.

The original and opt-in recordleaf1 and default recordleaf2 runs
matched each focal trace. All four neighborhoods, surveys, and final
decisions matched the whole-original classdeep2 route. Gameplay
used one muted instance at a time. All 14 Release CTest checks and
14 replay and profile tests passed. The queue remains at 100.
The bounds predicate retains its earlier verified ledger entry.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-recordleaf1"
$c = "$r/tree-shake-default-recordleaf2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_record_leaves --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The portable fixtures run without the capture argument. The next
observed step dependency is intermediate acceptance `0x15e9c80`.
The separate collision query `0x15ed290` and overlay sampling in
the newer classifier scopes remain at the guest boundary.

### Shovel intermediate point acceptance

The step probe's intermediate check `0x15e9c80` now defaults to
C++ on the shovel classifier path. It checks the center and eight
radial heights, preserves the original radius cutoff, and orders
the stage and obstruction callbacks as on AArch64. Guest frame
construction and selectable original routing remain in the adapter.
Set `NX_ACNH_SHOVEL_INTERMEDIATE=original` for the generated check.

| Entry | Independent AArch64 cases | Focal calls per route |
| --- | ---: | ---: |
| `0x15e9c80` | 108 | 40 |

Two fresh Unicorn runs agreed on each of the 52 captures and 56
portable cases, including callback order and argument words. The
no-op candidate failed the first portable case. The focal gameplay
reached the first step probe at `0x15f0fe0` with surface mode on.
Portable cases exercise the zero-radius exit, stage selection,
failed center and offset casts, and skipped offsets.

The original and opt-in intermediate1 and default intermediate2
runs matched all 40 focal calls. All four neighborhoods, surveys,
and final decisions matched across routes, including the default
comparison against the whole-original classdeep2 run. Gameplay
used one muted instance at a time. All 14 Release CTests and 14
Python replay and profile tests passed. The queue remains at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-intermediate1"
$c = "$r/tree-shake-default-intermediate2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_intermediate --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

Intermediate height sampling still reaches the generated
`0x15e026c` in this caller scope. Collision casts `0x15ed290`
and step traversal fallback `0x15f10f0` also remain generated.
The next group follows the observed center-marker call at
`0x15e9ccc` through `0x15ea0c8`, the shared mask predicate
`0x849ac8`, wrapper `0x876320`, and quadrant query `0x8493f8`.
The quadrant query also provides scoped reuse opportunities for
the verified terrain sampler and primary fallback.

### Shovel center and rim markers

The center-marker path now uses C++ for the extent check, global
terrain wrapper, bit-mask predicate, and corner query. The corner
query also uses the previously verified terrain sampler and primary
fallback at the new call sites. Set `NX_ACNH_SHOVEL_MARKER=original`
to route this group through the generated functions.

| Entry | Function | Independent AArch64 cases | Focal calls per route |
| --- | --- | ---: | ---: |
| `0x15ea0c8` | Check the center and rim for a material marker | 96 | 40 |
| `0x876320` | Check the current terrain for a marker | 232 | 160 |
| `0x849ac8` | Test four material kinds against the caller's mask | 548 | 360 |
| `0x8493f8` | Resolve the four corner material kinds | 516 | 360 |
| `0x862530` | Reuse the terrain record sampler | 328 | 360 |
| `0x87a980` | Reuse the primary terrain fallback | 80 | 1440 |

The four new ports replayed 52, 208, 468, and 468 original
marker1 invocations respectively, plus 196 portable cases. The
reference checked callback order and argument words, and the
quadrant query compared initial and final output words and ordered
writes. A portable case covers an aliased output. Another preserves
the original outgoing words where an out-of-bounds query returns
fallback kind 7. The original marker capture saw 1,872 primary
fallback calls returning 7. Non-7 behavior is covered by independent
fixtures. The first capture comparison exposed a test harness width
error in the sample reply, which carries a 64-bit address.

The original and opt-in marker1 and default marker2 runs matched
all six focal traces and the whole-original classdeep2 outcomes.
All three runs used one muted instance at a time. The 14 Release
CTest and 14 Python replay and profile checks passed. The discovery
queue remains at 100. Shared terrain leaves retain their earlier
verified entries.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-marker1"
$c = "$r/tree-shake-default-marker2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_marker --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_sample
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_fallbacks --entry 87a980
```

The primary fallback can still call stage and material helpers in
other scenes. The next group widens the verified height sampling
path to the classifier, intermediate point, stage base, and
obstruction probes. The step's collision query and traversal
fallback remain generated.

### Height sampling under the classifier and intermediate point

The typed height chain now handles the intermediate center and rim,
the classifier's surface children, the current-stage overlay tail,
and the obstruction probes. The original route for these new scopes
is selectable with `NX_ACNH_SHOVEL_HEIGHT_CHAIN=original`. Existing
root switches still select the generated bodies where supported.

| Entry | Function | Independent AArch64 cases | Focal calls per route |
| --- | --- | ---: | ---: |
| `0x15e026c` | Select surface height and material | 768 | 360 |
| `0x83b024` | Sample ground height and material | 128 | 400 |
| `0x83a950` | Sample overlay height and material | 128 | 480 |
| `0x241e98c` | Resolve height parameters | 30 | 800 |
| `0x60cba4` | Read a height parameter value | 3 | 1200 |
| `0x862700` | Sample the terrain grid | 672 | 880 |
| `0x22688e4` | Match the height parameter type | 180 | 800 |

The runs exercised intermediate surface calls `0x15e9d68` and
`0x15e9f9c`, the classifier surface call `0x15f4010`, the current
stage overlay tail returning to `0x15e0724`, and the obstruction's
center sampler. Avoid counting these seven functions a second time.

The original and opt-in heightchain1 and default heightchain2
runs matched every focal trace, including same-input grid checks.
All four neighborhoods, surveys, and final decisions matched the
whole-original classdeep2 route. All 14 Release CTests and 14
Python replay and profile checks passed. The discovery queue remains
at 100 and gameplay used one muted instance at a time.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-heightchain1"
$c = "$r/tree-shake-default-heightchain2"
& private/venv/Scripts/python -m tests.acnh_shovel_height_chain_live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_height
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_height_dependencies
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_grid_height
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_height_parameter_type
```

The separate stage-item surface call at
`0x15e08c0` had zero hits and retains its generated path. Retry with an
item setup that reaches the stage descriptor height branch. The step
cast `0x15ed290` and traversal fallback `0x15f10f0` still cross the
guest boundary. The next replay group follows the cast's height
difference and stage-item probes.

### Height and stage-item collision probes

The shovel step's collision query now uses C++ to compare the
surface and stage heights and to check the field item in the
selected stage. The generated collision query remains the caller.
Set `NX_ACNH_SHOVEL_COLLISION_PROBES=original` for both original
probe bodies.

| Entry | Function | Independent AArch64 cases | Focal calls per route |
| --- | --- | ---: | ---: |
| `0x15e11a0` | Compare surface and selected-stage heights | 968 | 720 |
| `0x15ec764` | Check the selected stage item and its distance | 352 | 168 |

The replay compared 864 height and 256 stage-item captures with
two independent original runs of each case. It also checked 104
and 96 portable cases respectively, including threshold boundaries,
scene selection, alternate terrain height, negative coordinates,
and item checks from both sides of the distance threshold. The
callback comparisons cover the stage descriptor, initial root
descriptor, resolved record, and height argument words.

Live coverage reached the height comparison through
`0x15ede4c` and `0x15ef5ac`, and the stage-item probe through
`0x15edfd4` and `0x15ef890`. The other static sites
`0x15ee934` and `0x15eebf0` had zero hits. Their generated
routes are retained for a smaller follow-up fixture.

The original and opt-in collisionprobe1 and default
collisionprobe2 runs matched all focal traces and all four
whole-original classdeep2 outcomes. All 14 Release CTests and 14
Python replay and profile checks passed. The 100-entry discovery
queue is intact. Gameplay used one muted instance at a time.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-collisionprobe1"
$c = "$r/tree-shake-default-collisionprobe2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_collision_probes --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The default run also captured 12 generated casts for each of the
three observed step callers of `0x15ed290`. Two independent
original runs agreed on returned masks and child order for all
36 captures. The next cast milestone covers the stage-item and
height-only masks at `0x15f0f10` and `0x15f0f48`. The wider
mixed-material cast at `0x15f0fc4` and traversal fallback
`0x15f10f0` remain generated.

### Simple shovel step casts

The stage-item and height-only forms of `0x15ed290` now run in C++
at their observed step callers. The typed query checks the center
and up to eight rim points. The height-hit path resolves the same
stage and invokes the field lookup before returning its mask.
Set `NX_ACNH_SHOVEL_SIMPLE_CAST=original` for these two original
forms.

| Cast mask | Observed caller | Focal calls per route |
| --- | --- | ---: |
| `0x10` | `0x15f0f10` | 40 |
| `0x4` | `0x15f0f48` | 40 |

The independent whole-function replay used 24 scoped original
captures and 218 portable fixtures. Both AArch64 runs and C++
matched the returned mask, callback order, point coordinates, and
stage descriptor words across all 242 cases. Portable fixtures cover
early and late hits, zero and boundary radii, per-call point changes,
and stage-name selection.

The original and opt-in simplecast1 and default simplecast2 runs
matched both focal traces and the four whole-original classdeep2
outcomes. All 14 Release CTests and 14 Python replay and profile
checks passed. Gameplay used one muted instance at a time. The
discovery queue remains at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-simplecast1"
$c = "$r/tree-shake-default-simplecast2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_simple_cast --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The mixed-material mask `0x67` at `0x15f0fc4` still uses the
generated cast. Count `0x15ed290` once after this remaining
observed caller passes its own replay and gameplay gates. Its
recorded child order puts the ground and overlay material tests
before the tile flag and height check. Traversal fallback
`0x15f10f0` still has portable original replay evidence only.

### Mixed-material shovel step cast

The remaining observed cast mask `0x67` at `0x15f0fc4` now
defaults to C++. The query uses the same center and rim order as
the simple casts. At each point it checks the base and overlay
materials, the tile flag, the overlay code and property, and then
the height difference. The boundary preserves sampler point edits,
material result slots, and the stage field lookup on a height hit.
Set `NX_ACNH_SHOVEL_MIXED_CAST=original` to select the generated
form.

Two original AArch64 runs matched the C++ result and ordered child
arguments on all 198 mixed cases. This includes 36 original captures
and 162 portable inputs covering all five result masks, missing rows
and columns, point mutation, equal and unequal materials, and the
radius cutoff. Large repeated sampler shims use separate replay
addresses to keep the original neighboring game entry ranges intact.

The original and opt-in mixedcast1 and default mixedcast2 runs
matched 40 focal mixed calls apiece. The default run also repeated
the two simple masks and matched 40 focal calls for each, plus all
four whole-original classdeep2 outcomes. The 14 Release CTests and
14 Python replay and profile checks passed. The 100-entry discovery
queue is intact. Gameplay used one muted instance at a time.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-mixedcast1"
$c = "$r/tree-shake-default-mixedcast2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_simple_cast --mixed --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

All three observed forms verify `0x15ed290` once. Other caller
families keep the generated dispatch. The generated ground and
overlay scopes inside the mixed cast and the generated traversal
fallback `0x15f10f0` are the main remaining step dependencies.
The flat fixture has zero traversal hits. A side-strip terrain
fixture is queued for the traversal evidence.

### Terrain sampling inside the mixed cast

The mixed cast now reaches the existing C++ ground, overlay, and
grid samplers and both material fallbacks. The secondary fallback
also reaches the existing C++ layer material predicate. Their new
caller scope retains the original route through
`NX_ACNH_SHOVEL_CAST_HEIGHT=original`.

| Entry | Independent AArch64 cases | Focal calls per route |
| --- | ---: | ---: |
| `0x83b024` | 128 | 360 |
| `0x83a950` | 128 | 1080 |
| `0x862700` | 672 | 1440 |
| `0x87a980` | 80 | 1404 |
| `0x87aa70` | 480 | 1044 |
| `0x1285040` | 42 | 1044 |

The original and opt-in castheight1 and default castheight2 runs
matched all 6,372 focal calls, including same-input grid checks and
material fallback results. They also matched all four neighborhoods,
surveys, and final decisions against the whole-original classdeep2
route. All 14 Release CTests and 14 Python replay and profile
checks passed. The six reused bodies retain their original ledger
entries and the Batch 3 count remains 72. The discovery queue
contains 100 entries.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-castheight1"
$c = "$r/tree-shake-default-castheight2"
& private/venv/Scripts/python -m tests.acnh_shovel_height_chain_live "$o/nx.log" "$c/nx.log" --cast --survey-tile 40 69
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_height_dependencies
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_grid_height
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_fallbacks
```

The step traversal fallback `0x15f10f0` had zero hits in the
traversalcapture1 side-strip test and traversalcapture2. Both tests
completed 52 step checks. Target traces in the retry placed the
actor on the original centerline. Retry with lateral movement after
the first shovel action. The next group follows the previously
observed alternate terrain property pair at `0x880630` and
`0x881090` and its shared cache.

### Alternate terrain material predicates

The two predicates under `0x87bf30` now call the typed field
query at all eight observed sites. They share the original column
cache. The first checks slot 13 and returns false for a missing
column. The second checks slot 6 and returns true for a missing
column on a valid row. An absent row returns false for both.
Set `NX_ACNH_SHOVEL_TERRAIN_PAIR=original` to select the generated
predicates.

| Entry | Independent standalone AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x880630` | 585 | 9296 |
| `0x881090` | 585 | 9296 |

Each standalone suite includes nine distinct gameplay inputs. The
48 additional paired replays pass the first result's cache into the
second predicate and match the full order and values of the writes.
Independent Linux and Windows replays also verified that the two
existing terrain property modes preserve their earlier results and
cache writes.

The first two gameplay attempts recorded zero traces because the
new adapters lacked dispatch registration. The archived probe
reproduced all 24 sampled leaf calls on the half-tile fixture and
identified the working scope. After registration, the original
and opt-in terrainpair3 and default terrainpair4 runs matched all
18,592 calls in order. Each requirement call consumed the prior
rejection call's final cache. All eight neighborhoods, surveys,
and final decisions matched. All 14 Release CTests and 14 Python
profile and replay checks passed. The discovery scan replaced the
two verified entries and the queue remains at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-terrainpair3"
$c = "$r/tree-shake-default-terrainpair4"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_pair --live "$o/nx.log" "$c/nx.log"
```

The default run captured 12 calls into the generated alternate
terrain rectangle check `0x87bf30` at `0x1288bd0`. The next
group follows that rectangle check and its level and layer tests.

### Alternate terrain rectangle check

The rectangle check `0x87bf30` now defaults to C++ from
`0x1288bd0`. The typed query runs the rectangle level check once
and then visits tiles in row order. It checks each tile's flags and
layer before reading the four materials and running their cached
predicates. Owner mode 1 selects layer 50 and other modes select
layer 1. Set `NX_ACNH_SHOVEL_ALTERNATE_TERRAIN=original` for the
generated rectangle check and level, layer, and sampler calls in
that scope. The material pair retains its own route switch.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x87bf30` | 158 | 570 |
| `0x871f80` | 224 | 570 |
| `0x1285fe0` | 122 | 2324 |
| `0x862530` | 328 | 2324 |
| `0x880630` | 585 | 9296 |
| `0x881090` | 585 | 9296 |

The 158 rectangle replays include 12 gameplay captures and compare
the complete ordered child arguments against two original runs.
The original and opt-in altterrain1 runs and the default altterrain2
run matched the scoped calls and all eight whole-fixture outcomes.
Each material requirement consumed the preceding rejection's
cache state. All 14 Release CTests and 14 Python profile and
replay tests passed. The parent adds one ledger entry. The five
reused bodies retain their earlier entries, and the discovery
queue contains 100 candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-altterrain1"
$c = "$r/tree-shake-default-altterrain2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_alternate_terrain --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_terrain_pair --live "$o/nx.log" "$c/nx.log"
```

The rectangle caller `0x1288630` remains generated. Inside the
main shovel step path, the next group follows surface-height
queries from the collision probes. Traversal fallback `0x15f10f0`
still needs a gameplay hit.

### Surface height inside the collision probes

Both collision probes now use the existing typed surface-height
chain on the shovel step route. The height difference probe calls
the dispatcher at `0x15e11d0` and the stage item probe calls it at
`0x15ec7b4`. Set `NX_ACNH_SHOVEL_COLLISION_HEIGHT=original` to
select the generated height chain at both sites.

| Entry | Independent AArch64 cases | Focal calls per gameplay route |
| --- | ---: | ---: |
| `0x15e026c` | 768 | 888 |
| `0x83b024` | 128 | 888 |
| `0x83a950` | 128 | 888 |
| `0x241e98c` | 30 | 1776 |
| `0x60cba4` | 3 | 2664 |
| `0x862700` | 672 | 1776 |
| `0x22688e4` | 180 | 1776 |

The original and opt-in collisionheight1 and default
collisionheight2 runs matched all 10,656 height-chain calls at
survey tile (40,69). Of the 888 dispatcher calls, 720 came from
the height difference probe and 168 from the stage item probe.
Frame timing varied outside the focal survey. The opt-in and
default routes matched the four whole-original classdeep2
surveys, neighborhoods, and final decisions. All 14 Release
CTests and 14 Python profile and replay checks passed. The seven
bodies retain their earlier entries. The Batch 3 count is 75 and
the discovery queue contains 100 candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-collisionheight1"
$c = "$r/tree-shake-default-collisionheight2"
& private/venv/Scripts/python -m tests.acnh_shovel_height_chain_live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The collision probes still reach the generated stage-specific
height and stage field lookup calls at `0x15e12bc` and
`0x15ec8b4`. The next group follows those calls and their
observed record and sampler dependencies.

### Stage height and field lookup inside the collision probes

The collision height probe now uses the typed stage-height query
at `0x15e12bc`. Its base-height sampler and primary item lookup
also run in C++, along with the current-stage overlay and grid
sampling. The stage record selection runs in C++ both under the
item resolver and at the collision item probe's direct
`0x15ec8b4` call. Set `NX_ACNH_SHOVEL_COLLISION_STAGE=original`
to select the generated route for these six functions.

| Entry | Independent AArch64 cases | Focal calls per gameplay route |
| --- | ---: | ---: |
| `0x15e06d0` | 124 | 720 |
| `0x15e80f0` | 59 | 720 |
| `0x15e7980` | 73 | 720 |
| `0x195190c` | 240 | 888 |
| `0x83a950` | 128 | 720 |
| `0x862700` | 672 | 720 |

The first collisionstage1 run sent inventory input during scene
loading, so it produced no shovel surveys. With a wait before
equipping, the original and opt-in collisionstage2 runs and the
default collisionstage3 run matched all 4,488 calls at tile
(40,69). The four surveys, neighborhoods, and final decisions
also matched the whole-original classdeep2 fixture. All 14
Release CTests and 14 Python profile and replay checks passed.
The six bodies retain their original entries. The Batch 3
count is 75, and the discovery queue contains 100 candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-collisionstage2"
$c = "$r/tree-shake-default-collisionstage3"
& private/venv/Scripts/python -m tests.acnh_shovel_collision_stage_live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

The original trace did not capture the provider tail call
`0x11eb21c` at `0x19519ec` because the test library called it
directly. The provider still uses generated code in this scope.
Retry the provider and its paired bounds predicate in a smaller
group with both calls routed through dispatch.

### Provider and bounds inside collision stage queries

The C++ field record selection now uses the typed bounds predicate
`0x194f798` and provider lookup `0x11eb21c` for collision probes.
Both paths preserve the record and output root. The direct probe
returns to `0x15ec8b8` and the stage resolver returns to
`0x15e7bf4` after the provider tail call. Set
`NX_ACNH_SHOVEL_COLLISION_PROVIDER=original` for these two
generated bodies in the collision scope.

| Entry | Independent AArch64 cases | Focal calls per gameplay route |
| --- | ---: | ---: |
| `0x194f798` | 464 | 888 |
| `0x11eb21c` | 344 | 888 |

The original and opt-in collisionprovider1 runs and the default
collisionprovider3 run matched all 1,776 calls in order, including
the input tile, accepted bounds, incoming and resulting root,
and record contents. Of the 888 pairs, 720 came through the stage
resolver and 168 came directly through the collision item probe.
The first default run sent movement input 60 frames later, and
14 bounds inputs differed at the target. The retry scheduled
inputs at the original HID acknowledgement frames and
reproduced the full trace. Both C++ routes matched all four
whole-original classdeep2 outcomes.

All 14 Release CTests and 14 Python profile and replay checks
passed. These two bodies retain their earlier ledger entries, so
the Batch 3 count stays at 75 and the discovery queue at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-collisionprovider1"
$c = "$r/tree-shake-default-collisionprovider3"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_record_leaves --collision --capture "$r/tree-shake-original-recordleaf1/console.log" --live "$o/nx.log" "$c/nx.log" --survey-tile 40 69
```

Stage filters at the newer classifier and collision call sites
still use generated code. The step traversal fallback still needs
a gameplay hit. The next groups cover the stage filters and
retry the traversal fixture with post-action lateral movement.

### Initial peak inside the shovel traversal fallback

Lateral movement after the first shovel action reached the
generated traversal fallback `0x15f10f0`. The first two
side-strip probes had missed it. The new fixture captured eight
complete traversal invocations. Two original AArch64 executions
per capture reproduced all 17 callback sites and results, the
caller output, and W0=1 using the recorded child effects.

The initial traversal peak `0x15f27e0` at `0x15f11e0` now
defaults to typed C++. It samples the origin and nine positions
in order, preserves ties, and uses the coordinates written by
a winning height callback. That call also uses the existing
C++ surface-height chain for its ten samples. Set
`NX_ACNH_SHOVEL_TRAVERSAL_PEAK=original` for the original route
at this call site and its height dependencies.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x15f27e0` | 84 | 9 |
| `0x15e026c` | 768 | 90 |
| `0x83b024` | 128 | 90 |
| `0x83a950` | 128 | 90 |
| `0x241e98c` | 30 | 180 |
| `0x60cba4` | 3 | 270 |
| `0x862700` | 672 | 180 |
| `0x22688e4` | 180 | 180 |

The peak cases include 75 portable cases and nine original
gameplay captures. The replays compare all three return
registers, the origin, and ten ordered callback inputs. The
original and opt-in traversalpeak1 runs and default
traversalpeak2 run used the same HID frame schedule and matched
all 1,089 scoped calls, eight captured traversal returns, ten
surveys and neighborhoods, and 12 final decisions. All 14
Release CTests and 14 Python profile and replay checks passed.
The peak adds one ledger entry. The seven reused functions
retain their earlier entries and the discovery queue stays at
100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-traversalpeak1"
$c = "$r/tree-shake-default-traversalpeak2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_traversal_peak --capture "$o/nx.log" --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m tests.acnh_shovel_height_chain_live "$o/nx.log" "$c/nx.log"
```

The traversal parent, recursive calls, and both modes of sweep
`0x15f23b0` still use generated code. The latest original run
captured six sweeps per mode. Those captures reproduced the
original sweep body's ordered callbacks and unchanged outputs
in AArch64 using recorded child replies. The sweeps are the
next implementation group.

### Segment sweeps inside shovel traversal

Both modes of `0x15f23b0` now default to C++ at the original
traversal parent's calls `0x15f2140` and `0x15f215c`. Both
test up to 19 positions along the segment. Mode 1 first chooses
the nearest half-tile edges and samples their surface heights
through the existing C++ height chain. A collision that accepts
without blocking stops the sweep and, when supplied, writes
the output's Z word before its X/Y pair. Set
`NX_ACNH_SHOVEL_TRAVERSAL_SWEEP=original` to select the generated
sweep and its height chain in this scope.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x15f23b0` | 114 | 18 |
| `0x15e026c` | 768 | 18 |
| `0x83b024` | 128 | 18 |
| `0x83a950` | 128 | 18 |
| `0x241e98c` | 30 | 36 |
| `0x60cba4` | 3 | 54 |
| `0x862700` | 672 | 36 |
| `0x22688e4` | 180 | 36 |

The sweep replay includes 96 portable cases and 18 original
gameplay inputs. The portable cases exercise both modes,
nullable output, empty and populated destination records,
blocked hits, and hits at the first and last probes. The live
cases reached nine sweeps in each mode. Each returned W0=1
with unchanged XYZ after 19 misses. The original and opt-in
traversalsweep1 runs and the default traversalsweep2 run matched
234 scoped calls, all 342 ordered collision inputs and outputs,
nine traversal results, ten surveys and neighborhoods, and
12 final decisions. All 14 Release CTests and 14 Python profile
and replay checks passed. The sweep counts once, so the ledger
is at 77 and the discovery queue stays at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-traversalsweep1"
$c = "$r/tree-shake-default-traversalsweep2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_traversal_sweep --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m tests.acnh_shovel_height_chain_live "$o/nx.log" "$c/nx.log"
```

The traversal parent and recursive calls still use generated
code. The sweep retains generated stage-name selection,
stage filtering, record selection, and the collision cast
at its guest boundary. The observed stage-name and filter
callbacks are the next group.

### Stage helpers inside traversal sweeps

Both sweep modes now use the existing C++ stage-name lookup,
terminated hash, and stage filter with its two predicates.
The name getter uses the observed virtual target `0x5cb0a0`.
The stage filter receives the selected key from the sweep at
`0x15f2654`. Set `NX_ACNH_SHOVEL_SWEEP_STAGE=original` to
select the generated route for all five functions in this scope.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x5cb0a0` | 24 | 18 |
| `0x5bd550` | 209 | 18 |
| `0x16baf44` | 1505 | 18 |
| `0x16f37d0` | 197 | 18 |
| `0x16f3a18` | 108 | 18 |

The original and opt-in sweepstage1 runs and default sweepstage2
run matched all 90 calls in order, including name index, hash
bytes and seed, predicate inputs, and all returns. They also
matched nine traversal results, ten surveys and neighborhoods,
and 12 final decisions. All 14 Release CTests and 14 Python
profile and replay checks passed. The five functions retain
their earlier entries. The ledger is at 77 and the discovery
queue contains 100 candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-sweepstage1"
$c = "$r/tree-shake-default-sweepstage2"
& private/venv/Scripts/python -m tests.acnh_shovel_sweep_stage_live "$o/nx.log" "$c/nx.log"
```

The sweep's record lookup `0x195190c` at `0x15f2670` still
uses generated code. Its bounds check and provider lookup
are the next group so the destination record and root can be
verified through the typed interface.

### Destination records inside traversal sweeps

The sweep now uses typed C++ for record selection, the bounds
predicate, and the provider tail call. All three are reached
from `0x15f2670`. The bounds callback returns to `0x1951938`.
The provider inherits the sweep's return address `0x15f2674`.
Set `NX_ACNH_SHOVEL_SWEEP_RECORD=original` for this scope.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x195190c` | 240 | 18 |
| `0x194f798` | 464 | 18 |
| `0x11eb21c` | 344 | 18 |

The original and opt-in sweeprecord1 runs and default
sweeprecord2 matched all 54 ordered calls. Bounds accepted
the sampled tiles, the provider updated the root coordinates,
and the records contained the `0xfffe` empty item marker.
All 342 default collision calls matched the original
traversalsweep1 inputs and outputs, including the absent
destination argument. Separate original AArch64 composition
cases exercised rejected bounds and a nonempty record with
an outgoing root. They modeled the surrounding heading, filter
and collision effects.

The gameplay runs matched nine traversal results, ten
surveys and neighborhoods, and 12 final decisions. All 14
Release CTests and 14 Python profile and replay checks passed.
The three bodies retain their ledger entries. The ledger
is at 77 and the discovery queue stays at 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-sweeprecord1"
$c = "$r/tree-shake-default-sweeprecord2"
& private/venv/Scripts/python -m tests.acnh_shovel_sweep_stage_live --record "$o/nx.log" "$c/nx.log"
```

The sweep's collision call `0x15ed290` at `0x15f2780` still
uses its generated 16-sector mode with separate output and
block-flag pointers. The outer traversal parent also still
uses generated surface and stage heights directly. Follow
those outer height calls next.

### Direct heights inside outer shovel traversal

The generated traversal parent calls the typed surface-height
chain at `0x15f11d0`, `0x15f1bcc`, `0x15f1e3c`, `0x15f20d0`,
and `0x15f20e8`. Calls at `0x15f1cb8` and `0x15f1f28` use typed
stage height. The scope requires the traversal's shovel-step
caller and accepts only the direct parent frame. Set
`NX_ACNH_SHOVEL_TRAVERSAL_HEIGHT=original` to select the generated
height functions for these sites.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x15e026c` | 768 | 45 |
| `0x83b024` | 128 | 45 |
| `0x83a950` | 128 | 45 |
| `0x241e98c` | 30 | 90 |
| `0x60cba4` | 3 | 135 |
| `0x862700` | 672 | 90 |
| `0x22688e4` | 180 | 90 |
| `0x15e06d0` | 124 | 18 |

Stage-height calls carried S0=15.0 from the preceding surface
query into the original body and returned 15.0 on all 18 calls.
The original and opt-in `traversalheight1` runs and default
`traversalheight2` run matched 558 ordered calls across all
seven direct call sites and their traced descendants. The three
routes matched nine traversal results, ten surveys, ten
neighborhoods, and 12 final decisions. All 14 Release CTests
and 14 profile and replay checks passed. These eight bodies
retain their earlier entries. The ledger remains at 77 verified
functions with 100 in the discovery queue.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-traversalheight1"
$c = "$r/tree-shake-default-traversalheight2"
& private/venv/Scripts/python -m tests.acnh_shovel_traversal_height_live "$o/nx.log" "$c/nx.log"
```

The generated stage-height base and field-item resolver remain
on this caller path, as do recursive traversal and the 16-sector
sweep collision callback. The collision callback is the next
unported dependency under the typed sweep.

### Cached row values under the shared field lookup

Eight readers under `0x29cce10` use `CachedRowValueQuery` for
the owner, slot, and property key. The adapter admits only the
eight direct return sites. The caller still chooses the row,
compares each returned value, and controls the early exit. Set
`NX_ACNH_FIELD_VALUE_CACHE=original` to select the generated
readers. The four earlier siblings at slots 12 through 15 are
replay-tested and still use generated code.

The first `rowcache1` trial matched all eight shovel outcomes,
but the strict full trace found 2,976 original and 2,978 opt-in
parent calls, with different ambient requests. The
`rowcache2` retry recorded the exact word consulted by each warm
reader. Original and opt-in `rowcache2` and default `rowcache3`
matched the ordered group that resolved all eight slots and the
19 distinct read-only inputs. Every read-only call preserved
the cache and returned its recorded value. Ambient repeats
varied across the three routes. The survey, neighborhood, and
final decisions each matched on all eight shovel interactions.

Each body matched 122 independent portable AArch64 cases
and two or three additional gameplay-derived read-only cases.
The replay also checked 363 cases for the eight-slot column
lookup and 488 for the four earlier row readers. All 14
Release CTests and 14 profile and replay checks passed.
Batch 3 has 85 verified functions. The updated discovery probe
confirmed 65 direct entries. Four moved into the ledger and
four replacements keep 100 candidates in the discovery queue.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-rowcache2"
$c = "$r/tree-shake-default-rowcache3"
& private/venv/Scripts/python -m tests.acnh_field_value_cache_live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_value_cache --capture "$o/nx.log"
```

### Sixteen-sector sweep collision

The traversal sweep's call at `0x15f2780` now uses a typed
collision query for mask 4 and mode `0x10001`, with writable XYZ
and a separate block flag. The observed calls had no destination
root. Calls with a destination root use the generated body. The
C++ route tests the center and all 16 rim points and retains the
first hit's XYZ. It records the block flag independently of W0.
Set `NX_ACNH_SHOVEL_SWEEP_COLLISION=original` to select the
generated sweep collision body. The other cast modes keep their
existing routes.

Independent AArch64 replay compared 96 portable cases and 36
captured sweep calls. It checked the callback order, probe XYZ and
S0, root inputs, returns, and ordered output writes. The original
center and rim height probes and contact-stage helpers remain
separate callbacks. The existing simple and mixed cast replays
also passed all 218 and 162 cases, respectively.

Original and opt-in `sweepcollision1` and default
`sweepcollision2` matched 342 ordered casts, including 102 hits
and 240 misses. The measured heading increment was `0x10000000`.
The traces also matched nine traversal results, ten surveys,
ten neighborhoods, and 12 final decisions. All 14 Release CTests
and 14 profile and replay checks passed. This caller uses the
already counted cast body. Batch 3 is at 85 verified functions
with 100 candidates in discovery.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-sweepcollision1"
$c = "$r/tree-shake-default-sweepcollision2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_sweep_collision --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log"
```

The next group follows the center and rim calls to `0x15e11a0`
and its surface-height chain.

### Height probes inside the sixteen-sector sweep

The center call at `0x15ede4c` and rim call at `0x15ef5ac` use
the typed height-difference probe under the sweep caller. The
probe uses the existing surface dispatcher and its six height
helpers at `0x15e11d0`. Set `NX_ACNH_SHOVEL_SWEEP_PROBE=original`
to select the generated versions for this chain.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x15e11a0` | 140 | 5814 |
| `0x15e026c` | 768 | 5814 |
| `0x83b024` | 128 | 5814 |
| `0x83a950` | 128 | 5814 |
| `0x241e98c` | 30 | 11628 |
| `0x60cba4` | 3 | 17442 |
| `0x862700` | 672 | 11628 |
| `0x22688e4` | 180 | 11628 |

The probe replay includes 36 captures, 18 at each site. The
live comparison checks the ordered probe XYZ, incoming S0,
flags, return value, and every scoped surface result. The
producer consumes W0 and reloads the point before contact
handling. Original and opt-in `sweepprobe1` and default
`sweepprobe2` matched 75,582 scoped calls, 342 sweep casts,
nine traversal results, ten surveys, ten neighborhoods, and 12
final decisions. All 14 Release CTests and 14 profile and replay
checks passed. The eight functions retain their existing entries.
The count is 85 verified, with 100 in discovery.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-sweepprobe1"
$c = "$r/tree-shake-default-sweepprobe2"
& private/venv/Scripts/python -m tests.acnh_shovel_traversal_height_live --sweep-probe "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_collision_probes --capture "$o/console.log"
```

Stage height at `0x15e12bc` and the associated base, item and
provider queries are the next generated dependencies within the
probe.

### Shared stage subtree under shovel traversal

The sweep probe's stage query at `0x15e12bc` and the two
existing outer stage calls share the same typed C++ base
height, overlay and grid sampling, item resolution, bounds,
and provider lookup. Resolver returns at `0x15e0798` and
`0x15e0c54` pass the field record lookup at `0x15e7bf4`.
The provider tail inherits that return address. The bounds
adapter retains the separate lookup frame at `0x1951938`.
Set `NX_ACNH_SHOVEL_TRAVERSAL_STAGE=original` for this new scope.

| Entry | Independent AArch64 cases | Calls per gameplay route |
| --- | ---: | ---: |
| `0x15e06d0` | 124 | 5814 |
| `0x15e80f0` | 59 | 5832 |
| `0x83a950` | 128 | 5832 |
| `0x862700` | 672 | 5832 |
| `0x15e7980` | 73 | 6051 |
| `0x195190c` | 240 | 6051 |
| `0x194f798` | 464 | 6051 |
| `0x11eb21c` | 344 | 6051 |

The 18 outer calls use the existing direct stage-height route
at their entry. Each contributes to the stage-base and item
subtree measured here. Original and opt-in `traversalstage1`
and default `traversalstage2` matched 47,514 ordered records
across all eight bodies. The 342 sweep casts, nine traversal
results, ten surveys, ten neighborhoods, and 12 final decisions
also matched. All 14 Release CTests and 14 profile and replay
checks passed. The functions retain their earlier entries.
Batch 3 stays at 85 verified and 100 discovery candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-traversalstage1"
$c = "$r/tree-shake-default-traversalstage2"
& private/venv/Scripts/python -m tests.acnh_shovel_traversal_stage_live "$o/nx.log" "$c/nx.log"
```

The stage-name and predicate calls
from this subtree remain generated. The next group ports the
direct traversal parent that coordinates the height checks,
recursive legs, and two sweeps.

### Direct shovel traversal fallback

The step's call at `0x15f10bc` now enters the typed
`ShovelTraversal` query. It coordinates the initial peak,
same-tile and boundary rectangles, stage split, recursive
legs, and both sweep modes. The adapter handles the guest
frame, vector registers, and scoped routing. The null-output
slot is at SP+0x88. Intermediate points use separate slots.
Set `NX_ACNH_SHOVEL_TRAVERSAL=original` to choose the generated
parent. Recursive calls and other callers still use generated
code.

Two independent original AArch64 runs per case matched 152
portable cases and nine gameplay-derived calls, including
callback order and arguments, return values, and external
writes. Portable controls cover the rectangle branches,
thresholds, overlapping output, null output, both split axes,
sweep exits, and peak tie-breaking. The nine measured gameplay
calls returned 1 with unchanged output.

Original and opt-in `typedtraversal1` and default
`typedtraversal2` matched all nine descriptors, outputs and
returns, 342 sweep casts, ten surveys, ten neighborhoods,
and 12 final decisions. All 14 Release CTests and 14 profile
and replay checks passed. Batch 3 has 86 verified functions
and 100 discovery candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-typedtraversal1"
$c = "$r/tree-shake-default-typedtraversal2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_traversal --capture "$o/console.log" --live "$o/nx.log" "$c/nx.log"
```

The first two recursive legs reached by this fixture are the
next traversal dependencies. The discovery queue also contains
eight observed terrain triangle readers that share one query
implementation.

### Terrain triangle predicates

Eight observed predicates now share the typed terrain triangle query.
The four slope checks use the ground material, overlay suppression, and
the cell flag. The four flatness checks use the same material and height
layout. The runtime unpacks the half-tile descriptor and limits the C++
route to the 22 verified call sites. Set `NX_ACNH_TERRAIN_TRIANGLES=original`
to use the generated route.

| Main entry | Unwind size | Typed predicate | Observed caller | Stage |
| --- | --- | --- | --- | --- |
| `0x866f60` | `0x170` | Triangle 0 slope | `0x865ec0` | Verified |
| `0x8670d0` | `0x178` | Triangle 1 slope | `0x8663d0` | Verified |
| `0x867248` | `0x178` | Triangle 2 slope | `0x8680f0` | Verified |
| `0x8673c0` | `0x178` | Triangle 3 slope | `0x8658f8` | Verified |
| `0x867538` | `0x130` | Triangle 0 flatness | `0x867ac0` | Verified |
| `0x867668` | `0x13c` | Triangle 1 flatness | `0x8658e8` | Verified |
| `0x8677a4` | `0x13c` | Triangle 2 flatness | `0x865eb0` | Verified |
| `0x8678e0` | `0x13c` | Triangle 3 flatness | `0x8663c0` | Verified |

Each entry matched both original AArch64 executions on 174 cases, with
no caller-visible writes. Independent Linux replay covered all 1,392
cases. The first original run captured all 22 sites but logging volume
delayed startup and missed the shovel action. The bounded original retry
and both C++ routes matched eight surveys, eight neighborhoods, and
eight final decisions. The gameplay checker compares bounded
caller/tile/result classes with the original inventory and checks every
sample on the same inputs. All 5,853 opt-in and 6,258 default classes
matched the full original inventory. The two bounded traces each logged
6,505 calls. They differed by 33 classes in each direction, so the
comparison uses the full original inventory for sampled class coverage.
The default logged 162, 192, 112 and 176 true results for the four slope
predicates respectively, and both outcomes for every predicate.
All 14 Release CTests and 14 profile and replay checks passed.

The direct traversal parent, sweep, and measured height paths remain
typed. Recursive traversal legs, rectangle callbacks, and stage helpers
outside their measured scopes still use generated code. The next queue
group completes four observed row readers that share the typed cache.

### Early field row readers

The four earlier lookups at slots 12–15 now use the same typed row
cache as the eight downstream readers. Each is guarded by its BL
return address under `0x29cce10`. Set
`NX_ACNH_FIELD_VALUE_EARLY_CACHE=original` to select the original bodies
for these four call sites.

Each reader passed 122 portable cases against two independent
original AArch64 executions. The cases compare return values and
exact cache write order. The additional 22 captured read-only
cases matched, for 510 focused comparisons. The full matrix passed
1,849 cases including the other eight readers and the column resolver.

Original and opt-in `earlyrow1` and default `earlyrow2` matched
two ordered cache-resolving groups that cover all four entries,
22 read-only inputs, eight surveys, eight neighborhoods, and
eight final decisions. Original and default recorded 3,106 parent
groups. Opt-in recorded 3,096. The state-changing groups and the
distinct read-only inputs agree across routes. All 14 Release CTests
and 14 profile and replay checks passed. Batch 3 has 98 verified
functions and the discovery queue still holds 100.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-earlyrow1"
$c = "$r/tree-shake-default-earlyrow2"
& private/venv/Scripts/python -m tests.acnh_field_value_cache_live --early "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf field_value_cache --early --capture "$o/nx.log"
```

### Terrain material masks

The ground and overlay material predicates now use a typed query for
the half tile, terrain layer, and 96-bit mask. Each resolves the grid
from its caller's terrain owner and checks the four material bytes.
Both share the material-bit test with the shovel marker. The C++ route
applies at the verified BL for each entry. Set
`NX_ACNH_TERRAIN_MATERIAL=original` to call the generated predicates.

| Main entry | Unwind size | Typed predicate | Observed caller | Stage |
| --- | --- | --- | --- | --- |
| `0x84b728` | `0x118` | Ground material intersects mask | `0x15753e0` | Verified |
| `0x84b840` | `0x118` | Overlay material intersects mask | `0x15755c0` | Verified |

Each independently matched 136 cases against two original AArch64
runs with no caller-visible writes. The marker regression passed
its 196 cases after sharing the bit test. Original and opt-in
`terrainmaterial1` and default `terrainmaterial2` matched all
414,358 ordered material calls and the eight survey, neighborhood, and
final outcomes. Each route exercised 464 true and 28,456 false ground
queries, plus 1,752 true and 383,686 false overlay queries. The
same-input check matched every recorded call. All 14 Release CTests
and 14 profile and replay checks passed.

## Active batch 4

The discovery queue starts with [100 candidates](batch-4-candidates.md).
It carries forward unverified entries and adds replacements for the
two promoted material queries. Verified so far, 1.

Implementation continues on the typed shovel traversal path.

### First-axis shovel traversal legs

The recursive calls at `0x15f1dd8` and `0x15f1e24` now use the same
typed coordinator as the outer traversal. The runtime verifies their
outer traversal, step, and classifier ancestry. Their initial peak
and height queries use the same scoped C++ route. Set
`NX_ACNH_SHOVEL_TRAVERSAL_LEGS=original` to restore the original legs
and their height subtree independently of the outer coordinator.

The traversal replay matched 152 portable cases and 18 gameplay
calls against two original AArch64 executions per case. The peak
and all seven height bodies also passed their independent replay
suites. The first original run reached all nine bodies, but its
generated outer call bypassed the runtime leg-entry trace. The
retry kept the outer coordinator typed across the three routes
and captured all leg inputs, outputs, and returns.

Original `traversalleg2`, opt-in `traversalleg1`, and default
`traversalleg3` matched 2,412 ordered calls across the nine bodies,
including both legs, 18 peak calculations, and 198 height dispatches.
They also matched nine outer traversal results, 342 sweep casts, ten
surveys, ten neighborhoods, and 12 final decisions. All 14 Release
CTests and 14 profile and replay checks passed. The functions
retain their Batch 3 entries. Batch 4 still has 100 discovery candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-traversalleg2"
$c = "$r/tree-shake-default-traversalleg3"
& private/venv/Scripts/python -m tests.acnh_shovel_traversal_leg_live "$o/nx.log" "$c/nx.log"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_traversal --capture-legs "$o/console.log"
```

The live original legs reached 144 rectangle checks at 16 caller sites.
Those checks go next. The alternate recursion axis and stage-name and
rule calls outside the verified scopes remain generated.

### Rectangle height breaks in traversal legs

| Entry | Unwind size | Runtime scope | Stage |
| --- | --- | --- | --- |
| `0x8721a0` | `0x75c` | 16 observed rectangle calls in the two first-axis legs | Verified |

The typed rectangle query checks height changes across adjacent
triangle edges. It reads terrain rows and chooses the overlay per
triangle. The runtime unpacks the inclusive rectangle and follows
the two first-axis leg frames back to the direct shovel traversal.
The scoped calls all pass mode zero. Set
`NX_ACNH_SHOVEL_TRAVERSAL_RECTANGLE=original` to restore the generated
rectangle function and its sampler. Other callers, including direct
or alternate-axis traversal checks, keep that route.

Two AArch64 executions per input matched 562 portable cases and
16 distinct gameplay inputs. The original replay runs the actual
terrain sampler at `0x862530` when the mode selects it, and checks
the rectangle result and absence of caller-visible writes. There
were 372 true portable results. All 144 rectangle calls per live
route returned false, with matching bounds, terrain rows, and
results across all 16 call sites.

Original and opt-in `traversalrect1` and default `traversalrect2`
also matched the 2,412 leg records, nine direct traversals, 342
sweep casts, ten surveys, ten neighborhoods, and 12 final
decisions. All 14 Release CTests and 14 profile and replay
checks passed. Batch 4 has one verified function and 100
discovery candidates.

```powershell
$r = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$o = "$r/tree-shake-original-traversalrect1"
$c = "$r/tree-shake-default-traversalrect2"
& private/venv/Scripts/python -m private.heading.compare_leaf shovel_traversal_rectangle --capture "$o/nx.log" --live "$o/nx.log" "$c/nx.log"
```

Next are the stage-name and rule callbacks reached by the
outer traversal, sweep probes, and stage-item resolution.
