# Candidates for the next ACNH 3.0.3 batch

These ten entries are new to the porting ledger. Their names and data
layouts are provisional. The main NSO build ID is
`FF1D1C05670DB6021C85B624A710B963`. The previous ten-port batch is
complete. This queue records discovery and does not start another batch.

The NSO unwind table establishes each entry and size. Every listed call
site is a direct AArch64 BL to that entry. The disassembly has a return,
with no direct or indirect calls and no branch outside the function.
The observed entries come from an instrumented recompilation that loaded
a disposable save and ran the pinned-clock tree-shake and island-walk
scenario. The probe recorded up to 12 entry hits per function. Entry hits
show reachability. The next stage needs paired inputs and exits.

| Main entry | Size | Provisional function | Known direct call site | Stage |
| --- | ---: | --- | --- | --- |
| `0x24699b8` | `0x68` | Initialize working action bits once | `0x2373540` | Live entry observed |
| `0x1218220` | `0xe8` | Read a tile record's bit flag | `0x14c4a70` | Live entry observed |
| `0x1146470` | `0xfc` | Resolve a column using a 25-slot cache | `0x34ae584`, `0x23dfe74` | Live entry observed at the first site |
| `0x113d890` | `0x148` | Test a bit in a cached record column | `0x267ec84` | Live entry observed |
| `0x1632d80` | `0xf4` | Initialize four inline string buffers and their descriptors | `0x15fd478` | Live entry observed |
| `0x5bd550` | `0x104` | Hash a terminated byte string with a seed | `0x5bd7a4`, `0x15ded24` | Live entry observed at the first site |
| `0x112b8ec` | `0x3c` | Advance a four-word RNG and draw a value below 100 | `0x21bc410` | Live entry observed |
| `0x24642a8` | `0x68` | Set or clear a bit in an action mask | `0x24674d8` | Live trigger pending |
| `0x1281c54` | `0x15c` | Look up a half-tile record by coordinates | `0x21f787c` | Live trigger pending |
| `0x3db4ed0` | `0x4c` | Multiply a three-vector by a 3-by-3 matrix | `0x3dafc64` | Live trigger pending |

## Replay prerequisites

1. **`0x24699b8`** takes an object pointer in X0 and has no meaningful
   scalar return. Capture the source and destination mask words and the
   initialization byte at X0+0x28. Compare the initial write sequence and
   the already-initialized path.
2. **`0x1218220`** takes an X0 pointer with two 32-bit coordinates at
   offsets 8 and 12 and returns W0. Capture the singleton rooted at
   `main+0x509d040`, the selected primary or secondary grid descriptor,
   and the selected record or fallback byte. Cover both grids and the
   out-of-bounds path.
3. **`0x1146470`** takes an X0 row pointer, W1 slot and W2 hash and
   returns a pointer or null in X0. Resolve both cache pointers from
   `main+0x4abbc70` and `main+0x4abbc78`. Capture the row backlink,
   column records, initialization byte and 25-slot cache. Compare cold
   initialization, cache misses and ordered writes. Check whether the
   existing field-column helper can be reused with this cache layout.
4. **`0x113d890`** takes an X0 row pointer and W1 bit index and returns
   W0. It uses slot 22 of the same 25-slot cache. Capture the cache and
   the selected column byte. Preserve signed index arithmetic and test
   null rows and cold and warm cache paths.
5. **`0x1632d80`** takes writable object storage in X0 and has no
   meaningful scalar return. Capture the destination through offset
   `0x1ff` and the descriptor pointer loaded through `main+0x4ab2bc0`.
   Compare self-relative buffer pointers and the exact write footprint.
6. **`0x5bd550`** reads a NUL-terminated byte string through X0 and
   takes a W1 seed. It returns a 32-bit hash in W0 and writes no guest
   data. Capture a field-key call, then test lengths around four-byte
   boundaries, embedded terminators, and nonzero seeds. There are 9,107
   direct BL sites in the pinned main text, so measure integration cost
   before changing dispatch for this entry.
7. **`0x112b8ec`** has no explicit arguments and returns a value below
   100 in X0. Capture the four 32-bit words at `main+0x504cb58` before
   and after each call. Preserve ordered state writes. Establish
   repeatable seed state or defer the live comparison.
8. **`0x24642a8`** takes an X0 mask pointer, W1 bit index and W2 flag and
   has no meaningful scalar return. Capture both 32-bit words and
   exercise indices 31, 32, 54 and 55. Find an action that reaches a known
   caller before testing routing.
9. **`0x1281c54`** takes a table pointer in X0 and a coordinate record
   pointer in X1, and returns a record pointer or null in X0. Preserve
   the two input regions and the selected table backing store. Exercise
   negative half-tile conversion, both grids, and the fallback record.
   Start gameplay discovery at the shovel field survey.
10. **`0x3db4ed0`** takes an output vector in X0, a nine-float matrix
    in X1 and a three-float input in X2. It writes three floats and has
    no meaningful scalar return. Preserve buffer aliasing and the
    original fused operation order. First identify an action that
    reaches the caller at `main+0x3dafa70`.

## Evidence and reproduction

Run from the `port/acnh-engine` worktree with its existing Python venv.

```powershell
& private/venv/Scripts/python tools/acnh_version.py H:/ACNH/ACNHRecomp/private/game --get build_id
& private/venv/Scripts/python -m private.heading.identify_batch2
```

The local scanner checks unwind sizes, decodes every instruction in each
range, and enumerates direct calls in main. Its probe input is
`H:/ACNH/ACNHRecomp/private/port-acnh-engine/tree-shake-original-discovery1/console.log`,
SHA-256 `ed40829966597fa1a81db16b5d5ba3656bd3eae097a0a88ad4324f64bea628fd`.
The older ledger rows retain their original evidence and retry conditions.
