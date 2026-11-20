"""Replay the original grid-height routine from the shipped AArch64 module."""
import argparse
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0
from recomp import nso, ehframe
from tests.function_replay import run
from tests.acnh_shovel_target_replay import compare_live
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x862700, 0x8d8
GRID, POINT, MATERIAL, OVERRIDE, FLAGS, GROUND, OVERLAY = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000, 0x26000

TRACE = re.compile(r"acnh shovel height (original|cpp) entry=(862700) "
                   r"same_input=(?:stock|match) point=[0-9a-f,]+ (call=.* out=[0-9a-f]+)")


def compare_grid_live(paths, tile):
    if paths:
        for path in paths:
            if "entry=862700 same_input=mismatch" in path.read_text(errors="replace"):
                raise SystemExit(f"{path}: grid height differs on the same input")
        compare_live(paths, tuple(tile) if tile else None, (ENTRY,), TRACE)


def fixture(point, mode, variant, slopes, initial_material=0xa1b2c3d4):
    grid = bytearray(0x78)
    struct.pack_into("<f", grid, 0x28, (4.0, 5.0, 7.0)[variant % 3])
    struct.pack_into("<f", grid, 0x2c, (2.5 if slopes else 0.0))
    struct.pack_into("<II", grid, 0x38, 2, 2)
    struct.pack_into("<I4xQI4xQI4xQ", grid, 0x48, 4, GROUND, 4 if mode != 0 else 0,
                     OVERLAY if mode != 0 else 0, 4, FLAGS)
    bits = (0, 1 << 16, 1 << 17, (1 << 16) | (1 << 17))[variant & 3]
    tables = {}
    for offset, start in enumerate((GROUND, OVERLAY)):
        records = bytearray()
        for cell in range(4):
            floats = [value for q in range(4) for value in (
                1.5 + q + cell + offset*2, 13.5 - cell + q if slopes else float(q % 2),
                -7.25 + q if slopes else float((q+1)%2))]
            kinds = (1,2,3,4) if offset == 0 else ((7,9,5,6) if mode == 1 else (8,6,9,7))
            records.extend(struct.pack("<12f4B", *floats, *kinds))
        tables[start] = bytes(records)
    return {GRID: bytes(grid), POINT: struct.pack("<3f", point[0], 0, point[1]),
            MATERIAL: struct.pack("<I", initial_material), OVERRIDE: b"\xEE", FLAGS: struct.pack("<4I", *([bits]*4)),
            **tables}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--worker", action="store_true",
                        help="Use the worker's ground-only mode and initial materials 5 and 7.")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected grid-height boundary")
    compare_grid_live(args.live, args.survey_tile)
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    points = ((-0.2, 1), (1, -0.2), (50, 50), (0, 0), (1, 0.5), (0.5, 1), (2, 2),
              (4, 1), (1, 4), (4, 4), (4.999, 4.999), (5, 5), (7.5, 6), (9.9, 9.99))
    cases = []
    ground_modes = (1,) if args.worker else (0, 1)
    materials = (5, 7) if args.worker else (0xa1b2c3d4,)
    for point, mode, variant, ground_only, slopes, material in itertools.product(
            points, range(3), range(4), ground_modes, (0, 1), materials):
        memory = fixture(point, mode, variant, slopes, material)
        cases.append((point, mode, variant, ground_only, slopes, material, memory))
    payload = []
    for _, _, _, ground_only, _, _, memory in cases:
        pairs = [(a+i, byte) for a,data in memory.items() for i,byte in enumerate(data)]
        payload.append(" ".join(map(str, [ground_only, len(pairs), *(v for pair in pairs for v in pair)])))
    results = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload)+"\n",
                             capture_output=True, text=True, check=True, timeout=45).stdout.splitlines()
    if len(results) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    for (point, mode, variant, ground_only, slopes, material, memory), line in zip(cases, results):
        regs = {UC_ARM64_REG_X0: GRID, UC_ARM64_REG_X1: POINT, UC_ARM64_REG_X2: MATERIAL,
                UC_ARM64_REG_X3: ground_only, UC_ARM64_REG_X4: OVERRIDE}
        first = run(BASE+ENTRY, code, regs, (UC_ARM64_REG_S0,), memory)
        if first != run(BASE+ENTRY, code, regs, (UC_ARM64_REG_S0,), memory):
            raise SystemExit("unstable original grid-height case")
        expected = [first.registers[UC_ARM64_REG_S0], int.from_bytes(first.memory[MATERIAL], "little"),
                    first.memory[OVERRIDE][0], *(value for write in first.writes for value in write)]
        actual = list(map(int, line.split()))
        if actual != expected:
            raise SystemExit(f"{point=}, {mode=}, {variant=}, {ground_only=}, {slopes=}, {material=}: "
                             f"original {expected}; C++ {actual}")
    print(f"{ENTRY:#x}: {len(cases)} cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
