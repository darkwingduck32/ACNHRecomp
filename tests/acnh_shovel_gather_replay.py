"""Compare typed survey orchestration with the original neighborhood gather."""
import argparse
import collections
import hashlib
import itertools
import re
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_shovel_gather_reference import BASE, BUILD, ENTRY, SCRATCH, SIZE, fixture
from tests.function_replay import run
from tools.acnh_version import build_id


TRACE = re.compile(
    r"acnh shovel gather (original|cpp) caller=21f7618 base=[0-9a-f]+ scratch=[0-9a-f]+ "
    r"before=([0-9a-f]{752}) after=([0-9a-f]{752})"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    states = collections.Counter()
    for route, before, after in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        before, after = bytes.fromhex(before), bytes.fromhex(after)
        if before[:0x18] != after[:0x18] or before[0x1c:0x30] != after[0x1c:0x30] or before[0x150:] != after[0x150:]:
            raise ValueError(f"{path}: gather changed bytes outside the mask and record table")
        bounds = before[0x28:0x30] + before[0x160:0x168] + before[0x170:0x178]
        states[(bounds, before[0x18:0x1c], before[0x30:0x150], after[0x18:0x1c], after[0x30:0x150])] += 1
    if not states:
        raise ValueError(f"{path}: no {expected_route} shovel gather calls")
    print(f"{path}: {states.total()} full shovel gathers, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected neighborhood gather boundary")
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("original and C++ gather inputs or outputs differ")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    modes = ("absent", "actor", "metadata-match", "metadata-miss", "extent-match", "extent-miss", "anchor-miss", "empty", "stage-match", "stage-other")
    cases = [(idx, *fixture(x, z, mode)) for (x, z), (idx, mode) in itertools.product(
        ((-4, -3), (0, 0), (40, 69)), enumerate(modes)
    )]

    lines = []
    for mode, memory, _ in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{BASE} {SCRATCH} {mode} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")

    for i, (mode, memory, callees) in enumerate(cases):
        regs = {UC_ARM64_REG_X0: SCRATCH}
        first = run(BASE + ENTRY, code, regs, (), memory, callees=callees)
        if first != run(BASE + ENTRY, code, regs, (), memory, callees=callees):
            raise SystemExit(f"unstable original neighborhood case {i}")
        expected = [len(first.calls), *(target - BASE for _, target, _ in first.calls)]
        expected.append(next(x0 for _, target, x0 in first.calls if target == BASE + 0x16baf44))
        expected.extend(value for write in first.writes for value in write)
        actual = list(map(int, native[i].split()))
        if actual != expected:
            mismatch = next((j for j, (a, b) in enumerate(zip(actual, expected)) if a != b),
                            min(len(actual), len(expected)))
            raise SystemExit(f"case {i} ({modes[mode]}), index {mismatch}: "
                             f"expected {expected[mismatch:mismatch+6]}, got {actual[mismatch:mismatch+6]}")
    print(f"{len(cases)} cases match two original replays with exact call and write order")


if __name__ == "__main__":
    main()
