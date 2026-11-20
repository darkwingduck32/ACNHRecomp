# Record category predicates

These five leaves share a packed-record lookup and test distinct categories.
Each takes a guest row pointer in X0 and returns W0 as a zero-extended
boolean. Each returns true for category 2 or for its own category. None
writes guest memory or makes external calls. The main build is
`FF1D1C05670DB6021C85B624A710B963` and each unwind range is `0xa4` bytes.

| Entry | Categories accepted | Observed call site |
| --- | --- | --- |
| `0x23255e4` | 2 or 3 | `0x2431040` |
| `0x2325688` | 2 or 4 | `0x2430fbc` |
| `0x232572c` | 2 or 5 | `0x2430fe0` |
| `0x23257d0` | 2 or 6 | `0x2431000` |
| `0x2325874` | 2 or 7 | `0x2431020` |

The row's backlink gives its packed header. The leaf derives the index
using the record stride, then reads the category from a descriptor at
`main+0x5390120`. Each fixture includes the exact header, descriptor,
and category table memory that the leaf can reach.

## Verification

A constant-false native candidate failed against each of the five original
entry points. The failing fixtures all used the shared category 2. After
implementation, each entry matched its independent Unicorn reference on
163 synthetic cases, repeated from fresh state. The cases cover both
packed header layouts, the shared category, all distinct categories,
invalid metadata, and missing or out-of-range entries.

The original gameplay probe observed each entry at its own call site
during the same island scenario. The scenario starts from a disposable
save and uses an in-game walk, turn and tree shake. The trace captured 12 calls per entry on the original route. The reference replay retains each call's actual memory
addresses and compares the normalized inputs and results between routes.

The opt-in C++ run and the promoted default each captured 12 calls per
entry. The final 187-case comparison for each leaf includes 24 live
snapshots from the original and default routes. Each entry independently
matched both fresh Unicorn runs for every input.

The standard emitter matched all five leaf bodies and all three caller
bodies. The generated Windows module routes all 13 direct calls through
runtime dispatch. C++ is the default on 3.0.3.
`NX_ACNH_RECORD_CATEGORY=original` selects the original implementations.
`NX_ACNH_RECORD_CATEGORY_TRACE=1` traces the calls in the gameplay scenario.

For replay instructions and checks across all ten entries, see
[the batch verification](batch-verification.md).
