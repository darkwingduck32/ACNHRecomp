"""Replay the original neighborhood gather with declared field and predicate responses."""
import argparse
import itertools
import struct
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x127CAB0, 0x5AC
SCRATCH = 0x20000
ITEM_SITES = (0x127CCB8, 0x127CD0C, 0x127CD60, 0x127CE04, 0x127CE50)
CELL_SITES = (0x127CC84, 0x127CF84, 0x127CFD0, 0x127CFF8)
RETURN = 0xD65F03C0


def words(*opcodes):
    return struct.pack(f"<{len(opcodes)}I", *opcodes)


def w0(value):
    return words(0x52800000 | (value << 5), RETURN)


def cell_body(anchor_matches):
    # Load half-tile coordinates from X2 and return them as a record in X8.
    # The optional resolved-position argument X3 can deliberately miss X.
    adjust = () if anchor_matches else (0x11000529,)  # add w9,w9,#1
    skip = 3 + len(adjust)
    return words(0xb9400849, 0xb9400c4a, 0xb9000109, 0xb900050a,
                 0xb4000003 | (skip << 5), *adjust, 0xb9000869, 0xb9000c6a, RETURN)


def fixture(x, z, mode):
    scratch = bytearray(0x178)
    struct.pack_into("<I", scratch, 0x18, 0x200)
    struct.pack_into("<ii", scratch, 0x28, x, z)
    struct.pack_into("<ii", scratch, 0x160, x, z)
    struct.pack_into("<ii", scratch, 0x170, x + 2, z + 2)
    if mode == "empty":
        struct.pack_into("<i", scratch, 0x164, z + 3)
    memory = {
        SCRATCH: bytes(scratch),
        BASE + 0x54c04d8: bytes(4),
        BASE + 0x4ab2720: struct.pack("<Q", 0x21000),
        0x21000: bytes(8),
        BASE + 0x4ab43c0: struct.pack("<Q", 0x21008),
        0x21008: bytes(8),
        BASE + 0x4abbc68: struct.pack("<Q", 0x21010),
        0x21010: bytes(8),
    }
    for offset in (0x4abfe68, 0x4ab2010, 0x4abc1a0, 0x4ac35c0, 0x4ac0510, 0x4ab2018):
        memory[BASE + offset] = bytes(8)

    if mode == "actor":
        actor = bytearray(0x490)
        for offset in (0xb0, 0x488, 0x2b0):
            struct.pack_into("<Q", actor, offset, 0x23000)
        struct.pack_into("<I", actor, 0x1d8, 1)
        struct.pack_into("<Q", actor, 0x1e0, 0x23008)
        memory[0x21010] = struct.pack("<Q", 0x22000)
        memory[0x22000] = bytes(actor)
        memory[0x23008] = struct.pack("<Q", 1)

    if mode.startswith("metadata"):
        memory[0x21008] = struct.pack("<Q", 0x50000)
        memory[0x50000 + 0x57e58] = struct.pack("<Q", 0x24000)
        table = bytearray(0x1fa)
        struct.pack_into("<I", table, 0x1d8, 1)
        struct.pack_into("<Q", table, 0x1e0, 0x23008)
        memory[0x24000] = bytes(table)
        memory[0x23008] = struct.pack("<Q", 1)
        memory[BASE + 0x4ac0428] = struct.pack("<Q", 0x21018)
        memory[0x21018] = struct.pack("<Q", 0x24000)

    item_id = 0xf619 if mode.startswith("extent") else 0
    extent = 1 if mode == "extent-match" else 2
    # Store the two upper bounds through the helper's second argument.
    extent_body = words(0x52800008 | (extent << 5), 0xb9002028, 0xb9002428, RETURN)
    callees = {
        BASE + 0x127cb68: (BASE + 0x16baf44, w0(0)),
        BASE + 0x127cd94: (BASE + 0x19ee3b4, w0(int(mode == "metadata-miss"))),
        BASE + 0x127cea0: (BASE + 0x253fe7c, extent_body),
        **{BASE + site: (BASE + 0x267d6c4, w0(item_id)) for site in ITEM_SITES},
        **{BASE + site: (BASE + 0x195033c, cell_body(mode != "anchor-miss")) for site in CELL_SITES},
    }
    return memory, callees


def expected_records(x, z):
    return [
        (2 * tx + dx, 2 * tz + dz)
        for tz in range(z, z + 3)
        for tx in range(x, x + 3)
        for dx, dz in ((0, 0), (1, 0), (1, 1), (0, 1))
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected neighborhood gather boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    modes = ("absent", "actor", "metadata-match", "metadata-miss", "extent-match", "extent-miss", "anchor-miss", "empty")
    cases = 0
    for (x, z), mode in itertools.product(((-4, -3), (0, 0), (40, 69)), modes):
        memory, callees = fixture(x, z, mode)
        regs = {UC_ARM64_REG_X0: SCRATCH}
        first = run(BASE + ENTRY, code, regs, (), memory, callees=callees)
        if first != run(BASE + ENTRY, code, regs, (), memory, callees=callees):
            raise SystemExit(f"{mode} at {x,z}: original replay changed across executions")
        actual = first.memory[SCRATCH]
        records = list(struct.iter_unpack("<ii", actual[0x30:0x150]))
        expected = [(0, 0)] * 36 if mode == "empty" else expected_records(x, z)
        if records != expected:
            raise SystemExit(f"{mode} at {x,z}: expected {expected[:4]}, got {records[:4]}")
        occupied = mode in ("actor", "metadata-match", "extent-match")
        mask, = struct.unpack_from("<I", actual, 0x18)
        if mask != (0x3ff if occupied else 0x200):
            raise SystemExit(f"{mode} at {x,z}: unexpected mask {mask:#x}")
        cases += 1
    print(f"{cases} original neighborhood cases match their expected records and masks twice")


if __name__ == "__main__":
    main()
