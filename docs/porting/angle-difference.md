# Signed angular difference

Main build `FF1D1C05670DB6021C85B624A710B963`, entry `0x5f05f8`, unwind
range `0xe4` bytes. S0 and S1 are angles in degrees. X0 points to an output
float that the function always writes. W0 returns a direction of -1, 0, or
1 and is zero-extended into X0. There are no globals or external calls.

The leaf has three direct callers at `0x1e6abbc`, `0x2360070`, and
`0x246419c`. The third caller ran during the island walk and turn scenario.
The other two callers' generated bodies were checked against the new hook
profile, and their live behavior has no coverage in this scenario.

## Evidence

Unicorn independently ran both synthetic and captured inputs twice from
fresh state. The comparison checks X0, the exact bits of the output float,
and all four written guest bytes. The constant-zero candidate failed at
case 1. Inputs `ff7fffff d4800000` yielded expected direction
`ffffffff` and expected distance word `ff7fffff`.

The three generated caller shards were rebuilt from the previous full
3.0.3 emission. The four dispatched calls match the standard emitter's
output, including both bodies of the speed variant where applicable. All
three gameplay runs reached `main+0x246419c` 224 times with 29 unique call
inputs. The opt-in and default routes matched the original. The original
and default screenshots showed the same position and direction after the
walk. All 1,237 offline cases passed, including each live input. All 13
registered CTest tests and eight replay/profile unit tests passed.

The original, opt-in C++, and default capture log SHA-256 digests were

- `650cfc9ad7b8ed8c700c2361ecb098a9ac60e33eb98b269fb0215f8099f062e3`
- `79e07b9c61a5a513fdad5feaf6b29aba30ea7a4c80f8e4eedf6d4462d978fcca`
- `ed6fa6073109194c48902fc6a52589774fa5667acaefb258fd8864a4bf3acf38`

C++ is the default for finite arguments on 3.0.3. The switch
`NX_ACNH_ANGLE_DIFFERENCE=original` selects the original and non-finite
inputs use it automatically. The separate trace switch is
`NX_ACNH_ANGLE_DIFFERENCE_TRACE=1`.

## Replay

From this worktree with the local Python environment and x64 clang-cl
developer shell used for the heading pilot:

```powershell
clang-cl /nologo /std:c++20 /O2 /fp:precise /Foprivate/heading/angle_difference.obj /Feprivate/heading/angle_difference.exe tests/acnh_angle_difference_candidate.cpp
$root = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$env:PORT_HEADING_LIBRARY = "$root/generated-dll/angle_generated.dll"
$env:PORT_TRACE_ANGLE = '1'
& private/venv/Scripts/python -m private.heading.try_tree_shake angle1 original cpp default
& private/venv/Scripts/python -m tests.acnh_angle_difference_replay H:/ACNH/ACNHRecomp/private/game/exefs/main private/heading/angle_difference.exe --live "$root/tree-shake-original-angle1/nx.log" "$root/tree-shake-default-angle1/nx.log"
```
