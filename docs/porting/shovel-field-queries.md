# Shovel field queries on ACNH 3.0.3

The active implementation path starts at the field survey called from
`main+0x21f58dc`. Its entry is `main+0x21f6fe0`, unwind size `0xc44`.
This path covers field queries during shovel targeting and classification.
The action engine continues to own item removal, inventory, and hole creation.

| Survey call site | Target | Field-query responsibility | State |
| --- | --- | --- | --- |
| `0x21f7568` to `0x21f75a4` | `0x3f04450`, `0x127c524`, `0x127c6ec` | Prepare the survey scratch data | `0x127c6ec` verified C++ default. Other preparation is generated. |
| `0x21f7614` | `0x127cab0` | Gather the neighborhood records and occupancy flags | Generated, with the string hash already in C++ |
| `0x21f76b0` | `0x267d6c4` | Resolve an item ID | Verified C++ default with original auxiliary fallback `0x1a03610` |
| `0x21f7788` | `0x877130` | Test the selected terrain | Generated, with `0x880870` and `0x1285fe0` in C++ by default |
| `0x21f77ac` through `0x21f7944` | `0x127d05c` | Classify field item and terrain properties | Generated, with `0x113d890` already in C++ |
| `0x21f787c` | `0x1281c54` | Find the field record at a half-tile position | Verified C++ default through typed field queries |
| `0x21f790c`, `0x21f7bb8` | `0x8773f0` | Check the alternate terrain condition | Generated, with leaf `0x880a10` |

The shovel survey hook at `main+0x21f7618` consumes the generated
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
| `0x127cb2c` | Virtual stage lookup | Generated |
| `0x127cb38` | Hash the stage key | Verified C++ default |
| `0x127cb68` | `0x16baf44` stage predicate | Generated |
| `0x127cc84`, `0x127cf84`, `0x127cfd0`, `0x127cff8` | `0x195033c` field-cell lookup | Generated |
| Five calls from `0x127ccb8` through `0x127ce50` | `0x267d6c4` item-ID resolution | Verified C++ default |
| `0x127cd94`, `0x127cea0` | `0x19ee3b4`, `0x253fe7c` field predicates | Generated |

The field-cell lookup is the next common dependency. It checks layer
and channel bounds through `0x194f798`, initializes an empty item through
`0x267d4ac`, and invokes a provider through `0x11e4f58`. The provider uses
virtual dispatch. The lookup returns the item into the caller's X8
result buffer. Its X3 argument carries the resolved position. The typed bounds check is the verified default. The field-cell candidate
moves provider selection and result handling behind the typed interface.
The runtime boundary maintains the original routes and invokes the
generated initializer and provider. The opt-in gameplay trial compares the
result item and resolved position from all four gather call sites.
