"""Replay the stage-name array lookup and compare its live pointer displacement."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
ENTRY, SIZE = 0x5cb0a0, 0x14
TRACE = re.compile(r"acnh shovel stage name (original|cpp) base=([0-9a-f]+) index=([0-9a-f]+) out=([0-9a-f]+)")


def live_cases(path, expected):
    states = Counter()
    cases = []
    for route, base, index, out in TRACE.findall(path.read_text(errors="replace")):
        if route != expected:
            raise ValueError(f"{path}: expected {expected}, found {route}")
        base, index, out = (int(value, 16) for value in (base, index, out))
        states[(index, out - base)] += 1
        cases.append((base, index))
    if not cases:
        raise ValueError(f"{path}: no {expected} stage-name lookups")
    print(f"{path}: {states.total()} stage-name lookups")
    return states, cases


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
        parser.error("unexpected stage-name accessor boundary")
    cases = list(itertools.product((0x4000000000, 0x4400000000, 0x2900000000),
                                   (0, 1, 2, 1023, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff)))
    if args.live:
        stock, original = live_cases(args.live[0], "original")
        selected, native = live_cases(args.live[1], "cpp")
        if set(stock) != set(selected):
            raise SystemExit("live stage-name lookup displacement differs")
        cases.extend(original + native)
    lines = "".join(f"{base} {index}\n" for base, index in cases)
    native = subprocess.run([str(args.candidate.resolve())], input=lines, text=True, capture_output=True,
                            check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for (base, index), actual in zip(cases, native):
        regs = {UC_ARM64_REG_X0: base, UC_ARM64_REG_X1: 0x83ffff00000000 | index}
        outputs = (UC_ARM64_REG_X0,)
        original = run(base + ENTRY, code, regs, outputs, {})
        if original != run(base + ENTRY, code, regs, outputs, {}):
            raise SystemExit("unstable original stage-name replay")
        if original.registers[UC_ARM64_REG_X0] != int(actual) or original.writes:
            raise SystemExit(f"stage-name lookup differs for index {index:#x}")
    print(f"{len(cases)} independent cases match both original runs")


if __name__ == "__main__":
    main()
