"""Replay the three shovel item queries independently with explicit catalog fixtures."""
import argparse
from collections import Counter
import hashlib
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
ITEM, PRIMARY_SLOT, ALT_SLOT, META_SLOT, PRIMARY, ALT_OWNER, ALT, META, LOOKUP, READY, TAP = (
    0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000, 0x26000, 0x27000, 0x28000, 0x29000, 0x32000
)
ENTRIES = ((0x267da10, 0x144), (0x267e024, 0x134), (0x127d05c, 0x2a8))
RET = 0xd65f03c0


def fixture(raw_id, flag, route):
    primary = bytearray(0x490)
    for offset in (0xb0, 0x488, 0x2b0):
        struct.pack_into("<Q", primary, offset, READY if route == "primary" else 0)
    def catalog(addr, count):
        struct.pack_into("<I4xQ16xH", addr, 0x1d8, count, LOOKUP, raw_id)
    catalog(primary, 1)
    alternate = bytearray(0x200)
    catalog(alternate, 1 if route.startswith("alternate") else 0)
    metadata = bytearray(0x200)
    catalog(metadata, int(route == "alternate-metadata"))
    return {
        BASE + 0x4abbc68: struct.pack("<Q", PRIMARY_SLOT),
        BASE + 0x4ab43c0: struct.pack("<Q", ALT_SLOT),
        BASE + 0x4ac0428: struct.pack("<Q", META_SLOT),
        BASE + 0x4ac35c0: struct.pack("<Q", 0x90000),
        BASE + 0x5050358: struct.pack("<Q", PRIMARY),
        ITEM: struct.pack("<HBBI", raw_id, flag, 0, 0x89abcdef),
        PRIMARY_SLOT: struct.pack("<Q", PRIMARY),
        ALT_SLOT: struct.pack("<Q", 0 if route == "off" else ALT_OWNER),
        META_SLOT: struct.pack("<Q", META),
        PRIMARY: bytes(primary),
        ALT_OWNER + 0x57e58: struct.pack("<Q", ALT),
        ALT: bytes(alternate),
        META: bytes(metadata),
        LOOKUP: struct.pack("<Q", 0x2b000),
        READY: struct.pack("<QQ", 0, 1),
        TAP: bytes(16),
    }


def words(*instructions):
    return struct.pack(f"<{len(instructions)}I", *instructions)


def imm(number):
    return 0x52800000 | (number << 5)


def dependencies(number, mode, category):
    if number == 0:
        id_sites = (0x267da88, 0x267dac0, 0x267db2c)
    elif number == 1:
        id_sites = (0x267e04c, 0x267e0c0, 0x267e0ec)
    else:
        id_sites = (0x127d0a0, 0x127d100, 0x127d14c, 0x127d1d8, 0x127d2e8)
    routes = {site: (0x267d6c4, words(0x79400000, RET)) for site in id_sites}
    for site in ((0x267e11c,) if number == 1 else (0x127d17c,) if number == 2 else ()):
        routes[site] = (0x19e59b0, words(
            0xd284000a, 0xf2a0006a, 0x79401008, 0xf9400809, 0xb9000148, 0xf9000549,
            imm(int(mode != 2)), RET))
    if number == 2:
        routes.update({
            0x127d074: (0x267da10, words(imm(int(mode != 0)), RET)),
            0x127d080: (0x267e024, words(imm(int(mode == 1)), RET)),
            0x127d188: (0x19e6020, words(imm(int(mode != 3)), RET)),
            0x127d208: (0x114c030, words(0xf9400028, 0xf9000408, 0xf900081f, RET)),
            0x127d210: (0x114dcb0, words(imm(int(mode == 4)), RET)),
            0x127d220: (0x113d890, words(imm(int(mode == 5)), RET)),
        })
        for site in (0x127d238, 0x127d250, 0x127d264, 0x127d27c, 0x127d294, 0x127d2a8, 0x127d2c0):
            routes[site] = (0x113db50, words(imm(category), RET))
    return {BASE + site: (BASE + target, body) for site, (target, body) in routes.items()}


def expected(result):
    result_fields = [result.registers[UC_ARM64_REG_X0]]
    for _, target, arg in result.calls:
        entry = target - BASE
        result_fields.extend((entry, arg if arg == ITEM else 0))
        if entry == 0x19e59b0:
            result_fields.extend(value for _, _, value in result.writes)
    return result_fields


LIVE = re.compile(
    r"acnh shovel item class (original|cpp) entry=([0-9a-f]+) "
    r"(caller=[0-9a-f]+ before=[0-9a-f]+ after=[0-9a-f]+ out=[01])"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    result = {entry: Counter() for entry, _ in ENTRIES}
    for route, entry, detail in LIVE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        result[int(entry, 16)][detail] += 1
    for entry, states in result.items():
        if not states:
            raise ValueError(f"{path}: no {expected_route} item classification calls at {entry:#x}")
    print(f"{path}: {[(hex(entry), states.total()) for entry, states in result.items()]}, "
          f"SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
        parser.error("unexpected shovel item query boundary")
    if args.live:
        original = live_cases(args.live[0], "original")
        native = live_cases(args.live[1], "cpp")
        for entry in original:
            if original[entry] != native[entry]:
                raise SystemExit(f"{entry:#x} live mismatch. Original-only {original[entry] - native[entry]}. "
                                 f"C++ only {native[entry] - original[entry]}")
    layout = ("off", "empty", "primary", "alternate", "alternate-metadata")
    cases = [(number, mode, category, fixture(raw_id, flag, route)) for number, mode, category, raw_id, flag, route
             in itertools.chain(
                 ((0, 2, 0, raw_id, 0, route) for raw_id, route in itertools.product(
                     (0xfffd, 0xfffe, 0xffff, 0xf618, 0xf619, 0xf61f, 0xf620, 0x10), layout)),
                 ((1, mode, 0, 0x10, flag, route) for mode, flag, route in itertools.product(
                     (2, 3), (0, 4, 0x84), layout)),
                 ((2, mode, category, 0x10, 4, route) for mode, category, route in itertools.product(
                     range(7), (0, 0xb, 0x11, 0x12, 0x17, 0x18, 0x1e, 0x1f, 0x23, 0x2b, 0x2c, 0x2d), layout)),
             )]
    lines = []
    for number, mode, category, memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{number} {mode} {category} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    count = [0] * len(ENTRIES)
    for i, ((number, mode, category, memory), line) in enumerate(zip(cases, native)):
        entry, size = ENTRIES[number]
        code = bytes(module.image[entry:entry + size])
        regs, outputs = {UC_ARM64_REG_X0: ITEM}, (UC_ARM64_REG_X0,)
        first = run(BASE + entry, code, regs, outputs, memory, callees=dependencies(number, mode, category))
        second = run(BASE + entry, code, regs, outputs, memory, callees=dependencies(number, mode, category))
        if first != second:
            raise SystemExit(f"unstable original query at {entry:#x}, case {i}")
        actual, reference = list(map(int, line.split())), expected(first)
        if actual != reference:
            mismatch = next((j for j, (a, b) in enumerate(zip(actual, reference)) if a != b),
                            min(len(actual), len(reference)))
            raise SystemExit(f"case {i}, entry {entry:#x}, mode {mode}, category {category:#x} at {mismatch}: "
                             f"original {reference[mismatch:mismatch+7]}, C++ {actual[mismatch:mismatch+7]}")
        count[number] += 1
    for (entry, _), cases in zip(ENTRIES, count):
        print(f"{entry:#x}: {cases} independent cases match both AArch64 runs")


if __name__ == "__main__":
    main()
