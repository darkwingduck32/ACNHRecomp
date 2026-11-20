"""Compare the typed terrain clearance and flag query with two original AArch64 runs."""
import argparse
from itertools import product
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import (
    UC_ARM64_REG_S0, UC_ARM64_REG_SP, UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2,
)

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, CHILD = 0x4000000000, 0x15eb1a0, 0x874bc8
POINT, LINK, OWNER, GRID, FLAGS, TRIG = 0x21000, 0x23000, 0x25000, 0x27000, 0x29000, 0x2b000
FIELD, LAYER = 0x31000, 0x33000
CENTER, RING = (1, 1), ((2, 1), (2, 2), (1, 2), (0, 2), (0, 1), (0, 0), (1, 0), (2, 0))
DIAGONAL = 2 ** -0.5
DIRECTIONS = ((1, 0), (DIAGONAL, DIAGONAL), (0, 1), (-DIAGONAL, DIAGONAL),
              (-1, 0), (-DIAGONAL, -DIAGONAL), (0, -1), (DIAGONAL, -DIAGONAL))


def fixture(tiles, point=(7.5, 7.5), owner=OWNER, grid=GRID, step=0x20000000, populated=False):
    table = bytearray(256 * 16)
    for i, (x, z) in enumerate(DIRECTIONS):
        struct.pack_into("<4f", table, i * 32 * 16, x, 0, z, 0)
    if populated:
        for i in range(256):
            struct.pack_into("<4f", table, i * 16, 0.25-i/256, 0.5, i/128-0.75, -0.25)
    cells = [0] * 9
    for (x, z), flag in tiles.items():
        cells[3*z+x] = flag
    return {
        POINT: struct.pack("<3f", point[0], 0.0, point[1]),
        BASE+0x4ab2010: struct.pack("<Q", FIELD),
        BASE+0x4ab5ba0: struct.pack("<Q", LAYER),
        BASE+0x4ab27c8: struct.pack("<Q", LINK),
        BASE+0x4ab2d00: struct.pack("<Q", TRIG),
        BASE+0x5140230: struct.pack("<I", step),
        LINK: struct.pack("<Q", owner),
        OWNER+0xa8: struct.pack("<Q", grid),
        GRID+0x38: struct.pack("<2I", 3, 3),
        GRID+0x68: struct.pack("<I4xQ", 9, FLAGS),
        FLAGS: struct.pack("<9I", *cells),
        TRIG: bytes(table),
    }


def cases():
    for mode, radius, tile in product(
        (0, 1), (0, 0x80000000, 0x33ffffff, 0x34000000, 0x40a00000, 0xc0a00000), (None, CENTER, *RING),
    ):
        yield mode, radius, fixture({tile: 1 << (13 if mode else 14)} if tile else {})
    for mode, point, (owner, grid) in product(
        (0, 1), ((-5., -4.5), (-0., 1.), (15., 7.5), (7.5, 7.5), (2147483648., -2147483648.)),
        ((0, GRID), (OWNER, 0), (OWNER, GRID)),
    ):
        yield mode, 0x407fffff, fixture({CENTER: 0x7f40}, point, owner, grid, 0x20ff1255, True)


def candidate_input(mode, radius, memory):
    data = [(address+i, byte) for address, block in memory.items() for i, byte in enumerate(block)]
    return " ".join(str(n) for n in (BASE, POINT, radius, mode, len(data), *(n for item in data for n in item)))


def original_result(parent, child, mode, radius, memory):
    calls = {BASE+site: (BASE+CHILD, child) for site in (0x15eb25c, 0x15eb390)}
    registers = {UC_ARM64_REG_X0: POINT, UC_ARM64_REG_X1: mode, UC_ARM64_REG_S0: radius}
    tiles = []

    def observe(machine, site, target):
        sp = machine.reg_read(UC_ARM64_REG_SP)
        assert machine.reg_read(UC_ARM64_REG_X0) == GRID
        assert machine.reg_read(UC_ARM64_REG_X1) == sp+0x10
        assert machine.reg_read(UC_ARM64_REG_X2) == sp
        assert struct.unpack("<Q", machine.mem_read(sp+0x10, 8))[0] == FIELD+0x10
        assert struct.unpack("<Q", machine.mem_read(sp, 8))[0] == LAYER+0x10
        assert site == BASE + (0x15eb390 if tiles else 0x15eb25c)
        tiles.extend(struct.unpack("<2i", machine.mem_read(sp+0x18, 8)))
        return False

    result = run(BASE+ENTRY, parent, registers, (UC_ARM64_REG_X0, UC_ARM64_REG_S0),
                 memory, callees=calls, call_model=observe)
    assert not result.writes and result.memory == memory
    return (result.registers[UC_ARM64_REG_X0], result.registers[UC_ARM64_REG_S0], *tiles)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    module = nso.load(args.module)
    bounds = {f.start: f.size for f in ehframe.parse(module)}
    assert bounds[ENTRY] == 0x230 and bounds[CHILD] == 0x150
    parent = bytes(module.image[ENTRY:ENTRY+bounds[ENTRY]])
    child = bytes(module.image[CHILD:CHILD+bounds[CHILD]])
    campaign = list(cases())
    payload = "\n".join(candidate_input(*case) for case in campaign) + "\n"
    output = subprocess.run([str(args.candidate.resolve())], input=payload, check=True,
                            text=True, capture_output=True).stdout.splitlines()
    assert len(output) == len(campaign)
    outcomes, query_counts = set(), set()
    for index, (case, line) in enumerate(zip(campaign, output)):
        expected = original_result(parent, child, *case)
        assert original_result(parent, child, *case) == expected
        actual = tuple(int(value) for value in line.split())
        if actual != expected:
            raise ValueError(f"Case {index}, selector {case[0]} and radius {case[1]:08x}: "
                             f"original {expected}, C++ {actual}")
        outcomes.add(expected[0])
        query_counts.add((len(expected)-2)//2)
    assert outcomes == {0, 1} and {0, 1, 2, 9} <= query_counts
    print(f"{len(campaign)} original clearance and flag summary cases matched twice, "
          f"exact W0/S0 and ordered tiles; query counts {sorted(query_counts)}")


if __name__ == "__main__":
    main()
