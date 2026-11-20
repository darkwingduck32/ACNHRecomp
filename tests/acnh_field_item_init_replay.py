"""Replay the guarded empty-item routes of the shovel snapshot initializers."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRIES = ((0x267d4ac, 0x218), (0x267f248, 0x5a8))
ITEM, POINTER, OWNER, CATALOG, LOOKUP, DESC_POINTER, DESCRIPTOR = (
    0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000, 0x26000
)
TRACE = re.compile(r"acnh shovel item init (original|cpp) entry=([0-9a-f]+) (caller=[^\r\n]+)")


def fixture(item, mode, salt):
    catalog = bytearray(0x200)
    struct.pack_into("<I4xQ16xH", catalog, 0x1d8, int(mode != 1), LOOKUP, 0xfffe)
    desc = bytes(0x200)
    return {
        BASE + 0x4ab43c0: struct.pack("<Q", POINTER),
        BASE + 0x4ac0428: struct.pack("<Q", DESC_POINTER),
        ITEM + salt * 8: struct.pack("<Q", item),
        POINTER: struct.pack("<Q", 0 if mode == 0 else OWNER),
        OWNER + 0x57e58: struct.pack("<Q", CATALOG),
        CATALOG: bytes(catalog),
        LOOKUP: struct.pack("<Q", 0x27000 if mode == 3 else 0),
        DESC_POINTER: struct.pack("<Q", DESCRIPTOR),
        DESCRIPTOR: desc,
    }


def live_cases(path, expected_route):
    result = {entry: Counter() for entry, _ in ENTRIES}
    for route, entry, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        result[int(entry, 16)][state] += 1
    if not all(result.values()):
        raise ValueError(f"{path}: missing item initializer calls at {[hex(e) for e,hits in result.items() if not hits]}")
    print(f"{path}: {[(hex(entry), states.total()) for entry, states in result.items()]}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    sizes = {f.start: f.size for f in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected item initializer boundaries")
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("empty-item gameplay traces differ")
    cases = [
        (0, mode, 0xfffe, salt) for mode, salt in itertools.product(range(4), range(16))
    ] + [
        (1, 0, (bits << 16) | id_, salt)
        for id_, bits, salt in itertools.product((0xfffd, 0xfffe), (0, 1, 0xffff, 0x800000000000), range(16))
    ]
    lines = []
    for entry, mode, item, salt in cases:
        memory = fixture(item, mode, salt)
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{entry} {BASE} {ITEM + salt * 8} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    count = [0, 0]
    fallbacks = 0
    for (index, mode, item, salt), line in zip(cases, native):
        entry, size = ENTRIES[index]
        code = bytes(module.image[entry:entry + size])
        memory = fixture(item, mode, salt)
        address = ITEM + salt * 8
        regs = {UC_ARM64_REG_X0: address}
        outputs = (UC_ARM64_REG_X0,)
        callees = {}
        if index == 0:
            callees = {
                BASE + 0x267d6a8: (BASE + 0x267f248, bytes(module.image[0x267f248:0x267f7f0])),
            }
            for site in (0x267d4d4, 0x267d530, 0x267d55c, 0x267d618, 0x267d654, 0x267d664):
                callees[BASE + site] = (BASE + 0x267d6c4, bytes(module.image[0x267d6c4:0x267d7b4]))
        first = run(BASE + entry, code, regs, outputs, memory, callees=callees)
        if first != run(BASE + entry, code, regs, outputs, memory, callees=callees):
            raise SystemExit(f"unstable original initializer at {entry:#x}, item {item:#x}")
        if first.registers[UC_ARM64_REG_X0] != address or int.from_bytes(first.memory[address], "little") != item:
            raise SystemExit(f"unexpected original shovel sentinel state at {entry:#x}")
        actual = tuple(map(int, line.split()))
        if mode == 3:
            if not first.writes or actual != (0, 0, item):
                raise SystemExit("catalog entry with metadata must use original initializer")
            fallbacks += 1
        else:
            if first.writes or actual != (1, int(index == 0), item):
                raise SystemExit(f"empty item route differs at {entry:#x}, item {item:#x}")
            count[index] += 1
    for (entry, _), count_for_entry in zip(ENTRIES, count):
        print(f"{entry:#x}: {count_for_entry} independent empty-item cases match both AArch64 runs")
    print(f"{fallbacks} catalog cases stayed on the original initializer route")


if __name__ == "__main__":
    main()
