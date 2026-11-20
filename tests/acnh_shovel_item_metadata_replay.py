"""Replay the shovel gather's item metadata classification."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x19ee3b4, 0x404
DESCRIPTOR, OWNER_SLOT, OWNER, CATALOG, ENTRIES, HEADER, EXT_HEADER = (
    0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x27000, 0x2b000
)


def table(valid):
    result = bytearray(0x80)
    if valid:
        # The row is one stride after the first packed row.
        struct.pack_into("<IIHB", result, 0, 4, 32, 1, 0)
        struct.pack_into("<I", result, 0x34, 0x34)
    return bytes(result)


def fixture(source, flags, mode, enum_type, direct, extra_code, extended):
    descriptor = struct.pack("<QHBBIQ", 0, 7, flags, 0, 0,
                             0 if source == 0 else HEADER + 0x34)
    catalog = bytearray(0x200)
    struct.pack_into("<I4xQ16xH", catalog, 0x1d8, int(mode & 8 != 0), ENTRIES, 7)
    memory = {
        DESCRIPTOR: descriptor,
        BASE + 0x4ab43c0: struct.pack("<Q", OWNER_SLOT),
        OWNER_SLOT: struct.pack("<Q", OWNER),
        OWNER + 0x57e58: struct.pack("<Q", CATALOG),
        CATALOG: bytes(catalog),
        ENTRIES: struct.pack("<Q", HEADER + 0x34),
        HEADER: table(source == 1),
        EXT_HEADER: table(bool(mode & 16)),
    }
    for address, values, code in ((0x5226ec0, 0x30000, enum_type),
                                  (0x5226e60, 0x31000, direct),
                                  (0x5226af0, 0x32000, extra_code),
                                  (0x5226b70, 0x33000, extended)):
        memory[BASE + address] = struct.pack("<I4xQ", 2, values)
        memory[values] = struct.pack("<HH", 0, code)
    return memory


def dependencies(mode):
    def immediate(value):
        return words(0x52800000 | (value << 5), RETURN)
    calls = {
        0x19ee3e0: (0x267d6c4, immediate(7)),
        0x19ee424: (0x19f93e8, immediate(10 if mode & 1 else 0)),
        0x19ee718: (0x19da7a0, words(*address_words(0, EXT_HEADER + 0x34 if mode & 4 else 0), RETURN)),
    }
    for site in (0x19ee440, 0x19ee44c, 0x19ee46c):
        calls[site] = 0x19e59b0, immediate(int(mode & 2 != 0))
    return {BASE + site: (BASE + target, body) for site, (target, body) in calls.items()}


ALTERNATE_ENTRY, ALTERNATE_SIZE = 0x19f93e8, 0x3d4
ALTERNATE_SITE, ALTERNATE_TARGET = 0x19f9760, 0x19f4c7c
ALTERNATE_TABLE, BIT_SLOT, BIT_OWNER = 0x34000, 0x35000, 0x36000


def alternate_fixture(layout, item, bit, category, special, kind):
    offset = 0x44 if layout == 3 else 0x34
    packed = bytearray(table(layout == 1))
    if layout == 3:
        struct.pack_into("<IIHB", packed, 0, 4, 32, 1, 1)
        struct.pack_into("<I", packed, offset, offset)
    if layout == 4:
        struct.pack_into("<IIHB", packed, 0, 1, 32, 1, 0)
        struct.pack_into("<I", packed, offset, offset)
    memory = {
        DESCRIPTOR: struct.pack("<QIIQ", 0, 0, item, HEADER + offset if layout else 0),
        HEADER: bytes(packed),
        BASE + 0x4ac00c8: struct.pack("<Q", ALTERNATE_TABLE),
        BASE + 0x4ab46f0: struct.pack("<Q", BIT_SLOT),
        BIT_SLOT: struct.pack("<Q", BIT_OWNER),
        BIT_OWNER + 0xe99e0 + ((item >> 3) & 0x1fff): bytes([bit << (item & 7)]),
    }
    for address, values, code in ((ALTERNATE_TABLE, 0x37000, category),
                                  (BASE + 0x5227298, 0x38000, special),
                                  (BASE + 0x5226f68, 0x39000, kind)):
        memory[address] = struct.pack("<I4xQ", 2, values)
        memory[values] = struct.pack("<HH", 0, code)
    return memory


def alternate_cases():
    for layout, item_and_bit, fields, callback in itertools.product(
            (0, 1, 2, 3, 4), ((0x123, 0), (0x123, 1), (0x8c37, 1)),
            ((0, 0, 0), (0, 1, 0), (0, 2, 0x17), (1, 0, 0x17),
             (2, 1, 0x17), (0xb1, 1, 0x17), (0xbc, 1, 0xd9),
             (0xbd, 1, 0xda), (0xffff, 1, 0x17)), (0, 1)):
        yield callback, alternate_fixture(layout, *item_and_bit, *fields)


PREDICATE_ENTRY, PREDICATE_SIZE = 0x19e59b0, 0x448
CATEGORY_DB, CATEGORY_HEADER, CATEGORY_ROWS, REGISTRY = 0x50000, 0x51000, 0x52000, 0x53000
CACHE_READY, CACHE_SLOTS, ENABLED_ROWS = 0x54000, 0x55000, 0x56000


def predicate_fixture(source, flags, category, layout, cache, cell):
    packed = bytearray(0x80)
    first = 0x24 if layout == "extended" else 0x14
    struct.pack_into("<IIHB", packed, 0, 2, 24, int(layout != "no-columns"), int(layout == "extended"))
    if layout != "no-columns":
        struct.pack_into("<II", packed, first - 8, 0 if layout == "absent" else 0x90bc0855, cache[2])
    for index in range(2):
        pos = first + 24 * index
        struct.pack_into("<IB", packed, pos, pos, cell)
    db = bytearray(0x1c4)
    if layout in ("packed", "extended", "masked", "no-columns"):
        struct.pack_into("<Q", db, 0xb0, REGISTRY)
        struct.pack_into("<I4xQ", db, 0xc8, 2, ENABLED_ROWS)
    else:
        struct.pack_into("<I4xQi", db, 0x1b0, 1 if layout == "row-zero" else 2, CATEGORY_ROWS, 2)
    return {
        DESCRIPTOR: struct.pack("<QHBBIQ", 0, 7, flags, 0, 0, HEADER + 0x34 if source else 0),
        HEADER: table(source == 1),
        BASE + 0x4ac00c8: struct.pack("<Q", ALTERNATE_TABLE),
        ALTERNATE_TABLE: struct.pack("<I4xQ", 2, 0x37000),
        0x37000: struct.pack("<HH", 0, category),
        BASE + 0x522a778: struct.pack("<Q", CATEGORY_DB),
        CATEGORY_DB: bytes(db),
        CATEGORY_HEADER: bytes(packed),
        CATEGORY_ROWS: struct.pack("<QQ", CATEGORY_HEADER + first,
                                   0 if layout == "null-row" else CATEGORY_HEADER + first + 24),
        REGISTRY: struct.pack("<QQ", 0, CATEGORY_HEADER),
        ENABLED_ROWS: bytes([1, layout != "masked"]),
        BASE + 0x4abfeb0: struct.pack("<QQ", CACHE_READY, CACHE_SLOTS),
        CACHE_READY: bytes([cache[0]]),
        CACHE_SLOTS: struct.pack("<11Ii19I", *([0] * 11), cache[1], *([0] * 19)),
    }


def predicate_cases():
    for source, flags, category, layout, cache, cell in itertools.product(
            (0, 1, 2), (0, 0x20), (0, 1, 2, 0xbd),
            ("indexed", "row-zero", "null-row", "packed", "extended", "masked", "no-columns", "absent"),
            ((0, 0, 4), (1, -1, 4), (1, -2, 4), (1, 4, 4), (0, 0, 0xffffffff)), (0, 1)):
        yield 0, predicate_fixture(source, flags, category, layout, cache, cell)


TRACE = re.compile(r"acnh shovel metadata (original|cpp) item=([0-9a-f]+) row=([01]) out=([0-9a-f]+)")
LEAF_TRACE = re.compile(
    r"acnh shovel metadata leaf (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+) "
    r"item=([0-9a-f]+) row=([01]) out=([0-9a-f]+)"
)
SURVEY = re.compile(r"acnh shovel survey x=(-?\d+) z=(-?\d+) ")


def live_cases(path, expected_route, required_item=None, tile=None, entry=ENTRY):
    result = Counter()
    pending = Counter()
    pattern = TRACE if entry == ENTRY else LEAF_TRACE
    for line in path.read_text(errors="replace").splitlines():
        if match := pattern.search(line):
            if entry == ENTRY:
                route, item, row, outcome = match.groups()
                call = None
            else:
                route, target, call, item, row, outcome = match.groups()
                if int(target, 16) != entry:
                    continue
            if route != expected_route:
                raise ValueError(f"{path}: expected {expected_route}, found {route}")
            key = (int(item, 16), int(row), int(outcome, 16), call)
            (pending if tile is not None else result)[key] += 1
        elif tile is not None and (match := SURVEY.search(line)):
            # Classifications are logged by the gather before the completed survey.
            if tuple(map(int, match.groups())) == tile:
                result.update(pending)
            pending.clear()
    if not result:
        raise ValueError(f"{path}: no {expected_route} metadata classifications")
    if required_item is not None and not any(
        (item & 0xffff) == required_item and row for item, row, _, _ in result
    ):
        raise ValueError(f"{path}: no populated metadata row for item {required_item:#x}")
    print(f"{path}: {result.total()} calls to {entry:#x}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--require-row-item", type=lambda value: int(value, 0), metavar="ID",
                        help="Require a live classification with a populated row for this item ID")
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"),
                        help="Compare classifications whose completed gather targeted this tile")
    candidates = parser.add_mutually_exclusive_group()
    candidates.add_argument("--predicate", action="store_true", help="Replay the descriptor predicate")
    candidates.add_argument("--alternate", action="store_true", help="Replay the alternate descriptor kind")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    entry, size = (ALTERNATE_ENTRY, ALTERNATE_SIZE) if args.alternate else (ENTRY, SIZE)
    if args.predicate:
        entry, size = PREDICATE_ENTRY, PREDICATE_SIZE
    if not any(f.start == entry and f.size == size for f in ehframe.parse(module)):
        parser.error("unexpected shovel metadata bounds")
    if args.require_row_item is not None and (not args.live or not 0 <= args.require_row_item <= 0xffff):
        parser.error("--require-row-item needs --live and a 16-bit item ID")
    if args.survey_tile and not args.live:
        parser.error("--survey-tile needs --live")
    if args.live:
        tile = tuple(args.survey_tile) if args.survey_tile else None
        original = live_cases(args.live[0], "original", args.require_row_item, tile, entry)
        native = live_cases(args.live[1], "cpp", args.require_row_item, tile, entry)
        if original != native:
            raise SystemExit(f"shovel metadata gameplay traces differ: {original - native}, {native - original}")
    if args.predicate:
        cases = list(predicate_cases())
    elif args.alternate:
        cases = list(alternate_cases())
    else:
        cases = [(mode, fixture(source, flags, mode, enum_type, direct, extra_code, extended))
                 for (source, flags, mode), (enum_type, direct, extra_code, extended) in itertools.product(
                     itertools.product((0, 1, 2), (0, 4, 8, 12), (0, 1, 2, 4, 7, 8, 9, 11, 13, 29)),
                     ((0, 2, 0, 0), (0xa4, 15, 0, 0), (0xa6, 18, 0, 0),
                      (0, 0x1c, 0x21, 11), (0xbc, 0x1b, 0x21, 28), (0xbd, 0x15, 0x21, 3)))]
    lines = []
    for mode, memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{BASE} {DESCRIPTOR} {mode} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    command = [str(args.candidate.resolve())]
    if args.predicate:
        command.append("--predicate")
    elif args.alternate:
        command.append("--alternate")
    native = subprocess.run(command, input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[entry:entry + size])
    for i, ((mode, memory), line) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: DESCRIPTOR}
        outputs = (UC_ARM64_REG_X0,)
        callees = dependencies(mode)
        if args.alternate:
            callees = {BASE + ALTERNATE_SITE: (BASE + ALTERNATE_TARGET,
                                               words(0x52800000 | (mode << 5), RETURN))}
        if args.predicate:
            callees = {}
        first = run(BASE + entry, code, regs, outputs, memory, callees=callees)
        if first != run(BASE + entry, code, regs, outputs, memory, callees=callees):
            raise SystemExit(f"unstable original metadata case {i}")
        expected = [first.registers[UC_ARM64_REG_X0]]
        for site, _, argument in first.calls:
            expected.append(site - BASE)
            if args.alternate:
                expected.append(argument)
        if args.predicate:
            expected.extend(value for write in first.writes for value in write)
        actual = list(map(int, line.split()))
        if actual != expected:
            raise SystemExit(f"metadata case {i}, mode {mode}: original {expected}, C++ {actual}")
    print(f"{entry:#x}: {len(cases)} cases matched both original AArch64 runs")


if __name__ == "__main__":
    main()
