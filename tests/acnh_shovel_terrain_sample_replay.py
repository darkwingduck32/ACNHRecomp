"""Replay the terrain record reader and its ground/overlay record combinations."""
import argparse
from collections import Counter
import hashlib
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x862530, 0x1CC
GRID, TILE, OUTPUT, GROUND, OVERLAY = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000


def fixture(tile, overlay, kinds):
    grid = bytearray(0x68)
    struct.pack_into("<I", grid, 0x38, 4)
    struct.pack_into("<I4xQI4xQ", grid, 0x48, 16, GROUND,
                     16 if overlay == "valid" else 0, 0 if overlay == "null" else OVERLAY)
    ground_rows = bytearray((i * 11 + i // 0x34 * 3) & 255 for i in range(16 * 0x34))
    overlay_rows = bytearray((i * 19 + i // 0x34 * 7) & 255 for i in range(16 * 0x34))
    for off in range(0x30, len(overlay_rows), 0x34):
        overlay_rows[off:off + 4] = bytes(kinds)
    return {
        GRID: bytes(grid),
        TILE: struct.pack("<Qii", 0, *tile),
        OUTPUT: bytes([0xa5] * 0x34),
        GROUND: bytes(ground_rows),
        OVERLAY: bytes(overlay_rows),
    }


TRACE = re.compile(r"acnh shovel terrain sample (original|cpp) (caller=(?:872114|848ed4) pos=-?\d+,-?\d+ out=[0-9a-f]{104})")


def live_cases(path, expected_route):
    raw = path.read_bytes()
    states = Counter()
    for route, state in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        states[state] += 1
    if not states:
        raise ValueError(f"{path}: no {expected_route} shovel terrain sample calls")
    print(f"{path}: {states.total()} terrain sample calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
        parser.error("unexpected terrain record reader boundary")
    if args.live:
        original = live_cases(args.live[0], "original")
        native = live_cases(args.live[1], "cpp")
        if original != native:
            raise SystemExit(f"live mismatch. Original-only {original - native}. C++ only {native - original}")
    cases = [(tile, fixture(tile, "valid", kinds))
             for tile, kinds in itertools.product(((0, 0), (2, 1), (3, 3), (-1, 1)),
                                                  itertools.product((0, 7, 9), repeat=4))]
    cases.extend((tile, fixture(tile, mode, (0, 7, 9, 3)))
                 for tile, mode in itertools.product(((0, 0), (3, 3)), ("null", "short")))
    lines = []
    for (x, z), memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{GRID} {x} {z} {OUTPUT} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for i, ((_, memory), line) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: GRID, UC_ARM64_REG_X1: TILE, UC_ARM64_REG_X2: OUTPUT}
        outputs = (UC_ARM64_REG_X0,)
        first = run(BASE + ENTRY, code, regs, outputs, memory)
        if first != run(BASE + ENTRY, code, regs, outputs, memory):
            raise SystemExit(f"unstable original terrain sample case {i}")
        reference = [first.registers[UC_ARM64_REG_X0], *(value for write in first.writes for value in write)]
        if list(map(int, line.split())) != reference:
            raise SystemExit(f"terrain sample case {i} differs in return value or ordered writes")
    print(f"{len(cases)} terrain sample cases match the ordered writes and return value of both AArch64 runs")


if __name__ == "__main__":
    main()
