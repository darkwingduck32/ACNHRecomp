# Final Windows 3.0.3 batch replay

The batch used the local main NSO with build ID
`FF1D1C05670DB6021C85B624A710B963`. Original and default runs used
the same tree-shake and walk commands, the same pinned game clock, and
fresh copies of the same saved game. The original run selected the
original implementation of all ten functions. The default run left
the route switches unset and hit all ten C++ implementations.

| Entry | Calls captured per route | Offline and captured cases compared |
| --- | ---: | ---: |
| `0x5f0400` | 30 | 3,418 |
| `0x5f05f8` | 224 | 1,237 |
| `0x3db4b60` | 30 | 998 |
| `0x8807cc` | 90 | 516 |
| `0x8823a0` | 90 | 501 |
| `0x23255e4` | 12 | 187 |
| `0x2325688` | 12 | 187 |
| `0x232572c` | 12 | 187 |
| `0x23257d0` | 12 | 187 |
| `0x2325874` | 12 | 187 |

All 7,605 comparisons passed against two independent Unicorn runs
from the same fixture. The Release player build passed 13 registered
CTest tests. The replay runner, dispatch and hook profile tests passed
all nine checks. The code review found no critical or important issues.
The final predicate comparison checks neighboring records with distinct
categories, descriptor bounds, and paired call counts.

The live game is the instrumented recompilation. Trace mode for the
memory-based field and category routines collects read-only C++ snapshots
before invoking the original path. Unicorn independently executes the
original AArch64 bytes using those captured addresses and values. Original
mode with tracing disabled invokes the original bodies directly. Coverage
is for this Windows build and the observed gameplay scenarios. The per-leaf
evidence files record specific caller and branch gaps.

## Local artifacts

The root is `H:/ACNH/ACNHRecomp/private/port-acnh-engine`.

| Artifact | SHA-256 |
| --- | --- |
| `player/acnh.exe` | `aec3b7a09d880c5f7051ce525f65d3a4fadc309e6c6f959316b2bc70d7678125` |
| `generated-dll/category_generated.dll` | `759e189dc6da7cae28834c53c0e7df033b93554faa29f71b0ce5bf8cb77f80e4` |
| `tree-shake-original-batch1/nx.log` | `378282fdb4c0622c16ae19063d445b00e1abfb310b525c68f30ed0924ed44de3` |
| `tree-shake-default-batch1/nx.log` | `8b031559c8cefe420d7fa9cedeb10bfe83771ea27c7000d96d003248cbadb139` |

## Reproduce

Run from the campaign worktree with its local venv. For new captures,
substitute an unused suffix in both commands.

```powershell
$root = 'H:/ACNH/ACNHRecomp/private/port-acnh-engine'
$env:PORT_HEADING_LIBRARY = "$root/generated-dll/category_generated.dll"
$env:PORT_VERIFY_BATCH = '1'
& private/venv/Scripts/python -m private.heading.try_tree_shake batch1 default original
& private/venv/Scripts/python -m private.heading.verify_batch batch1
& private/venv/Scripts/ctest --test-dir "$root/player" --output-on-failure
```

Set the following route switches to `original` to select any individual
original implementation or the group of five category predicates.

- `NX_ACNH_HEADING`
- `NX_ACNH_ANGLE_DIFFERENCE`
- `NX_ACNH_APPROACH`
- `NX_ACNH_FIELD_RECORD`
- `NX_ACNH_FIELD_COLUMN`
- `NX_ACNH_RECORD_CATEGORY`
