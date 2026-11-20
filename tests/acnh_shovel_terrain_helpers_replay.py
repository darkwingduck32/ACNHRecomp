"""Independently replay the terrain rectangle and quadrant material queries."""
import argparse
from collections import Counter
import hashlib
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X4, UC_ARM64_REG_X5,
)

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
GRID, RECT, POINT, GROUND, OVERLAY, FLAGS, OWNER, HANDLE, OUTPUT, TAP = (
    0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000, 0x26000, 0x27000, 0x28000, 0x2a000
)
READ_SAMPLE = 0x862530
ENTRIES = ((0x871f80, 0x21c), (0x848e68, 0x3e4))
RET = 0xd65f03c0


def fixture(mode, lo, hi):
    grid = bytearray(0x78)
    layout = (4, 4, 16, GROUND, 16 if mode & 4 else 0, OVERLAY if mode & 4 else 0, 16, FLAGS)
    struct.pack_into("<II8xI4xQI4xQI4xQ", grid, 0x38, *layout)
    owner = bytearray(0xb0)
    struct.pack_into("<Q", owner, 0xa8, GRID)
    handle = bytearray(0x88)
    struct.pack_into("<Q", handle, 0x80, GRID)
    ground = bytearray(16 * 0x34)
    overlay = bytearray(16 * 0x34)
    for record in range(16):
        height = (0.0, record * 0.09, record * 0.125, float("nan") if record % 3 == 0 else 0.0)[mode >> 3]
        offs = record * 0x34
        struct.pack_into("<f", ground, offs, height)
        struct.pack_into("<f", overlay, offs, height * 0.75)
        ground[offs + 0x30:offs + 0x34] = bytes((1, 2, 3, 4))
        overlay[offs + 0x30:offs + 0x34] = bytes((7, 5, 7, 6))
    flags = [3] * 16
    if mode & 2:
        flags[5] = 0
    return {
        BASE + 0x4ab2010: struct.pack("<Q", 0x90000),
        BASE + 0x4bfae30: struct.pack("<Q", OWNER if mode & 4 else 0),
        GRID: bytes(grid),
        RECT: struct.pack("<QQiiQii", 0, 0, *lo, 0, *hi),
        POINT: struct.pack("<Qii", 0, *lo),
        GROUND: bytes(ground),
        OVERLAY: bytes(overlay),
        FLAGS: struct.pack("<16I", *flags),
        OWNER: bytes(owner),
        HANDLE: bytes(handle),
        OUTPUT: bytes([0xa5] * 16),
        TAP: bytes(8),
    }


def kind_stub(value):
    return struct.pack("<8I", 0xd294000a, 0xf2a0004a, 0xb9400008, 0xb9000148, 0xb9400808,
                       0xb9000548, 0x52800000 | value << 5, RET)


def reference(module, number, mode, memory):
    entry, size = ENTRIES[number]
    code = bytes(module.image[entry:entry + size])
    sample = bytes(module.image[READ_SAMPLE:READ_SAMPLE + 0x1cc])
    if number == 0:
        regs = {UC_ARM64_REG_X0: GRID, UC_ARM64_REG_X1: RECT, UC_ARM64_REG_X2: mode & 1}
        callees = {BASE + 0x872110: (BASE + READ_SAMPLE, sample)}
    else:
        regs = {UC_ARM64_REG_X0: HANDLE, UC_ARM64_REG_X1: POINT, UC_ARM64_REG_X2: OUTPUT,
                UC_ARM64_REG_X3: OUTPUT + 4, UC_ARM64_REG_X4: OUTPUT + 8, UC_ARM64_REG_X5: OUTPUT + 12}
        callees = {BASE + 0x848ed0: (BASE + READ_SAMPLE, sample)}
        for site in (0x848fe4, 0x849040, 0x849098, 0x8490f0):
            callees[BASE + site] = (BASE + 0x87a980, kind_stub(7 if mode & 1 else 8))
        for site in (0x849000, 0x84905c, 0x8490b4, 0x84910c):
            callees[BASE + site] = (BASE + 0x87aa70, kind_stub(7 if mode & 2 else 9))
    outputs = (UC_ARM64_REG_X0,)
    first = run(BASE + entry, code, regs, outputs, memory, callees=callees)
    second = run(BASE + entry, code, regs, outputs, memory, callees=callees)
    if first != second:
        raise ValueError(f"unstable original helper at {entry:#x}")
    return [first.registers[UC_ARM64_REG_X0], len(first.calls),
            *(target - BASE for _, target, _ in first.calls),
            *(value for write in first.writes for value in write)]


LIVE = {
    "level": re.compile(r"acnh shovel terrain helper level (original|cpp) (area=-?\d+,-?\d+,-?\d+,-?\d+ mode=[01] out=[01])"),
    "materials": re.compile(r"acnh shovel terrain helper materials (original|cpp) (pos=-?\d+,-?\d+ kinds=[0-9a-f,]+ out=[01])"),
}


def live_cases(path, expected_route):
    raw = path.read_bytes()
    text = raw.decode(errors="replace")
    result = {}
    for name, pattern in LIVE.items():
        states = Counter()
        for route, state in pattern.findall(text):
            if route != expected_route:
                raise ValueError(f"{path}: expected {expected_route}, found {route}")
            states[state] += 1
        if not states:
            raise ValueError(f"{path}: no {expected_route} terrain {name} calls")
        result[name] = states
    print(f"{path}: {result['level'].total()} rectangle checks, {result['materials'].total()} material checks, "
          f"SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
        parser.error("unexpected terrain helper boundary")
    if args.live:
        original = live_cases(args.live[0], "original")
        native = live_cases(args.live[1], "cpp")
        for name in LIVE:
            if original[name] != native[name]:
                raise SystemExit(f"{name} live mismatch. Original-only {original[name] - native[name]}. "
                                 f"C++ only {native[name] - original[name]}")
    geometry = (((0, 0), (3, 3)), ((1, 1), (2, 3)), ((-2, -1), (0, 2)), ((0, 1), (0, 0)),
                ((2, 2), (1, 3)), ((3, 3), (5, 5)), ((8, 8), (8, 8)))
    cases = [(number, mode, lo, hi, fixture(mode, lo, hi))
             for number, mode, (lo, hi) in itertools.product(range(2), range(32), geometry)]
    lines = []
    for number, mode, lo, hi, memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{number} {mode} {lo[0]} {lo[1]} {hi[0]} {hi[1]} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts = [0, 0]
    for (number, mode, lo, hi, memory), line in zip(cases, native):
        expected = reference(module, number, mode, memory)
        actual = list(map(int, line.split()))
        if actual != expected:
            mismatch = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b),
                            min(len(actual), len(expected)))
            raise SystemExit(f"{ENTRIES[number][0]:#x} mode {mode}, bounds {(lo, hi)}, index {mismatch}: "
                             f"original {expected[mismatch:mismatch+8]}, C++ {actual[mismatch:mismatch+8]}")
        counts[number] += 1
    for (entry, _), count in zip(ENTRIES, counts):
        print(f"{entry:#x}: {count} independent cases match both original runs")


if __name__ == "__main__":
    main()
