# Shovel field queries on ACNH 3.0.3

The active implementation path starts at the field survey called from
`main+0x21f58dc`. Its entry is `main+0x21f6fe0`, unwind size `0xc44`.
This path covers field queries during shovel targeting and classification.
The action engine continues to own item removal, inventory, and hole creation.

| Survey call site | Target | Field-query responsibility | State |
| --- | --- | --- | --- |
| `0x21f70d4` | `0x15f36e0` | Select the survey target | C++ selector and height dispatch with a generated point-classification callback |
| `0x21f74fc` | `0x87b460`, `0x1284ee0` | Check terrain before gathering items | C++ terrain gate and layer-one predicate on the observed route |
| `0x21f7568` to `0x21f75a4` | `0x3f04450`, `0x127c524`, `0x127c6ec` | Prepare the survey scratch data | `0x127c6ec` and `0x127c524` are verified C++ defaults. The empty-item initialization path also defaults to C++. |
| `0x21f7614` | `0x127cab0` | Gather the neighborhood records and occupancy flags | Verified C++ default at the shovel call site |
| `0x21f76b0` | `0x267d6c4` | Resolve an item ID | Verified C++ default with original auxiliary fallback `0x1a03610` |
| `0x21f7788` | `0x877130` | Test the selected terrain | Verified C++ dispatcher, rectangle height, quadrant materials and record reader |
| `0x21f77ac` through `0x21f7944` | `0x127d05c` | Classify field item and terrain properties | Verified C++ classifier and entry checks with generated metadata and category callbacks |
| `0x21f787c` | `0x1281c54` | Find the field record at a half-tile position | Verified C++ default through typed field queries |
| `0x21f790c`, `0x21f7bb8` | `0x8773f0` | Check the alternate terrain condition | Generated, with leaf `0x880a10` |

The shovel survey hook at `main+0x21f7618` consumes the
neighborhood results. Its fractional-placement path also uses the generated
anchor lookup `main+0x15e1014` via the 3.0.3 field helper. This call does
not appear among the survey's original direct BL instructions.

The disposable shovel trace confirmed the survey's half-tile call at
`0x21f787c`. It also observed the terrain leaf `0x880870` at all four
call sites in `0x877130`. The layer flag leaf `0x1285fe0` was observed
at another caller, `0x87c048`. Capture its call from `0x87737c` before
counting it toward the shovel path. The item ID resolver was observed at
other callers. A callee hit from a different path only establishes that
the candidate runs in gameplay.

## Implementation sequence

1. Extract the existing half-tile query into a typed interface that takes
   a field grid and half-tile coordinates, with register decoding and the
   original toggle in the runtime adapter. Replay the established half-tile
   fixtures and capture it in the next shovel trial.
2. Port `0x1285fe0` through the same typed query interface. Port the terrain
   record predicate `0x880870` using the verified packed row and column
   conventions. Begin the group with these leaves and extend it once the
   traces confirm more relevant callers.
3. Use the terrain leaves to move `0x877130` behind a typed terrain query.
   Resolve and replay the `0x848e68` and `0x871f80` dependencies first.
   This removes a generated terrain call from the shovel path and from
   the half-tile placement check.
4. Collect original and replacement traces for the 3-by-3 occupancy
   survey and item classification. Use the survey outputs in the same
   disposable shovel scenario to verify the composed path, including
   the selected record and outcome.

The next gameplay validation is one muted original, opt-in and default run
for the group. A function needs its own passing replay and a hit on each
route before the ledger counts it.

## Neighborhood gather dependencies

`0x127cab0` receives the survey scratch object in X0. The survey sets
its neighborhood bounds before this call. The gather populates the
records at offset `0x30` and the occupancy mask at offset `0x18`. The
shovel survey consumes these outputs at `0x21f7618`.

| Gather call site | Target | Status |
| --- | --- | --- |
| `0x127cb2c` | Virtual stage lookup, observed `0x5cb0a0` | Verified C++ default for the observed target |
| `0x127cb38` | Hash the stage key | Verified C++ default |
| `0x127cb68` | `0x16baf44` stage predicate | Verified C++ default with both stage-key helpers |
| `0x127cc84`, `0x127cf84`, `0x127cfd0`, `0x127cff8` | `0x195033c` field-cell lookup | Verified C++ default on these four call sites |
| Five calls from `0x127ccb8` through `0x127ce50` | `0x267d6c4` item-ID resolution | Verified C++ default |
| `0x127cd94`, `0x127cea0` | `0x19ee3b4` metadata, `0x253fe7c` extent | Metadata defaults to C++. The extent helper remains generated. |

