"""Independently replay continuation anchor setup and cleanup against AArch64."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X8

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRIES = ((0x11e4578, 0xc0), (0x2540184, 0x50))
PROVIDER, POINT, READER_VTABLE, TABLE, TABLE_VTABLE, OUTPUT = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x26000
CONTEXT_HANDLE, READ_CONTEXT, RELEASE_CONTEXT, OTHER_CONTEXT, CONTEXT_VTABLE, RELEASE_VTABLE = (
    0x27000, 0x28000, 0x29000, 0x2a000, 0x2b000, 0x2c000
)
TRACE = re.compile(r"acnh shovel anchor (original|cpp) entry=([0-9a-f]+) (state=[^\r\n]+)")


def fixture(mode, pos, release, ordinary):
    reader = struct.pack("<40xQ", READER_VTABLE)
    table = struct.pack("<Q32xQ", TABLE_VTABLE, CONTEXT_HANDLE if mode > 1 else 0)
    handle = struct.pack("<QQ8xQ", READ_CONTEXT if mode > 3 else 0, RELEASE_CONTEXT if mode > 2 else 0,
                         OTHER_CONTEXT if mode > 2 else 0)
    output = struct.pack("<QQBB2xII", 0xaaaa, TABLE, ordinary, release, 0x11111111, 0x22222222)
    return {
        BASE + 0x4ab4c28: struct.pack("<Q", 0x80000),
        PROVIDER: reader,
        POINT: struct.pack("<QII", 0, *pos),
        READER_VTABLE: struct.pack("<Q", BASE + 0x120000),
        TABLE: table,
        TABLE_VTABLE: struct.pack("<24xQQ", BASE + 0x120020, BASE + 0x120040),
        OUTPUT: output,
        CONTEXT_HANDLE: handle,
        READ_CONTEXT: struct.pack("<Q", CONTEXT_VTABLE),
        RELEASE_CONTEXT: struct.pack("<Q", RELEASE_VTABLE),
        OTHER_CONTEXT: struct.pack("<Q", RELEASE_VTABLE),
        CONTEXT_VTABLE: struct.pack("<Q", BASE + 0x120060),
        RELEASE_VTABLE: struct.pack("<Q", BASE + 0x120080),
    }


def dependencies(index, mode):
    if index == 1:
        return {BASE + 0x25401cc: (BASE + 0x120080, words(RETURN))}
    return {
        BASE + 0x11e459c: (BASE + 0x120000, words(*address_words(0, TABLE), RETURN)),
        BASE + 0x11e45ac: (BASE + 0x120020, words(0x52800100, RETURN)),
        BASE + 0x11e45c0: (BASE + 0x120040, words(0x52800080, RETURN)),
        BASE + 0x11e45ec: (BASE + 0x120060, words(0x52800000 | (mode % 2 << 5), RETURN)),
    }


def live_cases(path, expected_route):
    result = {entry: Counter() for entry, _ in ENTRIES}
    for route, entry, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        result[int(entry, 16)][state] += 1
    if not all(result.values()):
        raise ValueError(f"{path}: no {expected_route} calls at {[hex(e) for e, hits in result.items() if not hits]}")
    print(f"{path}: {[(hex(entry), states.total()) for entry, states in result.items()]}")
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
        parser.error("unexpected provider anchor boundaries")
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("provider anchor traces differ")
    cases = [(index, mode, pos, fixture(mode, pos, release, ordinary))
             for index, mode, (pos, release, ordinary) in itertools.chain(
                 ((0, mode, (pos, False, True)) for mode, pos in itertools.product(
                     range(6), ((0, 0), (7, 3), (8, 4), (0xffffffff, 1)))),
                 ((1, mode, ((0, 0), release, ordinary)) for mode, release, ordinary in itertools.product(
                     range(6), (False, True), (False, True))),
             )]
    lines = []
    for index, mode, (x, z), memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{index} {mode} {PROVIDER} {x} {z} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                            capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    count = [0, 0]
    for i, ((index, mode, _, memory), line) in enumerate(zip(cases, native)):
        entry, size = ENTRIES[index]
        code = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: OUTPUT} if index == 1 else {
            UC_ARM64_REG_X0: PROVIDER, UC_ARM64_REG_X1: POINT, UC_ARM64_REG_X8: OUTPUT}
        first = run(BASE + entry, code, regs, (), memory, callees=dependencies(index, mode))
        if first != run(BASE + entry, code, regs, (), memory, callees=dependencies(index, mode)):
            raise SystemExit(f"unstable original anchor at {entry:#x}, case {i}")
        expected = [len(first.calls)]
        for _, target, x0 in first.calls:
            expected.extend((target - BASE, x0))
        expected.extend(value for write in first.writes for value in write)
        actual = list(map(int, line.split()))
        if actual != expected:
            mismatch = next((j for j, (a, b) in enumerate(zip(actual, expected)) if a != b),
                            min(len(actual), len(expected)))
            raise SystemExit(f"{entry:#x}, case {i}, mode {mode}, index {mismatch}: "
                             f"original {expected[mismatch:mismatch+8]}, C++ {actual[mismatch:mismatch+8]}")
        count[index] += 1
    for (entry, _), cases in zip(ENTRIES, count):
        print(f"{entry:#x}: {cases} independent cases match both AArch64 runs")


if __name__ == "__main__":
    main()
