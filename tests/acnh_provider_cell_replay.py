"""Replay the original provider's data and virtual-call orchestration."""
import argparse
from collections import Counter
import hashlib
import itertools
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X8

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x11E4F58, 0x330
PROVIDER, POINT, READER_VTABLE, TABLE, TABLE_VTABLE = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000
RECORDS, OUTPUT, ROOT, MASK = 0x25000, 0x26000, 0x27000, 0x28000
CONTEXT_HANDLE, READ_CONTEXT, RELEASE_CONTEXT, CONTEXT_VTABLE, RELEASE_VTABLE = (
    0x29000, 0x2a000, 0x2b000, 0x2c000, 0x2d000
)
RETURN = 0xD65F03C0


def words(*ops):
    return struct.pack(f"<{len(ops)}I", *ops)


def address_words(reg, value):
    return 0xd2800000 | ((value & 0xffff) << 5) | reg, 0xf2a00000 | ((value >> 16) << 5) | reg


def fixture(mode, ox, oz, root_present):
    provider = bytearray(0x1d0)
    struct.pack_into("<ii", provider, 0x10, ox, oz)
    struct.pack_into("<Q", provider, 0x28, READER_VTABLE)
    struct.pack_into("<QIIII", provider, 0x1b8, MASK if mode == 1 else 0, 16, 11, 4, 4)
    x = ox + (4 if mode == 7 else 2)
    z = oz + (4 if mode == 8 else 1)
    reader_vtable = struct.pack("<3Q", BASE + 0x120000, 0, BASE + 0x120060)
    table = bytearray(0x30)
    struct.pack_into("<QQ", table, 0, TABLE_VTABLE, RECORDS)
    context = mode in (5, 6, 9)
    if context:
        struct.pack_into("<Q", table, 0x28, CONTEXT_HANDLE)
    table_vtable = bytearray(0x30)
    struct.pack_into("<QQQ", table_vtable, 0x18, BASE + 0x120020, BASE + 0x120040, BASE + 0x120080)
    records = bytearray().join(struct.pack("<HHI", index + 10, 0x4321, 0x12345678 + index) for index in range(16))
    if mode in (3, 4, 6):
        distance = 3 if mode == 4 else 1
        struct.pack_into("<HHI", records, 8 * (2 + 4), 0xfffd, 0x9876, distance << 24 | distance << 16)
    memory = {
        PROVIDER: bytes(provider), POINT: struct.pack("<Qii", 0, x, z), READER_VTABLE: reader_vtable,
        TABLE: bytes(table), TABLE_VTABLE: bytes(table_vtable), RECORDS: bytes(records),
        OUTPUT: bytes(8), ROOT: bytes(16), MASK: struct.pack("<I", 1 << 6),
        BASE + 0x5486008: struct.pack("<II", 0x1010fffe, 0x41424344),
    }
    if context:
        memory.update({
            CONTEXT_HANDLE: struct.pack("<QQ", READ_CONTEXT, RELEASE_CONTEXT),
            READ_CONTEXT: struct.pack("<Q", CONTEXT_VTABLE),
            RELEASE_CONTEXT: struct.pack("<Q", RELEASE_VTABLE),
            CONTEXT_VTABLE: struct.pack("<Q", BASE + 0x1200a0),
            RELEASE_VTABLE: struct.pack("<Q", BASE + 0x1200c0),
        })
    callees = {}
    for sites, target, body in (
        ((0x11e4fa4, 0x11e4fcc, 0x11e5004, 0x11e50a0, 0x11e516c, 0x11e5190), 0x120000,
         words(*address_words(0, TABLE), RETURN)),
        ((0x11e4fb0, 0x11e50b0, 0x11e5178), 0x120020, words(0x52800080, RETURN)),
        ((0x11e4fd8, 0x11e50c4, 0x11e519c), 0x120040, words(0x52800080, RETURN)),
        ((0x11e4ff0,), 0x120060, words(0x52800000 | (int(mode != 2) << 5), RETURN)),
        ((0x11e5114, 0x11e51cc), 0x120080, words(0x0b020820, 0x531d7000, RETURN)),
        ((0x11e50f0,), 0x1200a0, words(0x52800000 | (int(mode != 9) << 5), RETURN)),
        ((0x11e5284,), 0x1200c0, words(RETURN)),
        ((0x11e51fc,), 0x2540184, words(RETURN)),
        # Return the anchor table and relative coordinates through X8.
        ((0x11e51b4,), 0x11e4578,
         words(*address_words(9, TABLE), 0xf9000509, 0xb9400829, 0xb9001509, 0xb9400c29, 0xb9001909, RETURN)),
    ):
        for site in sites:
            callees[BASE + site] = BASE + target, body
    return ROOT if root_present else 0, memory, callees


TRACE = re.compile(
    r"acnh shovel provider cell (original|cpp) (parent=[^\r\n]+?) counter=([0-9a-f]+),([0-9a-f]+)"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    states = Counter()
    for route, state, before, after in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        if before != after:
            raise ValueError(f"{path}: provider did not restore the incoming counter for {state}")
        states[state] += 1
    if not states:
        raise ValueError(f"{path}: no {expected_route} shovel provider calls")
    print(f"{path}: {states.total()} provider calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected provider boundary")
    if args.live:
        original = live_cases(args.live[0], "original")
        native = live_cases(args.live[1], "cpp")
        if original != native:
            raise SystemExit(f"provider mismatch. Original-only: {original-native}. C++ only: {native-original}")
    cases = [(mode, *fixture(mode, x, z, root)) for mode, (x, z), root in itertools.product(
        range(10), ((-20, -18), (0, 0), (80, 138)), (False, True)
    )]
    lines = []
    for mode, root, memory, _ in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{BASE} {PROVIDER} {POINT} {root} {OUTPUT} {mode} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    for i, ((mode, root, memory, callees), line) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: PROVIDER, UC_ARM64_REG_X1: POINT, UC_ARM64_REG_X2: root, UC_ARM64_REG_X8: OUTPUT}
        result = run(BASE + ENTRY, code, regs, (), memory, callees=callees)
        if result != run(BASE + ENTRY, code, regs, (), memory, callees=callees):
            raise SystemExit(f"unstable provider run at case {i}")
        expected = [len(result.calls), *(target - BASE for _, target, _ in result.calls)]
        expected.extend(value for write in result.writes for value in write)
        actual = list(map(int, line.split()))
        if actual != expected:
            mismatch = next((j for j, (a, b) in enumerate(zip(actual, expected)) if a != b),
                            min(len(actual), len(expected)))
            raise SystemExit(f"case {i} mode {mode}, index {mismatch}: "
                             f"expected {expected[mismatch:mismatch+8]}, got {actual[mismatch:mismatch+8]}")
    print(f"{len(cases)} provider cases match two reference replays with exact call and write order")


if __name__ == "__main__":
    main()
