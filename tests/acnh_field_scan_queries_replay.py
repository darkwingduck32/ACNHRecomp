"""Replay the six field scan helpers observed under the same parent."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
OBJECT, TILE, HEADER, VALUES, OWNER, NODE, GRID, ROWS = (
    0x22000, 0x24000, 0x26000, 0x28000, 0x2a000, 0x2c000, 0x2e000, 0x30000,
)
ENTRIES = (
    (0x19d6a30, 0xbc), (0x127d59c, 0x70), (0x1596154, 0x8c),
    (0x19d9fd0, 0xa4), (0x19f49bc, 0xc0), (0x1217cc0, 0xe4),
)


def type_cases(mode):
    for kind, selected, extended, broken in product((0, 1, 0x7f, 0xbc, 0xbd, 0xffff),
                                                    (0, 0x7f, 0xbc), (False, True),
                                                    ("none", "null", "row_offset", "count", "stride", "columns", "table")):
        row = HEADER + (0x1c if extended else 0xc) + 8 + 8
        header = bytearray(0x40)
        struct.pack_into("<IIHB", header, 0, 0 if broken == "count" else 2, 0 if broken == "stride" else 8,
                         0 if broken == "columns" else 1, extended)
        struct.pack_into("<I", header, row - HEADER, 0 if broken == "row_offset" else row - HEADER)
        yield mode, OBJECT, selected, {OBJECT + 0x10: struct.pack("<Q", 0 if broken == "null" else row),
                                       HEADER: bytes(header), VALUES: struct.pack("<2H", 9, kind),
                                       BASE + 0x5226ec0: struct.pack("<I4xQ", 1 if broken == "table" else 2, VALUES)}


def tile_cases():
    coords = (-2147483648, -16777217, -5, -4, -1, 0, 1, 4, 5, 16777217, 2147483647)
    for x, z, alias in product(coords, coords, (False, True)):
        memory = {BASE + 0x4ab2018: struct.pack("<Q", OWNER), OBJECT: struct.pack("<Qii", 0, x, z)}
        arg = OBJECT if alias else TILE
        if not alias:
            memory[TILE] = struct.pack("<Qii", 0, x, z)
        yield 1, OBJECT, arg, memory


def visited_cases():
    for x, z, fill in product((-1, 0, 1, 2, 31, 63, 64, 511, 512), (-1, 0, 1, 2, 253, 255, 256, 257),
                              (0, 0x55443322, 0xffffffff)):
        bounds = struct.pack("<4i2i", 0, 0, 0, 0, 511, 256)
        yield 2, OBJECT, TILE, {BASE + 0x5133970: bounds, TILE: struct.pack("<Qii", 0, x, z),
                                OBJECT: bytes(8) + struct.pack("<I", fill) * 1024}


def class_cases():
    for layer, grid_ok, secondary, count, coords in product(
        (0, 1, 56, 57, 0xffffffff), (False, True), (False, True), (1, 6),
        ((-3, 7), (-2, 7), (-1, 8), (9, -6), (8, -7), (0, 0)),
    ):
        grid = bytearray(0xb0)
        struct.pack_into("<II", grid, 0x18, 2, 2)
        struct.pack_into("<ii", grid, 0x28, -3, 7)
        struct.pack_into("<I4xQ", grid, 0x30, count, ROWS)
        struct.pack_into("<B3xI", grid, 0x88, secondary, 4)
        struct.pack_into("<iiII", grid, 0x98, 8, -7, 2, 2)
        records = bytearray(8 * 0x68)
        for i, value in enumerate((0, 1, 2, 127, 128, 255, 79, 243)):
            records[0x68 * i + 0x10] = value
        slot = 0x1f8 if layer == 0 else 0x30 + 8 * (layer if layer < 57 else 0)
        yield 5, TILE, layer, {BASE + 0x509d040: struct.pack("<Q", OWNER),
                               OWNER + slot: struct.pack("<Q", NODE if grid_ok else 0),
                               NODE + 0xb0: struct.pack("<Q", GRID),
                               GRID: bytes(grid), ROWS: bytes(records),
                               BASE + 0x50a50f0 + 0x10: b"\xf0",
                               TILE: struct.pack("<Qii", 0, *coords)}


def cases():
    for mode in (0, 3, 4):
        yield from type_cases(mode)
    yield from tile_cases()
    yield from visited_cases()
    yield from class_cases()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    sizes = {fn.start: fn.size for fn in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected field scan function boundaries")
    payload, expected, counts = [], [], Counter()
    for mode, x0, x1, memory in cases():
        entry, size = ENTRIES[mode]
        body = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: x0, UC_ARM64_REG_X1: x1}
        first = run(BASE + entry, body, regs, (UC_ARM64_REG_X0,), memory)
        if first != run(BASE + entry, body, regs, (UC_ARM64_REG_X0,), memory):
            raise ValueError(f"{entry:#x}: unstable original")
        expected.append((first.registers[UC_ARM64_REG_X0], *(v for write in first.writes for v in write)))
        values = [mode, BASE, x0, x1, sum(map(len, memory.values()))]
        values.extend(v for address, data in memory.items() for i, b in enumerate(data) for v in (address + i, b))
        payload.append(" ".join(map(str, values)))
        counts[entry] += 1
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                            text=True, capture_output=True, check=True, timeout=35)
    actual = [tuple(map(int, row.split())) for row in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise SystemExit(f"Expected {len(expected)} cases, got {len(actual)}")
    for index, (before, after) in enumerate(zip(expected, actual, strict=True)):
        if before != after:
            raise SystemExit(f"Case {index} mode {payload[index].split(maxsplit=1)[0]}: original {before}, C++ {after}")
    print("Matched original returns and ordered writes twice: " +
          ", ".join(f"{entry:#x}={counts[entry]}" for entry, _ in ENTRIES))


if __name__ == "__main__":
    main()
