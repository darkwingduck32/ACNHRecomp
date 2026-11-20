"""Replay the shovel terrain dispatcher with explicit typed dependency stand-ins."""
import argparse
from collections import Counter
import hashlib
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
BASE, ENTRY, SIZE = 0x4000000000, 0x877130, 0x2BC
TILE, OWNER_SLOT, OWNER, GRID = 0x20000, 0x21000, 0x22000, 0x23000
FLAGS, TABLE_SLOT, TABLE, LAYER_TAP = 0x24000, 0x25000, 0x26000, 0x27000
RET = 0xD65F03C0


def fixture(mode, x, z):
    owner = bytearray(0xb0)
    struct.pack_into("<Q", owner, 0xa8, 0 if mode == 2 else GRID)
    grid = bytearray(0x78)
    struct.pack_into("<II", grid, 0x38, 8, 8)
    struct.pack_into("<I4xQ", grid, 0x68, 64, FLAGS)
    memory = {
        BASE + 0x4abc1a0: struct.pack("<Q", 0x90000),
        BASE + 0x4ab2010: struct.pack("<Q", 0x91000),
        BASE + 0x4ab27c8: struct.pack("<Q", OWNER_SLOT),
        BASE + 0x4ab46c8: struct.pack("<Q", TABLE_SLOT),
        TILE: struct.pack("<Qii", 0, x, z),
        OWNER_SLOT: struct.pack("<Q", 0 if mode == 1 else OWNER),
        OWNER: bytes(owner),
        GRID: bytes(grid),
        FLAGS: struct.pack("<64I", *([4 if mode == 4 else 0] * 64)),
        TABLE_SLOT: struct.pack("<Q", TABLE),
        TABLE: bytes(4),
        LAYER_TAP: bytes(8),
    }
    return memory


def words(*instructions):
    return struct.pack(f"<{len(instructions)}I", *instructions)


def write_w(register, value, offset=0):
    return 0xb9000000 | (offset // 4 << 10) | (register << 5) | value


def read_w(register, offset):
    return 0xb9400008 | (offset // 4 << 10) | (register << 5)


def immediate(register, value):
    return 0x52800000 | (value << 5) | register


def dependencies(mode):
    grid = [op for i, offset in enumerate((0x10, 0x14, 0x20, 0x24))
            for op in (read_w(1, offset), write_w(0, 8, 4 * i))]
    quadrants = [op for i, value in enumerate((0x31, 0x32, 0x33, 0x34))
                 for op in (immediate(8, value), write_w(i + 2, 8))]
    return {
        BASE + 0x877254: (BASE + 0x871f80, words(*grid, immediate(0, int(mode != 3)), RET)),
        BASE + 0x87732c: (BASE + 0x848e68, words(
            read_w(1, 8), write_w(0, 8), read_w(1, 12), write_w(0, 8, 4),
            *quadrants, immediate(0, int(mode != 5)), RET)),
        **{BASE + site: (BASE + 0x880870, words(write_w(0, 1), immediate(0, int(mode != 6)), RET))
           for site in (0x87733c, 0x87734c, 0x87735c, 0x87736c)},
        BASE + 0x87737c: (BASE + 0x1285fe0, words(
            0xd28e000a, 0xf2a0004a, read_w(0, 8), write_w(10, 8), read_w(0, 12), write_w(10, 8, 4),
            immediate(0, int(mode == 7)), RET)),
    }


def expected(result):
    output = [result.registers[UC_ARM64_REG_X0]]
    writes = iter(result.writes)
    sizes = {0x871f80: 4, 0x848e68: 2, 0x880870: 1, 0x1285fe0: 2}
    for _, target, owner in result.calls:
        entry = target - BASE
        output.extend((entry, 0 if entry == 0x1285fe0 else owner))
        for _ in range(sizes[entry]):
            _, size, value = next(writes)
            assert size == 4
            output.append(value)
    assert next(writes, None) is None
    return output


TRACE = re.compile(r"acnh shovel terrain dispatch (original|cpp) (pos=-?\d+,-?\d+ out=[01])")


def live_cases(path, expected_route):
    raw = path.read_bytes()
    states = Counter()
    for route, state in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        states[state] += 1
    if not states:
        raise ValueError(f"{path}: no {expected_route} shovel terrain dispatch calls")
    print(f"{path}: {states.total()} terrain dispatch calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
        parser.error("unexpected terrain dispatcher boundary")
    if args.live:
        original = live_cases(args.live[0], "original")
        native = live_cases(args.live[1], "cpp")
        if original != native:
            raise SystemExit(f"live mismatch. Original-only {original - native}. C++ only {native - original}")
    cases = tuple(itertools.product(range(8), ((0, 0), (2, 3), (-2, -1), (40, 69), (2147483647, -2147483648))))
    lines = []
    for mode, (x, z) in cases:
        memory = fixture(mode, x, z)
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{BASE} {x} {z} {mode} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for (mode, (x, z)), line in zip(cases, native):
        regs = {UC_ARM64_REG_X0: TILE}
        outputs = (UC_ARM64_REG_X0,)
        memory = fixture(mode, x, z)
        first = run(BASE + ENTRY, code, regs, outputs, memory, callees=dependencies(mode))
        if first != run(BASE + ENTRY, code, regs, outputs, memory, callees=dependencies(mode)):
            raise SystemExit(f"unstable original terrain case {mode} at {(x, z)}")
        reference = expected(first)
        actual = list(map(int, line.split()))
        if actual != reference:
            mismatch = next((i for i, (a, b) in enumerate(zip(actual, reference)) if a != b),
                            min(len(actual), len(reference)))
            raise SystemExit(f"terrain case {mode} at {(x, z)}, index {mismatch}: "
                             f"original {reference[mismatch:mismatch + 8]}, C++ {actual[mismatch:mismatch + 8]}")
    print(f"{len(cases)} terrain dispatcher cases match the result, call order and arguments of two original runs")


if __name__ == "__main__":
    main()
