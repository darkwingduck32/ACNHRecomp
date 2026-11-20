"""Compare the eight-slot lookup and the cached row-value entries against their NSO bodies."""
import argparse
import itertools
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
COLUMN = 0x1148DB0
VALUE = (
    (0x29CBE80, 12, 0xAC68310D), (0x29CBFB0, 13, 0x95109C4D),
    (0x29CC0E0, 14, 0x8238F88D), (0x29CC210, 15, 0x5EA2E0CD),
    (0x29CC340, 16, 0x498A840D), (0x29CC470, 1, 0xB6F6977B),
    (0x29CC5A0, 2, 0xA1DEF3BB), (0x29CC6D0, 3, 0x98A65EFB),
)
HEADER, ROW, OBJECT, READY, SLOTS = 0x22000, 0x22200, 0x24000, 0x25000, 0x26000


def read_fixtures(entry, mode, index, hash_value):
    for initial, cached, variant, match in itertools.product(
            (0, 1, 2), (-2, -1, 0, 28), (0, 1), (None, 0, 28, 0xffffffff, 0xfffffffe)):
        header = bytearray(0x40)
        struct.pack_into("<HB", header, 8, 2, variant)
        start = 0x1c if variant else 0xc
        struct.pack_into("<IIII", header, start, hash_value ^ 1, 0, hash_value, match or 0)
        if match is None:
            struct.pack_into("<I", header, start + 8, hash_value ^ 2)
        row = bytearray(0x40)
        struct.pack_into("<I", row, 0, ROW - HEADER)
        struct.pack_into("<I", row, 28, 0x56789abc)
        memory = {HEADER: bytes(header), ROW: bytes(row)}
        if mode == 0:
            obj = ROW
            memory[BASE + 0x5050d18] = struct.pack("<B3x8i", initial, *([cached] * 8))
        else:
            obj = OBJECT
            memory.update({
                OBJECT: struct.pack("<QQ", 0, ROW),
                BASE + 0x4ac4610: struct.pack("<QQ", READY, SLOTS),
                READY: bytes([initial]),
                SLOTS: struct.pack("<36i", *([cached] * 36)),
            })
        yield entry, mode, obj, index, hash_value, memory
    if mode == 0:
        yield entry, mode, ROW, index, hash_value, {ROW: bytes(4)}
    else:
        yield entry, mode, OBJECT, index, hash_value, {OBJECT: bytes(16)}
        yield entry, mode, OBJECT, index, hash_value, {
            OBJECT: struct.pack("<QQ", 0, ROW), ROW: bytes(4),
        }


def cases():
    for index in (0, 1, 7):
        yield from read_fixtures(COLUMN, 0, index, 0x623dc307)
    for entry, index, hash_value in VALUE:
        yield from read_fixtures(entry, 1, index, hash_value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module build {BUILD}")
    module = nso.load(args.module)
    ranges = {function.start: function.size for function in ehframe.parse(module)}
    expected_sizes = {COLUMN: 0xd4, **{entry: 0x124 for entry, _, _ in VALUE}}
    if any(ranges.get(entry) != size for entry, size in expected_sizes.items()):
        parser.error("unexpected function boundary")
    inputs = list(cases())
    lines = []
    for _, mode, obj, index, hash_value, memory in inputs:
        pairs = [(address + offset, byte) for address, block in memory.items()
                 for offset, byte in enumerate(block)]
        lines.append(f"{mode} {BASE} {obj} {index} {hash_value} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(inputs):
        raise SystemExit("candidate returned the wrong number of cases")

    counts, failures = {}, {}
    for i, ((entry, mode, obj, index, hash_value, memory), line) in enumerate(zip(inputs, native)):
        regs = {UC_ARM64_REG_X0: obj, UC_ARM64_REG_X1: index, UC_ARM64_REG_X2: hash_value}
        code = bytes(module.image[entry:entry + expected_sizes[entry]])
        first = run(BASE + entry, code, regs, (UC_ARM64_REG_X0,), memory)
        if first != run(BASE + entry, code, regs, (UC_ARM64_REG_X0,), memory):
            raise SystemExit(f"unstable original at {entry:#x}, case {i}")
        expected = (first.registers[UC_ARM64_REG_X0], *(item for write in first.writes for item in write))
        actual = tuple(map(int, line.split()))
        if actual != expected:
            failures.setdefault(entry, f"entry {entry:#x}, case {i}: expected {expected[:10]}, got {actual[:10]}")
        counts[entry] = counts.get(entry, 0) + 1
    if failures:
        raise SystemExit("\n".join(failures.values()))
    for entry, count in counts.items():
        print(f"{entry:#x}: {count} cases match two reference runs")


if __name__ == "__main__":
    main()
