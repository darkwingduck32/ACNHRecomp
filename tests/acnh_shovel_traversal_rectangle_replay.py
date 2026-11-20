"""Compare the traversal rectangle and original terrain sampler with the typed query."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import random
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SAMPLE = 0x8721a0, 0x862530
GRID, RECT, GROUND, OVERLAY = 0x21000, 0x22000, 0x23000, 0x24000
POINTS = (((0, 0, 0),) * 4,) * 9
TRACE = re.compile(
    r"acnh shovel traversal rectangle (original|cpp) entry=8721a0 (call=([0-9a-f]+)"
    r" bounds=(-?\d+,-?\d+,-?\d+,-?\d+) grid=(\d+),(\d+) threshold=([0-9a-f]{8})"
    r" count=(\d+),(\d+) ground=([0-9a-f/:]*) overlay=([0-9a-f/:]*)"
    r" same_input=match out=([01]))"
)
SITES = {
    0x15f13fc, 0x15f1424, 0x15f1470, 0x15f1498, 0x15f1500, 0x15f1528, 0x15f1570, 0x15f1598,
    0x15f19e0, 0x15f1a08, 0x15f1a54, 0x15f1a7c, 0x15f1aec, 0x15f1b14, 0x15f1b60, 0x15f1b88,
}


def rows(height, kinds):
    return b"".join(struct.pack("<12f4B", *(v for triangle in cell for v in triangle), *kind)
                    for cell, kind in zip(height, kinds, strict=True))


def fixture(bounds, threshold, ground, overlay, overlay_kinds):
    grid = bytearray(0x68)
    struct.pack_into("<f4xII", grid, 0x30, threshold, 3, 3)
    struct.pack_into("<I4xQ", grid, 0x48, 9, GROUND)
    descriptor = bytearray(0x28)
    struct.pack_into("<ii", descriptor, 0x10, *bounds[:2])
    struct.pack_into("<ii", descriptor, 0x20, *bounds[2:])
    memory = {BASE + 0x4ab2010: struct.pack("<Q", 0x25000),
              RECT: bytes(descriptor), GROUND: rows(ground, [(5,) * 4] * 9)}
    if overlay is not None:
        struct.pack_into("<I4xQ", grid, 0x58, 9, OVERLAY)
        memory[OVERLAY] = rows(overlay, overlay_kinds)
    memory[GRID] = bytes(grid)
    return memory


def alter(cell, corner, triangle):
    points = [list(tile) for tile in POINTS]
    points[cell][corner] = triangle
    return points


def fixtures():
    for bounds, mode in product((
        (0, 0, 2, 2), (1, 1, 1, 1), (0, 0, 2, 0), (0, 0, 0, 2),
        (-1, -1, 1, 1), (2, 2, 3, 3), (3, 3, 4, 4), (1, 1, 2, 0), (2, 0, 1, 0),
    ), (0, 1)):
        yield bounds, mode, fixture(bounds, 0.25, POINTS, None, ())
    for corner, value, mode in product(range(4), (-2., -1.25, -.125, 0., .125, 1.25, 2., float("nan")), (0, 1)):
        bounds = (1, 1, 1, 1)
        ground = alter(4, corner, (0., 0., value))
        yield bounds, mode, fixture(bounds, .25, ground, None, ())
        bounds = (0, 0, 2, 2)
        yield bounds, mode, fixture(bounds, .25, ground, None, ())
    for neighbor, corner, value, kind, mode in product(
        (1, 3, 7, 5), range(4), (.5, 2.), (7, 9), (0, 1)
    ):
        bounds = (0, 0, 2, 2)
        overlay = alter(neighbor, corner, (0., 0., value))
        kinds = [[7] * 4 for _ in POINTS]
        kinds[neighbor][corner] = kind
        yield bounds, mode, fixture(bounds, .25, POINTS, overlay, kinds)
    randomizer = random.Random(0x8721a0)
    heights = (-1.25, -.25, -0., 0., .25, 1.25, float("nan"), float("inf"))
    for bounds, threshold, mode in product(((0, 0, 2, 0), (0, 1, 0, 2), (0, 0, 2, 2)),
                                            (-0., .25, 1.25, float("nan")), (0, 1)):
        for _ in range(12):
            ground = [[[randomizer.choice(heights) for _ in range(3)] for _ in range(4)] for _ in POINTS]
            overlay = [[[randomizer.choice(heights) for _ in range(3)] for _ in range(4)] for _ in POINTS]
            kinds = [[randomizer.choice((7, 9)) for _ in range(4)] for _ in POINTS]
            yield bounds, mode, fixture(bounds, threshold, ground, overlay, kinds)


def read_trace(path, route):
    records = []
    with path.open(errors="replace") as log:
        for row in log:
            if not row.startswith("acnh shovel traversal rectangle "):
                continue
            match = TRACE.fullmatch(row.rstrip())
            if not match or match[1] != route:
                raise ValueError(f"{path}: incomplete trace or parity mismatch: {row.rstrip()}")
            records.append(match)
    seen = {int(match[3], 16) for match in records}
    if seen != SITES:
        raise ValueError(f"{path}: reached rectangle sites {sorted(map(hex, seen))}")
    return records


def captured_cases(path):
    seen = set()
    for match in read_trace(path, "original"):
        _, _, _, raw_bounds, width, depth, threshold, ground_count, overlay_count, ground, overlay, out = match.groups()
        observed = match.groups()[3:]
        if observed in seen:
            continue
        seen.add(observed)
        bounds = tuple(map(int, raw_bounds.split(",")))
        grid = bytearray(0x68)
        struct.pack_into("<I4xII", grid, 0x30, int(threshold, 16), int(width), int(depth))
        descriptor = bytearray(0x28)
        struct.pack_into("<ii", descriptor, 0x10, *bounds[:2])
        struct.pack_into("<ii", descriptor, 0x20, *bounds[2:])
        memory = {BASE + 0x4ab2010: struct.pack("<Q", 0x25000), RECT: bytes(descriptor)}
        for offset, table, count, data in ((0x48, 0x50000000, ground_count, ground),
                                           (0x58, 0x60000000, overlay_count, overlay)):
            struct.pack_into("<I4xQ", grid, offset, int(count), table if data else 0)
            for token in filter(None, data.split("/")):
                cell, raw_row = token.split(":")
                terrain = bytes.fromhex(raw_row)
                if len(terrain) != 0x34:
                    raise ValueError(f"invalid terrain row {token}")
                memory[table + int(cell) * 0x34] = terrain
        memory[GRID] = bytes(grid)
        yield (bounds, 0, memory), int(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path, help="Replay rectangle inputs recorded in original gameplay")
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if args.live:
        original, cpp = (read_trace(path, route) for path, route in zip(args.live, ("original", "cpp"), strict=True))
        if [match[2] for match in original] != [match[2] for match in cpp]:
            raise SystemExit(f"Rectangle state or order differs ({len(original)} original, {len(cpp)} C++)")
        print(f"Matched {len(cpp)} ordered gameplay rectangle records")
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    ranges = {fn.start: fn.size for fn in ehframe.parse(module)}
    if (ranges.get(ENTRY), ranges.get(SAMPLE)) != (0x75c, 0x1cc):
        parser.error("unexpected range for rectangle or sampler")
    body = bytes(module.image[ENTRY:ENTRY + ranges[ENTRY]])
    sample = bytes(module.image[SAMPLE:SAMPLE + ranges[SAMPLE]])
    callees = {BASE + 0x872308: (BASE + SAMPLE, sample)}
    rows, expected = [], []
    cases = [(case, None) for case in fixtures()]
    if args.capture:
        captures = list(captured_cases(args.capture))
        cases.extend(captures)
        print(f"Replaying {len(captures)} distinct gameplay inputs")
    for (bounds, mode, memory), observed in cases:
        registers = {UC_ARM64_REG_X0: GRID, UC_ARM64_REG_X1: RECT, UC_ARM64_REG_X2: mode}
        original = run(BASE + ENTRY, body, registers, (UC_ARM64_REG_X0,), memory, callees=callees)
        if original.writes or original != run(BASE + ENTRY, body, registers, (UC_ARM64_REG_X0,), memory, callees=callees):
            raise ValueError("unstable original or caller-visible write")
        if observed is not None and original.registers[UC_ARM64_REG_X0] != observed:
            raise ValueError(f"original replay disagrees with captured result for {bounds}")
        expected.append(original.registers[UC_ARM64_REG_X0])
        values = [GRID, *bounds, mode, sum(map(len, memory.values()))]
        values.extend(word for address, data in memory.items()
                      for offset, byte in enumerate(data) for word in (address + offset, byte))
        rows.append(" ".join(map(str, values)))
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(rows) + "\n",
                            text=True, capture_output=True, check=True, timeout=30)
    actual = list(map(int, output.stdout.splitlines()))
    if actual != expected:
        diffs = [(i, before, after) for i, (before, after) in enumerate(zip(expected, actual)) if before != after]
        raise ValueError(f"{len(expected)} original / {len(actual)} C++; first differences: {diffs[:20]}")
    print(f"{len(actual)} matched traversal rectangle cases twice with original counts {dict(Counter(expected))}")


if __name__ == "__main__":
    main()
