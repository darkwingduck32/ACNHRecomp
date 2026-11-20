# ACNH 3.0.3 function port ledger

The batch targets 10 verified gameplay functions. Five of ten functions are verified.

The `port/acnh-engine` branch is checked out at
`C:/Users/bettr/.codex/worktrees/bb9b/ACNHRecomp` and started from
`e15b0c3cd9f6aaaaecfced82b73c8d284ee6987f`. The original files are under
`H:/ACNH/ACNHRecomp/private/game`. Main NSO build ID
`FF1D1C05670DB6021C85B624A710B963` matches 3.0.3 in `games/acnh/versions.txt`.

| Module and entry | Provisional name | Stage | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `FF1D1C05670DB6021C85B624A710B963:0x5f0400` | Heading from horizontal direction | Verified | [Pilot](heading.md) | Continue with the next leaf. |
| `FF1D1C05670DB6021C85B624A710B963:0x5f05f8` | Shortest signed angular difference | Verified | [Evidence](angle-difference.md) | Continue with the next leaf. |
| `FF1D1C05670DB6021C85B624A710B963:0x3db4b60` | Step a position toward a target | Verified | [Evidence](approach-position.md) | Continue with the next leaf. |
| `FF1D1C05670DB6021C85B624A710B963:0x8807cc` | Resolve a field table row | Verified | [Evidence](field-record.md) | Continue with the next leaf. |
| `FF1D1C05670DB6021C85B624A710B963:0x8823a0` | Resolve a field table column | Verified | [Evidence](field-column.md) | Continue with the next leaf. |
| `FF1D1C05670DB6021C85B624A710B963:0x5f06dc` | Cosine of the shortest angular separation | Offline comparison | Exact unwind range 0x54. Relative relocation resolves the cosine table. 1,367 passing comparisons. | Observe the callers at `0x1655a34` and `0x1df2a0c` during gameplay. |
| `FF1D1C05670DB6021C85B624A710B963:0x1a03610` | Find index for auxiliary item type | Discovery | Exact unwind range of 0xec bytes. Called at `main+0x267d768`; reads a runtime table from the main image. | Obtain a live snapshot of the table before replay. |
| `FF1D1C05670DB6021C85B624A710B963:0x267d6c4` | Resolve field item ID | Discovery | Exact unwind range of 0xf0 bytes. Called at `main+0x21f76b0` and `main+0x23dfe18`; calls the preceding row on one branch. | Establish the leaf dependency and its capture prerequisites. |

Count a row after independent whole-function comparison, a matching gameplay run
that hits the replacement, passing relevant regressions, and a replay through
the promoted default. Keep the original selectable.
