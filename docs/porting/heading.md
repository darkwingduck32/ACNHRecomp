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

## Runtime route and live validation

The recompiler reads the opt-in dispatch target from the 3.0.3 hooks
profile. Calls to it now go through the runtime dispatch table. The installed
adapter defaults to C++ for finite inputs on 3.0.3. Select the original with
`NX_ACNH_HEADING=original`. Non-finite inputs use the original. Use
`NX_ACNH_HEADING_TRACE=1` to log input and output bits at the tree-shake call.

The standard emitter produced the expected dispatch at the actual caller:

```powershell
& private/venv/Scripts/python -m recomp H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/emitted --name main --jobs 2 --only 5f0400 23e02d0 --hooks games/acnh/hooks-3.0.3.txt
```

The isolated player build and all 13 registered CTest tests passed. The
campaign branch carries the standalone shader build guard already present
on the integration branch. The initial all-target build failed without it.

The private live scripts are under `private/heading/` in the campaign
worktree. They build a DLL from the cached 3.0.3 objects with the two
tree-shake call sites (base and speed variant) routed through dispatch.
This exercises the same call in gameplay before the full generated rebuild.
The player build, probe DLL, and disposable saves/logs live under
`H:/ACNH/ACNHRecomp/private/port-acnh-engine/`.

The first unpinned-clock trials did not reach the tree. A frame capture
showed the title screen and then the daily announcement. Trial 4 pins the
game clock to 2026-11-12 12:16 local time, the time of the prior successful
scenario, and copies the same disposable save for each run. Its original
route reached the heading entry five times with one unique input.

Both trial 4 routes made five calls with the same single input. The player
screenshots show three apples on the ground after the shake in each run.
The capture logs are in `tree-shake-original-trial4/nx.log` and
`tree-shake-cpp-trial4/nx.log` under the campaign output directory. Their
SHA-256 digests are, respectively,
`f05311b5f707a3bae6c47a08f9d785c6653b7450e6e5fe4f7f1ed3543f5b3208` and
`a34af3f3eb1d84912e5c7b3e1bbddf07ad393ac01779f2f7c5cc6419d249cc61`.
Adding these logs with `--live` gave 3,417 matching comparisons, including
the captured call, each with two independent runs of the reference.

Full emission with the same 3.0.3 hook profile and the locally prepared
3.0.3 60 FPS patch produced 1,380 files and zero unhandled instructions.
The generated module build uses the cached SDK and no-change main objects.
The local driver `private.heading.build_generated_dll` checks the generated
header and compares each main source file before reusing its object.

The disposable save manifest for trial 4 covers 11 files. Its SHA-256 is
`82edb9109060b0b6d57ddb8393087f732987fc42b2ca66959f073b18c39a743b`,
calculated from each relative path followed by a NUL and that file's binary
SHA-256, in path order. The DLL linked from full emission is
`generated-dll/heading_generated.dll`, SHA-256
`13737e6e19b06637d3190581eb7fa572eb825fe110be6d6702dfb9796ae79577`.
The 925 generated call sites all use runtime dispatch.

## Verification on the generated build

The generated DLL linked after rebuilding 176 changed units and reusing
1,206 byte-identical ones. The build still uses the cached SDK and subsdk0
modules. Verification applies to the local Windows 3.0.3 build.

From the campaign worktree, rerun the scenario on disposable copies with
the following commands. The default run sets the trace switch and leaves
the route switch unset.

```powershell
$env:PORT_HEADING_LIBRARY = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine/generated-dll/heading_generated.dll'
& private/venv/Scripts/python -m private.heading.try_tree_shake full1 default original
$root = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
& private/venv/Scripts/python -m tests.acnh_heading_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/candidate.exe --live "$root/tree-shake-original-full1/nx.log" "$root/tree-shake-default-full1/nx.log"
```

Both runs reached the expected route five times with one unique input,
and the result matched Unicorn exactly. Both result screenshots show
three apples dropped. The 3,417 cases passed against two fresh reference
runs per case. The isolated Release player passed all 13 registered CTest
tests and all eight replay and profile unit tests passed. The original
and default trace log SHA-256 values were
`86b38e67ac989978818eaeabeb4e44a580c7acae2241c6fa50c80b1b3e100400` and
`bf5689cf970978ec71f72ceb45f3bf4268d933b387243fdfe379b24beddf0d92`.