The field-cell lookup is the next common dependency. It checks layer
and channel bounds through `0x194f798`, initializes an empty item through
`0x267d4ac`, and invokes a provider through `0x11e4f58`. The provider uses
virtual dispatch. The lookup returns the item into the caller's X8
result buffer. Its X3 argument carries the resolved position. The typed bounds and cell queries are verified defaults on the shovel path.
The runtime boundary maintains the original routes. Both the provider
and the guarded empty-item initializer have typed C++ defaults. The original, opt-in and default trials matched
the result item and resolved position at all four gather call sites.

## Verified neighborhood path

The gather at `0x127cab0` now calls the typed field-cell interface for
the 3-by-3 survey. The stock, opt-in and default runs matched its full
scratch result, including the 36 records and the occupancy mask.
Outside the shovel call site, its existing generated entry is used.

The field provider `0x11e4f58` runs its occupancy filter and record
selection in C++. It calls five verified handwritten methods for table
lookup, readiness and indexing. The runtime boundary supplies generated
context callbacks. Continuation handling, the empty-item initializer
and metadata classification now default to C++. The original extent
helper `0x253fe7c` remains at the boundary. The shovel action engine
continues after the survey.

## Field provider dependencies

All observed dispatch targets below came from the field-cell call at
`0x1950410` under a shovel neighborhood gather.

| Provider call sites | Runtime entry | State |
| --- | --- | --- |
| `0x11e4fa4`, `0x11e4fcc`, `0x11e5004`, `0x11e50a0`, `0x11e516c`, `0x11e5190` | `0x11ac1e8`, table lookup | Verified C++ default. The table resolver remains dynamic. |
| `0x11e4ff0` | `0x11ac290`, readiness | Verified C++ default |
| `0x11e4fb0`, `0x11e50b0`, `0x11e5178` | `0x261157c`, width | Verified C++ default |
| `0x11e4fd8`, `0x11e50c4`, `0x11e519c` | `0x2611584`, height | Verified C++ default |
| `0x11e5114`, `0x11e51cc` | `0x261158c`, record displacement | Verified C++ default |
| `0x11e50f0`, tail `0x11e5284` | `0x260b860`, `0x260b934`, context dispatch | Verified C++ defaults, including the scoped context acquisition and release routines. |
| `0x11e51b4`, `0x11e51fc` | `0x11e4578`, `0x2540184`, continuation resolve and cleanup | Verified C++ defaults with guest synchronization calls |

The typed provider read is verified in gameplay. Its independent replay
covers bounds rejection, masking, ordinary and continuation records,
optional root output and context release.

## Terrain dispatcher group

The muted `tree-shake-default-terrainpath1` probe recorded two stock
`0x877130` calls from `0x21f7788`. It observed `0x871f80` twice from
`0x877254` and `0x848e68` eight times from `0x87732c`. Both helpers
called the shared terrain record reader `0x862530` eight times from
their respective sites at `0x872110` and `0x848ed0`. These four
functions connect the shovel survey to the terrain record property and
the layer flag check that already run in C++.

The dispatcher, rectangle height check `0x871f80`, quadrant materials
`0x848e68`, and shared record sampler now default to C++ on the observed
shovel path. The quadrant check can call `0x87a980` and `0x87aa70` for
world-position fallbacks. The alternate terrain dispatcher
`0x8773f0` remains generated.

## Item classification

The half-tile probe `tree-shake-original-shovelclassifyhalf1` observed six
calls at each of `0x21f77ac`, `0x21f77c8`, `0x21f77dc` and
`0x21f7944` into `0x127d05c`. The classifier first checks
`0x267da10` and conditionally checks `0x267e024`. It can then use the
item ID, descriptor metadata `0x19e59b0` and `0x19e6020`, descriptor
construction `0x114c030`, and category checks through `0x114dcb0`,
`0x113d890` and `0x113db50`. The half-tile probe reached the first two
gates. The later `shovelclassdeep2` fixture reached populated descriptor
metadata for the roses and the fossil.

The classifier and both entry checks now default to C++. The six
half-tile outcomes exercised the two early rejections. Replay also
covered accepted results and the remaining descriptor branches.

## Stage and provider dispatch

The stage-name lookup `0x5cb0a0`, stage filter `0x16baf44`, and
stage-key helpers `0x16f37d0` and `0x16f3a18` default to C++ on the
observed gather path. Provider storage `0x260bd50` and table binding
`0x1267360` also default to C++. The provider reaches the binding
through the runtime-loaded target in `0x11ac1e8`. The context dispatch
thunks forward to the scoped C++ implementations of `0x25ee660` and `0x25ee758`.

## Composed gameplay verification

