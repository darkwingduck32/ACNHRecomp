"""Replay each of the shovel survey's stage predicates on stock AArch64."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import random
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRIES = ((0x16f37d0, 0x248), (0x16f3a18, 0x164), (0x16baf44, 0x620))
TRACE = re.compile(r"acnh shovel stage (original|cpp) entry=([0-9a-f]+) (key=[0-9a-f]+ out=[01])")


def input_keys(code):
    mask = (1 << 32) - 1
    literals = set()
    previous = None
    for word, in struct.iter_unpack("<I", code):
        if word & 0xffe00000 == 0x52800000:
            previous = word & 31, (word >> 5) & 0xffff
        elif word & 0xffe00000 == 0x72a00000 and previous and previous[0] == word & 31:
            literals.add(previous[1] | (((word >> 5) & 0xffff) << 16))
            previous = None
        else:
            previous = None
    rng = random.Random(0)
    return sorted({0, 1, mask, mask >> 1, 1 << 31, *(rng.getrandbits(32) for _ in range(48)),
                   *((value + delta) & mask for value, delta in itertools.product(literals, (-1, 0, 1)))})


def live_cases(path, expected_route):
    result = {entry: Counter() for entry, _ in ENTRIES}
    for route, entry, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        result[int(entry, 16)][state] += 1
    if not all(result.values()):
        raise ValueError(f"{path}: missing stage calls at {[hex(e) for e, hits in result.items() if not hits]}")
    print(f"{path}: {[(hex(entry), hits.total()) for entry, hits in result.items()]}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    sizes = {f.start: f.size for f in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected stage predicate bounds")
    bodies = [bytes(module.image[entry:entry + size]) for entry, size in ENTRIES]
    live = live_cases(args.live[0], "original") if args.live else None
    if live and live != live_cases(args.live[1], "cpp"):
        raise SystemExit("original and C++ stage calls differ")
    observed = {int(text.split(" ")[0].split("=")[1], 16) for hits in (live or {}).values() for text in hits}
    cases = [(index, mode, value)
             for index, body in enumerate(bodies)
             for mode, value in itertools.product(range(4) if index == 2 else (0,),
                                                   sorted(set(input_keys(body)) | observed))]
    # Also exercise the stage dispatcher's use of both real predicates.
    cases.extend((2, 3, value) for body in bodies[:2] for value in input_keys(body))
    lines = "".join(f"{index} {mode} {key}\n" for index, mode, key in cases)
    native = subprocess.run([str(args.candidate.resolve())], input=lines, text=True, capture_output=True,
                            check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts = [0] * len(ENTRIES)
    for (index, mode, key), line in zip(cases, native):
        entry, _ = ENTRIES[index]
        callees = {}
        if index == 2:
            stub = lambda value: struct.pack("<II", 0x52800000 | int(value) << 5, 0xd65f03c0)
            for i, site in enumerate((0x16baf54, 0x16bb50c)):
                callees[BASE + site] = (BASE + ENTRIES[i][0], bodies[i] if mode == 3 else stub(mode == i + 1))
        regs = {UC_ARM64_REG_X0: key}
        outputs = (UC_ARM64_REG_X0,)
        first = run(BASE + entry, bodies[index], regs, outputs, {}, callees=callees)
        if first != run(BASE + entry, bodies[index], regs, outputs, {}, callees=callees):
            raise SystemExit(f"unstable original stage result at {entry:#x}, key {key:#x}")
        expected = [first.registers[UC_ARM64_REG_X0]]
        for _, target, arg in first.calls:
            expected.extend((target - BASE, arg & 0xffffffff))
        if list(map(int, line.split())) != expected:
            raise SystemExit(f"stage result differs at {entry:#x}, key {key:#x}, mode {mode}")
        counts[index] += 1
    for (entry, _), count in zip(ENTRIES, counts):
        print(f"{entry:#x}: {count} independent cases match both original runs")


if __name__ == "__main__":
    main()
