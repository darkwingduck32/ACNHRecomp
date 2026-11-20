# Move a position toward its target

Main build `FF1D1C05670DB6021C85B624A710B963`, entry `0x3db4b60`. The unwind
range is `0x100` bytes. X0 points to three writable floats and X1 points to
three target coordinates. S0, S1 and S2 supply the gain and step bounds.
S0 returns the distance remaining. There are no globals or external calls.

C++ runs by default with disjoint positions and finite inputs of magnitude
at most `2^20`. Overlapping regions and values outside that range use
the original. Select the original explicitly with
`NX_ACNH_APPROACH=original`. Set `NX_ACNH_APPROACH_TRACE=1` to log the
tree-shake call's inputs and outputs.

## Evidence

Unicorn executed each case twice from fresh state. Tests compare S0,
all 12 writable bytes, the unchanged target, and the exact footprint of
writes. Synthetic tests exercise interpolation, snapping, equal positions,
clamps, and data near the bounds. A constant-result candidate failed
case 0 with zero position and distance where the reference moved the
position and returned a nonzero distance.

The tree-shake caller enters the leaf at `main+0x23e0590` with disjoint
position buffers. Its original, opt-in C++, and default routes each logged
five calls and two unique inputs. One input moved to the target and the
other was already there. Both original and default runs dropped three
apples. All 996 comparisons, including both captures, passed twice.
All 13 registered Release CTest tests and the eight replay/profile tests
passed. Other gameplay callers and the original fallback cases have no
live capture in this scenario.

The incremental module routed all 390 generated calls through dispatch.
The full tree-shake caller matched the standard emitter in both generated
speed-variant bodies and the wrapper. The original, opt-in C++, and
default capture log SHA-256 digests were

- `accfea64a67ad61f4d7fea71dcb91b727af7baa91872849cc31c23d3fe13551c`
- `520c924403d64ba5f35ced2daa8ab4b4c5ec8dfa8d611f80237d21fc99947717`
- `2ca6745d5ff88775c4a8fbe686c055d65ad9c5d4b3f19c9d4fd49d7f7ac6ba2c`

## Replay

Use the same x64 clang-cl developer shell and Python environment as the
heading pilot. Pick an unused run suffix when generating new logs.

```powershell
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/approach.obj /Feprivate/heading/approach.exe tests/acnh_approach_candidate.cpp
$root = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$env:PORT_HEADING_LIBRARY = "$root/generated-dll/approach_generated.dll"
$env:PORT_TRACE_APPROACH = '1'
& private/venv/Scripts/python -m private.heading.try_tree_shake approach1 original cpp default
& private/venv/Scripts/python -m tests.acnh_approach_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/approach.exe --live "$root/tree-shake-original-approach1/nx.log" "$root/tree-shake-default-approach1/nx.log"
```
