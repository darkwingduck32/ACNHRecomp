# ACNH 3.0.3 function port ledger

Batch target: 10 verified gameplay functions. Verified: 0.

- Branch: `port/acnh-engine`
- Worktree: `C:/Users/bettr/.codex/worktrees/bb9b/ACNHRecomp`
- Kickoff revision: `e15b0c3cd9f6aaaaecfced82b73c8d284ee6987f`
- Original files: `H:/ACNH/ACNHRecomp/private/game`
- Main NSO build ID: `FF1D1C05670DB6021C85B624A710B963`, matching 3.0.3 in `games/acnh/versions.txt`.

| Module and entry | Provisional name | Stage | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `FF1D1C05670DB6021C85B624A710B963:0x5f0400` | Heading from horizontal direction | Offline comparison | [Pilot](heading.md) | Route the generated tree-shake call through runtime dispatch and capture paired entry/exit observations on a disposable save. |
| `FF1D1C05670DB6021C85B624A710B963:0x1a03610` | Find index for auxiliary item type | Discovery | Called at `main+0x267d768`; reads a runtime table from the main image. | Verify the unwind range and obtain a live snapshot of the table before replay. |
| `FF1D1C05670DB6021C85B624A710B963:0x267d6c4` | Resolve field item ID | Discovery | Called at `main+0x21f76b0` and `main+0x23dfe18`; calls the preceding row on one branch. | Establish the leaf dependency and its capture prerequisites. |

Count a row after independent whole-function comparison, a matching gameplay run
that hits the replacement, passing relevant regressions, and a replay through
the promoted default. Keep the original selectable.
