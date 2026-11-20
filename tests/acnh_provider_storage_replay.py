"""Replay the live provider storage accessor against the pinned AArch64 body."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x260bd50, 8
TRACE = re.compile(
    r"acnh shovel provider storage (original|cpp) caller=([0-9a-f]+) binding=([0-9a-f]+) "
    r"input=([0-9a-f]+) out=([0-9a-f]+)"
)


def live_cases(path, route):
    states = Counter()
    samples = []
    for observed, caller, obj, initial, result in TRACE.findall(path.read_text(errors="replace")):
        if observed != route or initial != result:
            raise ValueError(f"{path}: provider storage did not return the pointer it received")
        states[(caller, int(initial, 16) != 0)] += 1
        samples.append((int(obj, 16), int(initial, 16)))
    if not states:
        raise ValueError(f"{path}: no {route} provider storage calls")
    print(f"{path}: {states.total()} provider storage calls")
    return states, samples


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
        parser.error("unexpected provider storage bounds")
    cases = list(itertools.product((0x20000, 0x29ff0, 0x6300000080),
                                   (0, 1, 0x46000003d0, 0xffffffffffffffff)))
    if args.live:
        original, stock = live_cases(args.live[0], "original")
        native, selected = live_cases(args.live[1], "cpp")
        if original != native:
            raise SystemExit("provider storage call sites differ")
        cases.extend(stock + selected)
    lines = "".join(f"{obj} {field}\n" for obj, field in cases)
    native = subprocess.run([str(args.candidate.resolve())], input=lines, text=True, capture_output=True,
                            check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for (obj, field), actual in zip(cases, native):
        regs = {UC_ARM64_REG_X0: obj}
        outputs = (UC_ARM64_REG_X0,)
        memory = {obj + 0x20: struct.pack("<Q", field)}
        result = run(BASE + ENTRY, code, regs, outputs, memory)
        if result != run(BASE + ENTRY, code, regs, outputs, memory):
            raise SystemExit("unstable stock provider storage access")
        if result.registers[UC_ARM64_REG_X0] != int(actual) or result.writes:
            raise SystemExit(f"provider storage differs at binding {obj:#x}")
    print(f"{len(cases)} independent cases match both original runs")


if __name__ == "__main__":
    main()
