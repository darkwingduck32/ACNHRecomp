"""Compare five original catalog field helpers with typed C++."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X8

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
OBJECT, ITEM, TABLE, ROWS, LIST, ENUM, ALT, FLAGS, OUTPUT, READY, SLOTS, TEXT = (
    0x21000, 0x23000, 0x25000, 0x27000, 0x29000, 0x2b000, 0x2d000,
    0x2f000, 0x31000, 0x33000, 0x35000, 0x37000,
)
ENTRIES = (
    (0x19d778c, 0x140), (0x19d78cc, 0x1d8), (0x19e89e0, 0x178),
    (0x19e8b58, 0x118), (0x19d7ca0, 0x1c8),
)


def rows(prefix, count=2, stride=40, columns=1, found=True, offset=8):
    first = (0x1c if prefix else 0xc) + 8
    data = bytearray(0x90)
    struct.pack_into("<IIHB", data, 0, count, stride, columns, prefix)
    struct.pack_into("<II", data, first - 8, 0x87bf00e8 if found else 1, offset)
    for row in (first, first + 40):
        struct.pack_into("<I", data, row, row)
    return first, data


def database_captures(mode):
    for prepared, prefix, category, value in product((0, 1, 2), (0, 1), (0, 1, 2, 0xffffffff),
                                                     (0, 0x21, 0x74, 0x75, 0xffff)):
        first, raw = rows(prefix)
        db = bytearray(0x1c4)
        struct.pack_into("<Q", db, 0xb0, TABLE)
        struct.pack_into("<I4xQ", db, 0xc8, 2, FLAGS)
        struct.pack_into("<I4xQi", db, 0x1b0, 1, LIST, prepared)
        mem = {OBJECT: bytes(db), TABLE + 8: struct.pack("<Q", ROWS), ROWS: bytes(raw),
               LIST: struct.pack("<2Q", ROWS + first, ROWS + first + 40), FLAGS: b"\x01\x00",
               BASE + 0x522a7a0: struct.pack("<Q", OBJECT),
               BASE + 0x5226b00: struct.pack("<I4xQ", 2, ENUM),
               ENUM: struct.pack("<2H", value, value)}
        if mode == 0:
            yield 0, category, 0, mem
        else:
            at, item = rows(prefix)
            mem[ITEM] = bytes(item)
            mem[OUTPUT + 0x10] = struct.pack("<Q", ITEM + at if category != 0xffffffff else 0)
            mem[BASE + 0x5226af0] = struct.pack("<I4xQ", 2, ALT)
            mem[ALT] = struct.pack("<2H", category & 0xffff, 0)
            yield 1, OUTPUT, 0, mem


def predicates(mode):
    for primary, secondary, valid, available, prefix in product(
        (0, 7, 0xab, 0xae, 0xffff), (0, 7, 0xab, 0xad, 0xffff),
        ("empty", "back", "stride", "cols", "valid"), (0, 1), (0, 1),
    ):
        first, data = rows(prefix, stride=0 if valid == "stride" else 40, columns=0 if valid == "cols" else 1)
        if valid == "back":
            struct.pack_into("<I", data, first, 0)
        mem = {ITEM: bytes(data), OUTPUT + 0x10: struct.pack("<Q", 0 if valid == "empty" else ITEM + first),
               BASE + 0x4ac00c8: struct.pack("<Q", TABLE), TABLE: struct.pack("<I4xQ", available, ALT),
               BASE + 0x5226ec0: struct.pack("<I4xQ", 1, ENUM),
               ENUM: struct.pack("<H", secondary), ALT: struct.pack("<H", primary)}
        yield mode, OUTPUT, 0, mem


def descriptions():
    for present, ready, cached, found, offset in product((0, 1), (0, 1), (-2, -1, 0, 8), (0, 1), (8, 0xffffffff)):
        first, table = rows(0, found=bool(found), offset=offset)
        db = bytearray(0x1c4)
        struct.pack_into("<I4xQi", db, 0x1b0, 1, LIST, 1)
        yield 4, 0, OUTPUT, {
            BASE + 0x522a7a8: struct.pack("<Q", OBJECT), OBJECT: bytes(db),
            BASE + 0x4ab2020: struct.pack("<Q", TEXT), BASE + 0x4ab2238: struct.pack("<Q", TEXT + 0x100),
            BASE + 0x4abff10: struct.pack("<Q", TEXT + 0x200),
            TEXT + 0x108: struct.pack("<Q", TEXT + 0x110), TEXT + 0x200: struct.pack("<Q", TEXT + 0x210),
            BASE + 0x4abe218: struct.pack("<QQ", READY, SLOTS), READY: bytes([ready]),
            SLOTS: struct.pack("<14i", *([cached] * 14)), OUTPUT: bytes(16),
            ROWS: bytes(table), LIST: struct.pack("<Q", ROWS + first if present else 0),
        }


def cases():
    yield from database_captures(0)
    yield from database_captures(1)
    yield from predicates(2)
    yield from predicates(3)
    yield from descriptions()


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
        parser.error("unexpected catalog function range")
    payload, expected, counts = [], [], Counter()
    for mode, x0, x8, memory in cases():
        entry, size = ENTRIES[mode]
        body = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: x0, UC_ARM64_REG_X8: x8}
        outputs = () if mode == 4 else (UC_ARM64_REG_X0,)
        first = run(BASE + entry, body, regs, outputs, memory)
        if first != run(BASE + entry, body, regs, outputs, memory):
            raise ValueError(f"{entry:#x}: unstable original")
        expected.append((first.registers.get(UC_ARM64_REG_X0, 0), *(v for write in first.writes for v in write)))
        values = [mode, BASE, x0, x8, sum(map(len, memory.values()))]
        values.extend(v for address, data in memory.items() for i, b in enumerate(data) for v in (address + i, b))
        payload.append(" ".join(map(str, values)))
        counts[entry] += 1
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                            text=True, capture_output=True, check=True, timeout=40)
    actual = [tuple(map(int, row.split())) for row in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise SystemExit(f"Expected {len(expected)} cases, got {len(actual)}")
    for index, (before, after) in enumerate(zip(expected, actual, strict=True)):
        if before != after:
            raise SystemExit(f"Case {index} mode {payload[index].split(maxsplit=1)[0]}: original {before}; C++ {after}")
    print("Matched original returns and ordered writes twice: " +
          ", ".join(f"{entry:#x}={counts[entry]}" for entry, _ in ENTRIES))


if __name__ == "__main__":
    main()
