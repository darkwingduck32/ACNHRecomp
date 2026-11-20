"""Run the eight original triangle predicates against the typed terrain query."""
import argparse
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
OWNER, TILE, GROUND, OVERLAY, FLAGS = 0x21000, 0x22000, 0x23000, 0x24000, 0x25000
ENTRIES = (
    (0x866f60, 0x170), (0x8670d0, 0x178), (0x867248, 0x178), (0x8673c0, 0x178),
    (0x867538, 0x130), (0x867668, 0x13c), (0x8677a4, 0x13c), (0x8678e0, 0x13c),
)


def fixture(quadrant, coords=(1, 1), depth=4, ground_count=16, overlay_count=16, flag_count=16,
            ground_present=True, overlay_present=True, flags_present=True, active_flags=0, ground_kind=5,
            overlay_kind=7, heights=(0., 0., 0.)):
    owner = bytearray(0x78)
    struct.pack_into("<II", owner, 0x38, 4, depth)
    struct.pack_into("<I4xQ", owner, 0x48, ground_count, GROUND if ground_present else 0)
    struct.pack_into("<I4xQ", owner, 0x58, overlay_count, OVERLAY if overlay_present else 0)
    struct.pack_into("<I4xQ", owner, 0x68, flag_count, FLAGS if flags_present else 0)
    terrain = bytearray(16 * 0x34)
    overlay = bytearray(16 * 0x34)
    flags = bytearray(16 * 4)
    cell = 4 * coords[1] + coords[0]
    if 0 <= cell < 16:
        struct.pack_into("<3f", terrain, cell * 0x34 + 12 * quadrant, *heights)
        terrain[cell * 0x34 + 0x30 + quadrant] = ground_kind
        overlay[cell * 0x34 + 0x30 + quadrant] = overlay_kind
        struct.pack_into("<I", flags, cell * 4, active_flags)
    output = {OWNER: bytes(owner), TILE: struct.pack("<Qii", 0, *coords)}
    if ground_present:
        output[GROUND] = bytes(terrain)
    if overlay_present:
        output[OVERLAY] = bytes(overlay)
    if flags_present:
        output[FLAGS] = bytes(flags)
    return output


def fixtures(quadrant):
    for dims in ((0, 0), (3, 3), (-1, 1), (1, -1), (4, 1), (1, 4)):
        yield dims, fixture(quadrant, coords=dims)
    for ground, overlay, flags, kind, overlay_kind in product(
        (False, True), (False, True), (False, True), (5, 7), (7, 8)
    ):
        for mask in (0, 1, 2):
            yield (1, 1), fixture(quadrant, ground_present=ground, overlay_present=overlay,
                                  flags_present=flags, active_flags=mask, ground_kind=kind, overlay_kind=overlay_kind)
    for ground, overlay, flags in product((5, 6), repeat=3):
        yield (1, 1), fixture(quadrant, ground_count=ground, overlay_count=overlay,
                              flag_count=flags, active_flags=1, heights=(1., 2., 3.))
    near, apart = 2**-18, 2**-17
    for values in product((0., -0., near, -near, apart, -apart, float("nan"), float("inf")), repeat=2):
        yield (1, 1), fixture(quadrant, heights=(0., *values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    ranges = {f.start: f.size for f in ehframe.parse(module)}
    if any(ranges.get(entry) != size for entry, size in ENTRIES):
        parser.error("triangle entry ranges differ")
    canonical = []
    expectations = []
    for index, (entry, length) in enumerate(ENTRIES):
        quadrant, sloped = index % 4, index < 4
        body = bytes(module.image[entry:entry + length])
        count = 0
        for coords, memory in fixtures(quadrant):
            registers = {UC_ARM64_REG_X0: OWNER, UC_ARM64_REG_X1: TILE}
            first = run(BASE + entry, body, registers, (UC_ARM64_REG_X0,), memory)
            if first != run(BASE + entry, body, registers, (UC_ARM64_REG_X0,), memory) or first.writes:
                raise ValueError(f"{entry:#x}: unstable original or an external write")
            argument_line = [OWNER, *coords, quadrant, int(sloped), sum(map(len, memory.values()))]
            argument_line.extend(word for address, data in memory.items()
                                 for offset, byte in enumerate(data) for word in (address + offset, byte))
            canonical.append(" ".join(map(str, argument_line)))
            expectations.append(first.registers[UC_ARM64_REG_X0])
            count += 1
        print(f"{entry:#x}: {count} stable original AArch64 cases")
    actual = subprocess.run([str(args.candidate.resolve())], input="\n".join(canonical) + "\n",
                            text=True, capture_output=True, check=True, timeout=30)
    if list(map(int, actual.stdout.splitlines())) != expectations:
        for index, (original, native) in enumerate(zip(expectations, map(int, actual.stdout.splitlines()))):
            if original != native:
                raise SystemExit(f"Case {index}: original {original}, C++ {native}")
        raise SystemExit("candidate did not return all cases")
    print(f"{len(expectations)} cases matched the typed triangle query")


if __name__ == "__main__":
    main()
