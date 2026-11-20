"""Compare five observed field bookkeeping operations with original AArch64."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
OBJECT, HEADER, LIST, READY, SLOTS, TABLE, FLAGS, TILE, OUTPUT = (
    0x21000, 0x23000, 0x25000, 0x27000, 0x29000, 0x2b000, 0x2d000, 0x2f000, 0x31000,
)
ENTRIES = (
    (0x16bfae0, 0x198), (0x16c0910, 0x120), (0x194fa00, 0xfc), (0x874bc8, 0x150), (0x128367c, 0xe4),
)


def properties(mode):
    for ready, cached, layout, selected, column, value in product(
        (0, 1), (-2, -1, 16), (0, 1, 2), (0, 1, 3), (0, 1, 2), (0, 0x1234),
    ):
        prefix = 0x1c if layout == 1 else 0xc
        first = prefix + 16
        header = bytearray(0xc0)
        struct.pack_into("<IIHB", header, 0, 3, 40, 2, layout == 1)
        key = 0x54706054 if mode == 0 else 0xa4db9685
        struct.pack_into("<IIII", header, prefix, key if column else key ^ 1, 16 if column == 1 else 0xffffffff,
                         key ^ 8, 20)
        for i in range(3):
            offset = first + i * 40
            struct.pack_into("<I", header, offset, offset)
            struct.pack_into("<H", header, offset + 16, value)
        obj = bytearray(0x204)
        struct.pack_into("<I4xQi", obj, 0x1b8, 2, LIST, 0 if layout == 2 else 3)
        struct.pack_into("<I4xQ", obj, 0x1e0, 3, LIST)
        struct.pack_into("<H", obj, 0x200, 16)
        struct.pack_into("<Q", obj, 0xb8, TABLE)
        struct.pack_into("<I4xQ", obj, 0xd0, 2, FLAGS)
        memory = {
            BASE + 0x4ab5c58: struct.pack("<QQ", READY, SLOTS), READY: bytes([ready]),
            SLOTS: struct.pack("<14i", *([cached] * 14)), OBJECT: bytes(obj),
            HEADER: bytes(header), LIST: struct.pack("<3Q", HEADER + first, HEADER + first + 40, 0),
            TABLE + 8: struct.pack("<Q", HEADER), FLAGS: bytes((1, value != 0)),
        }
        yield mode, OBJECT, selected if mode == 0 else selected + 16, 0, memory


def layer():
    for index, node in product((0, 1, 2, 55, 56, 57, 0xffffffff), (0, TABLE)):
        slot = 0x1f8 if index == 0 else 0x30 + 8 * (index if index < 57 else 0)
        yield 2, OBJECT, 0, 0, {BASE + 0x4ab27a8: struct.pack("<Q", TABLE),
                               TABLE: struct.pack("<Q", LIST), LIST + slot: struct.pack("<Q", node),
                               OBJECT + 8: struct.pack("<I", index)}


def terrain():
    coords = ((-1, 0), (0, -1), (0, 0), (3, 3), (3, 2), (4, 3), (2, 4))
    for (x, z), mask, alias in product(coords, (0, 1, 0x4000, 0x3f00, 0x7f00, 0x7f40, 0x20f0, 0x1500),
                                       ("none", "tile", "flags")):
        bitmap = struct.pack("<16I", *([mask] * 16))
        target = OUTPUT if alias == "none" else TILE if alias == "tile" else FLAGS + 4 * (max(0, 4 * z + x) % 16) - 8
        yield 3, OBJECT, TILE, target, {OBJECT + 0x38: struct.pack("<II", 4, 4),
                                        OBJECT + 0x68: struct.pack("<I4xQ", 16, FLAGS),
                                        TILE: struct.pack("<Qii", 0, x, z), FLAGS: bitmap, OUTPUT: bytes(16)}


def queue():
    for (x, z), bitmap, size, cap, full in product(
        ((-1, 1), (1, -1), (0, 0), (3, 3), (1, 1), (4, 0), (0, 4), (65536, 65539)),
        (0, FLAGS), (0, 5, 16), (0, 1, 4), (False, True),
    ):
        owner = bytearray(0x34)
        struct.pack_into("<IIQIIQIII", owner, 8, 4, 4, bitmap, size, 0xffffffff, LIST,
                         cap, max(0, cap - 1), cap if full else max(0, cap - 2))
        bits = 1 << (4 * (z & 3) + (x & 3)) if full else 0
        yield 4, OBJECT, x & 0xffffffff, z & 0xffffffff, {
            OBJECT: bytes(owner), FLAGS: struct.pack("<I", bits), LIST: bytes(16),
        }


def cases():
    yield from properties(0)
    yield from properties(1)
    yield from layer()
    yield from terrain()
    yield from queue()


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
        parser.error("unexpected function range")
    payload, expected, counts = [], [], Counter()
    for mode, x0, x1, x2, memory in cases():
        entry, size = ENTRIES[mode]
        body = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: x0, UC_ARM64_REG_X1: x1, UC_ARM64_REG_X2: x2}
        first = run(BASE + entry, body, regs, (UC_ARM64_REG_X0,), memory)
        if first != run(BASE + entry, body, regs, (UC_ARM64_REG_X0,), memory):
            raise ValueError(f"{entry:#x}: unstable original")
        expected.append((first.registers[UC_ARM64_REG_X0], *(v for write in first.writes for v in write)))
        values = [mode, BASE, x0, x1, x2, sum(map(len, memory.values()))]
        values.extend(v for address, data in memory.items() for i, b in enumerate(data) for v in (address + i, b))
        payload.append(" ".join(map(str, values)))
        counts[entry] += 1
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                            text=True, capture_output=True, check=True, timeout=45)
    actual = [tuple(map(int, row.split())) for row in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise SystemExit(f"Expected {len(expected)} cases, got {len(actual)}")
    for index, (before, after) in enumerate(zip(expected, actual, strict=True)):
        if before != after:
            raise SystemExit(f"Case {index} mode {payload[index].split(maxsplit=1)[0]}: original {before}; C++ {after}")
    print("Matched original results and ordered writes twice: " +
          ", ".join(f"{entry:#x}={counts[entry]}" for entry, _ in ENTRIES))


if __name__ == "__main__":
    main()
