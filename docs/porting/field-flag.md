# Check a field flag around a point

Main build `FF1D1C05670DB6021C85B624A710B963`, entry `0x15e0e30`, unwind
range `0x1e4`. X0 points to coordinates, S0 carries the radius, and W1
selects between eight and sixteen samples. The leaf tests a bit in the
central cell and then around the circumference. The input map, angle
steps and the lookup table are read-only. It returns a boolean in W0.

The constant-false candidate failed for the marked central cell. The
replacement then passed all 517 synthetic comparisons with two independent
original runs for each input. The player passed 13 Release CTest tests.
The generated module routed all 11 direct calls through runtime dispatch.

The original tree-shake trial ran through the scenario with zero hits.
The original remains the default. Retry with a field interaction known to
reach one of the callers. The private trace is
`tree-shake-original-flag1/nx.log` under the campaign directory.
`NX_ACNH_FIELD_FLAG_TRACE=1` records inputs and reachable memory when a
caller is hit. The unverified C++ route is opt-in with
`NX_ACNH_FIELD_FLAG=cpp`.

```powershell
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/field_flag.obj /Feprivate/heading/field_flag.exe tests/acnh_field_flag_candidate.cpp
& private/venv/Scripts/python -m tests.acnh_field_flag_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/field_flag.exe
```
