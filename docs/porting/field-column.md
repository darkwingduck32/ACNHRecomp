# Resolve a field table column

Main build `FF1D1C05670DB6021C85B624A710B963`, entry `0x8823a0`, unwind
range `0xec` bytes. X0 is a guest row pointer, W1 is the column slot, and
W2 is the field hash. X0 returns a field pointer or null. The function
reads or updates the 60-byte shared cache at main+0x4bfab80. No external
calls occur.

The reference runner records the address, width and value of each memory
write, as well as the final contents. The test driver compares the
pointer and full cache bytes. It also compares initialization store order,
the missing-column marker, and the eventual field offset, including
multiple writes to the same cache bytes. All 321 synthetic cases passed
twice. A constant-null candidate failed at the first cold-cache case.

The field probe called the original, opt-in C++, and default routes
15 times each across three normalized live states during the tree shake.
All calls used a warm cache. Both pairs passed 351 comparisons, including
30 exact live snapshots and repeated original execution. The leaf and
two checked callers matched the standard emitter, and all 107 generated
calls route through dispatch. The Release player passed 13 CTest tests
and nine replay and profile tests passed. Cold initialization and misses
have synthetic coverage. Concurrent cold-cache calls have no live coverage.

The original, opt-in C++, and default capture log SHA-256 digests were

- `9e18193427fe5d3108af717046f9e59bdb197644597c896c48b779785dec2cc6`
- `5d894772edd42a3a9b05a8472ebc8208dd52d1551f542c3a8f650700d17eed50`
- `36d971953f1b2704b38ae13d9c5ae5973cb7116de6ba9a9a96077ec6feb37bda`

## Replay

C++ is the default on 3.0.3 and `NX_ACNH_FIELD_COLUMN=original` selects
the original. `NX_ACNH_FIELD_COLUMN_TRACE=1` logs the live inputs and
cache changes. Use an unused run suffix to create fresh disposable roots.

```powershell
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/field_column.obj /Feprivate/heading/field_column.exe tests/acnh_field_column_candidate.cpp
$root = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$env:PORT_HEADING_LIBRARY = "$root/generated-dll/column_generated.dll"
$env:PORT_TRACE_FIELD_COLUMN = '1'
& private/venv/Scripts/python -m private.heading.try_tree_shake column1 original cpp default
& private/venv/Scripts/python -m tests.acnh_field_column_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/field_column.exe --live "$root/tree-shake-original-column1/nx.log" "$root/tree-shake-default-column1/nx.log"
```
