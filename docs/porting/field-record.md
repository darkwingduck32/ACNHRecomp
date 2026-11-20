# Resolve a field table row

Main build `FF1D1C05670DB6021C85B624A710B963`, entry `0x8807cc`, unwind
range `0xa0` bytes. X0 is a guest database pointer and W1 is the row index.
X0 returns a guest row pointer or null. The original reads a prepared
pointer list or a packed table through its registry and uses 32-bit
wrapping arithmetic for the packed row offset. It writes no guest
memory and makes no external calls.

The tree shake reaches this leaf through the field probe at
`main+0x15e026c`. The live capture records all reachable scalar memory
reads. The replay preserves each capture's original guest addresses and
pointer relationships. A normalized pointer graph compares the same
scenario across runs even when allocations differ.

## Evidence

The zero-return candidate failed for the prepared pointer test at case 0.
Unicorn later agreed with C++ on all 336 synthetic cases, including missing
registries, absent rows, invalid counts, disabled flags, overflow and both
packed table formats. Each reference run was repeated from a fresh copy.

The standard emitter's two checked callers and the leaf match the
incrementally generated module. All 107 direct call sites use dispatch.
Original, opt-in C++, and default gameplay each reached the field row
lookup 15 times across three normalized states, and performed the tree
shake and fruit drop. These captures exercised prepared row pointers.
The packed table route is covered by synthetic replay. Both 366-case
strict comparisons included all 30 exact live call snapshots for the
respective pair. All 13 registered Windows Release CTest tests passed.

The original, opt-in C++, and default capture log SHA-256 digests were

- `53e2d845aea86c307b4bd6e5def7ff3c7250e75f9561c99871ed0a5462bbd6a4`
- `84a14e7ad6becb790aadb2711e2dcfd083b2b80b9109917b15dd951a04dd8f17`
- `72e1af990924f2ceb06c500c5d19a53ff4a35e6b4223e3b61d15accc187bbd2c`

## Replay

The default is C++ on 3.0.3. Select the original with
`NX_ACNH_FIELD_RECORD=original`. The trace switch is
`NX_ACNH_FIELD_RECORD_TRACE=1`. The scenario driver takes an unused
suffix when creating new disposable roots.

```powershell
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/field_record.obj /Feprivate/heading/field_record.exe tests/acnh_field_record_candidate.cpp
$root = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$env:PORT_HEADING_LIBRARY = "$root/generated-dll/field_generated.dll"
$env:PORT_TRACE_FIELD_RECORD = '1'
& private/venv/Scripts/python -m private.heading.try_tree_shake field1 original cpp default
& private/venv/Scripts/python -m tests.acnh_field_record_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/field_record.exe --live "$root/tree-shake-original-field1/nx.log" "$root/tree-shake-default-field1/nx.log"
```