The original and both C++ routes matched every captured neighborhood
and final decision in the whole-tile and half-tile fixtures. See
`shovelpathwhole1` and `shovelpathhalf1` in the ledger. Five decisions in
the first fixture accepted the interaction. Six decisions in the
second rejected it. The typed queries read live stage and provider
state through the boundary. Generated calls still handle synchronization
primitives and branches of descriptor metadata.

## Remaining generated dependencies

On the verified shovel path, context acquisition and release are typed
C++. The context-flag lookup `0x2601110` also uses C++. Its imported thread
resolver and virtual name refresh calls stay at the guest boundary.
Paired-handle acquisition `0x262e9bc` also defaults to C++. The live
fixture exercised acquire mode 1 with two kind-2 handles and the guard
disabled. Record selection `0x37567d0`, release `0x37568c0`, and
counter initialization `0x37e2e80` now default to C++ on this path.
The thread, lock, and wake imports remain at the guest boundary.

The alternate descriptor kind query `0x19f93e8` and the descriptor
predicate `0x19e59b0` default to C++ under the shovel neighborhood
metadata classifier. The alternate query retains the generated
special-kind callback `0x19f4c7c`.

The quadrant fallbacks `0x87a980` and `0x87aa70` now run in C++ on
the shovel survey route, including the field material predicate
`0x1285040`. The alternate terrain rectangle `0x87bf30` and its
two cached material predicates run in C++ at the verified caller.
The separate shovel survey alternate dispatcher `0x8773f0` and
its property check `0x880a10` still need gameplay coverage at their
survey call sites. The boundary also retains the auxiliary item
lookup `0x1a03610`, the extent helper `0x253fe7c`, and deeper
item descriptor and category callbacks.

## Host integration test

`acnh_shovel_field_query` runs without a guest image or save. It uses
the production typed interfaces for scratch preparation, neighborhood
gather, bounds checking, provider reads, catalog lookup, selected-item
classification, and terrain queries. The in-memory fixture includes a
continuation cell with an occupied anchor. It checks all 36 resolved
records, the stage key, and the occupancy bit for both metadata
outcomes. The gather runs the handwritten descriptor queries and
checks that the predicate resolves its cached column. The test also
checks selected-item suitability, material rejection, and blocking
by a half-tile layer flag. The host fixture supplies responses for
the remaining callbacks at the typed boundary.

```powershell
& private/venv/Scripts/ctest.exe --test-dir H:/ACNH/ACNHRecomp/private/port-acnh-engine/player -R '^acnh_shovel_field_query$' --output-on-failure
```

## Gameplay fixture for item classification

`shovelclassdeep2` starts from the disposable `whole` save. It
keeps the buried fossil at survey tile (40, 69) and adds red roses
(`0x0b77`) at (41, 70), a tile inside the surveyed neighborhood.
The four added item records are at layer 0 save coordinates (82, 108)
through (83, 109), including the three continuation records. The
[ledger](ledger.md#host-and-deeper-item-classification-checkpoint)
records the checks and next candidates.

## Height sampling

The shovel target selector's height dispatcher, ground and overlay
samplers, parameter accessors, and shared grid-height query now run in
handwritten C++ on the verified route. The typed grid query handles
sector selection, interpolation, and material outputs. Both samplers
also reuse the C++ world-position fallbacks and the field material
predicate. The height parameter object's type check also runs in
handwritten C++. Locking, stage-specific material overrides, and the
base-type fallback stay at the guest boundary. The selector also uses the
typed point classifier, which checks stage height and item metadata.
The classifier uses the C++ surface height, obstruction and
stage-specific height decisions. Stage selection and field-record
resolution and step geometry also run in typed C++. The provider
record lookup and its bounds check resolve the record and its
anchor half tile in C++. The three observed step cast modes and
both collision probes use typed C++ through their observed height,
stage item, and provider calls. Stage-name dispatch and the newer
stage filter call sites still use generated code. The traversal
fallback `0x15f10f0` reaches the C++ peak sampler, both C++
segment sweep modes, and their height chains in the side-strip
fixture. Both sweeps use the C++ stage-name, hash, and filter
functions and the typed destination record and bounds queries.
The outer traversal also calls typed C++ for five observed
surface sites and two stage-height sites. The sweep collision
query and 16-sector scan run in C++ for the observed calls with
separate outputs and no destination root. The traversal parent,
recursive traversal, its stage base and item resolver, and the
sweep collision's center and rim probe callbacks use generated
code.

[Traversal height verification](ledger.md#direct-heights-inside-outer-shovel-traversal)
[Sweep collision verification](ledger.md#sixteen-sector-sweep-collision)
