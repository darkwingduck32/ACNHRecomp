# Pilot heading function

`main+0x5f0400` through `main+0x5f05f8` (exclusive), confirmed by the
3.0.3 main NSO unwind table. The tree-shake approach path calls it directly at
`main+0x23e03d8`. The input is two 32-bit floats in S0 and S1. The result is
a 32-bit turn in W0, zero-extended into X0. The inputs do not alias memory.
The function reads a 129-entry, 8-byte-per-entry table at `main+0x409a7a8`.
It does not write guest data or call externally. Compare X0 exactly. The
C++ candidate currently accepts finite inputs. The live adapter needs to
retain original behavior for other inputs.

## Offline evidence

The independently executing reference is Unicorn 2.1.4 running the original
AArch64 bytes relocated to the guest main base. The test supplies the table
from the pinned main NSO to each side. It covers axes, octant boundaries,
signed zero, float extrema, interpolation boundaries, and seeded samples.
The input and table payload SHA-256 is
`58dc8bda4a6c9c48f0c6f1d7e58ec2e43866ebe27df536d93cac410add21aae1`.
All 3,416 synthetic comparisons passed, with each original run repeated
from a fresh fixture. The deliberately constant-zero candidate failed on
case 0 (`s0=ff7fffff s1=ff7fffff`), with expected `0xa0000000` and observed
`0x00000000`.

Run from the campaign worktree in an x64 Visual Studio developer shell with
the repository's clang-cl on PATH:

```powershell
python -m venv private/venv
& private/venv/Scripts/python -m pip install unicorn==2.1.4 capstone==5.0.9 lz4==4.4.5
New-Item -ItemType Directory -Force private/heading
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/candidate.obj /Feprivate/heading/candidate.exe tests/acnh_heading_candidate.cpp
& private/venv/Scripts/python -m unittest tests.function_replay_test tests.hook_profile_test
& private/venv/Scripts/python -m tests.acnh_heading_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/candidate.exe
```

## Remaining work

The generated caller currently invokes the generated function directly.
The next step is to test routing explicitly selected calls through the
runtime function table and wrap the pinned entry there, keeping the original
function available. Then drive the same tree-shake scenario with both paths,
record actual inputs and outcomes under ignored `private/`, replay the
captured inputs, and run the relevant build and regressions. No live
captures, runtime switch, or promotion exist yet.
