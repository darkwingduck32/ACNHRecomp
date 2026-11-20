"""Compare ground and overlay terrain material masks against two original AArch64 runs."""
import argparse
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
ENTRIES = (0x84b728, 0x84b840)
OWNER, TILE, GRID, ROW, MASK = 0x21000, 0x22000, 0x23000, 0x24000, 0x25000


def fixture(coords, layer, kinds, mask, count=16, present=True):
    grid = bytearray(0x70)
    struct.pack_into("<II", grid, 0x38, 4, 4)
    struct.pack_into("<I4xQ", grid, 0x48 if layer == 0 else 0x58, count, ROW if present else 0)
    row = bytearray(16 * 0x34)
    tile = 4 * coords[1] + coords[0]
    if 0 <= tile < 16:
        struct.pack_into("<4B", row, tile * 0x34 + 0x30, *kinds)
    return {OWNER + 0x68: struct.pack("<Q", GRID), TILE: struct.pack("<Qii", 0, *coords),
            GRID: bytes(grid), ROW: bytes(row), MASK: mask.to_bytes(12, "little")}


def fixtures(layer):
    for point, count, present in product(((-1, 0), (0, -1), (0, 0), (3, 3), (4, 0), (0, 4), (1, 1)),
                                         (0, 5, 6, 16), (False, True)):
        yield point, fixture(point, layer, (7, 7, 7, 7), 1 << 7, count, present)
    for corner, kind, enabled in product(range(4), (0, 31, 32, 63, 64, 72, 73, 95, 127, 255), (False, True)):
        kinds = [255, 255, 255, 255]
        kinds[corner] = kind
        mask = 1 << (kind % 96) if enabled else 0
        yield (1, 1), fixture((1, 1), layer, kinds, mask)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    ranges = {fn.start: fn.size for fn in ehframe.parse(module)}
    if any(ranges.get(entry) != 0x118 for entry in ENTRIES):
        parser.error("unexpected terrain mask entry boundary")
    payload, results = [], []
    for layer, entry in enumerate(ENTRIES):
        body = bytes(module.image[entry:entry + ranges[entry]])
        count = 0
        for coords, memory in fixtures(layer):
            registers = {UC_ARM64_REG_X0: OWNER, UC_ARM64_REG_X1: TILE, UC_ARM64_REG_X2: MASK}
            first = run(BASE + entry, body, registers, (UC_ARM64_REG_X0,), memory)
            if first.writes or first != run(BASE + entry, body, registers, (UC_ARM64_REG_X0,), memory):
                raise ValueError(f"{entry:#x}: original unstable or a caller-visible write")
            values = [OWNER, MASK, *coords, layer, sum(map(len, memory.values()))]
            values.extend(word for address, data in memory.items()
                          for offset, byte in enumerate(data) for word in (address + offset, byte))
            payload.append(" ".join(map(str, values)))
            results.append(first.registers[UC_ARM64_REG_X0])
            count += 1
        print(f"{entry:#x}: {count} cases agree across two original runs")
    candidate = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                               text=True, capture_output=True, check=True, timeout=30)
    actual = list(map(int, candidate.stdout.splitlines()))
    if actual != results:
        difference = next((i for i, (a, b) in enumerate(zip(actual, results)) if a != b), min(len(actual), len(results)))
        raise SystemExit(f"Case {difference}: expected {results[difference:difference + 1]}, "
                         f"C++ {actual[difference:difference + 1]}")
    print(f"{len(results)} cases match both typed terrain material routes")


if __name__ == "__main__":
    main()
