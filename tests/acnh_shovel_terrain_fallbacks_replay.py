"""Replay each quadrant terrain fallback and its field predicate in the original NSO."""

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
from tests.acnh_shovel_target_replay import live_cases as calls_before_survey
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRIES = ((0x87a980, 0xec), (0x87aa70, 0x160), (0x1285040, 0x14c))
POINT, OWNER, NODE, GRID, RECORDS, CONTEXT, STAGE, OBJECT, VTABLE, NAME, STRING, TERRAIN = (
    0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000, 0x26000, 0x27000,
    0x28000, 0x29000, 0x2a000, 0x2b000
)
NAME_TARGET = BASE + 0x10000
PRIMARY_OBJECT, PRIMARY_NAME_SLOT = 0x2e000, 0x2e008


def pointer(target):
    return struct.pack("<Q", target)


def fixture(number, mode, grid_mode, coords):
    owner = bytearray(0x200)
    node = bytearray(0xb8)
    grid = bytearray(0xa8)
    records = bytearray(0x68 * 10)
    fallback = bytearray(0x68)
    struct.pack_into("<Q", owner, 0x1f8, NODE if grid_mode != 0 else 0)
    struct.pack_into("<Q", node, 0xb0, GRID if grid_mode != 1 else 0)
    struct.pack_into("<II", grid, 0x18, 2, 2)
    struct.pack_into("<III4xQ", grid, 0x28, 0, 0, 1 if grid_mode == 4 else 10, RECORDS)
    struct.pack_into("<B3xI8xiiII", grid, 0x88, grid_mode in (3, 4, 5), 4, -2, -2, 2, 2)
    for index in range(10):
        records[index * 0x68 + 0xe] = 0x20 if index % 3 == 1 else 0
        records[index * 0x68 + 0x1e] = 255 if index % 3 == 2 else 6
    fallback[0x1e] = 6 if grid_mode == 5 else 255
    context = bytearray(0x33c)
    struct.pack_into("<QI", context, 0x330, STAGE if mode != 2 else 0, 23)
    stage = bytearray(0x248)
    struct.pack_into("<I", stage, 0x244, mode != 4)
    terrain = bytearray(0x11c9)
    struct.pack_into("<Q", terrain, 0xa8, GRID if mode != 6 else 0)
    terrain[0x11c8] = mode == 7
    return {
        POINT: struct.pack("<Qii", 0, *coords) if number == 2 else struct.pack("<fff", coords[0], 2, coords[1]),
        OWNER: bytes(owner), NODE: bytes(node), GRID: bytes(grid), RECORDS: bytes(records),
        CONTEXT: bytes(context), STAGE: bytes(stage), TERRAIN: bytes(terrain),
        PRIMARY_OBJECT: pointer(VTABLE), PRIMARY_NAME_SLOT: pointer(PRIMARY_OBJECT),
        OBJECT: pointer(VTABLE), VTABLE: bytes(24) + pointer(NAME_TARGET),
        NAME: pointer(STRING), STRING: b"I\0",
        BASE + 0x509d040: pointer(OWNER), BASE + 0x50a50f0: bytes(fallback),
        BASE + 0x5192338: pointer(0 if mode == 0 else 1),
        BASE + 0x59ed670: pointer(0 if mode == 1 else CONTEXT),
        BASE + 0x4ab2640: pointer(PRIMARY_NAME_SLOT),
        BASE + 0x59ed668: pointer(OBJECT),
        BASE + 0x4ab2010: pointer(0x2d000),
        BASE + 0x4bfae30: pointer(0 if mode == 5 else TERRAIN),
    }


def reference(module, number, mode, memory):
    entry, size = ENTRIES[number]
    code = bytes(module.image[entry:entry + size])
    callees = {}
    if number != 2:
        name_site, hash_site, resolve_site, resolve = (
            (0x87a9d4, 0x87a9e0, 0x87aa64, 0x83b9a4) if number == 0 else
            (0x87ab38, 0x87ab44, 0x87abc8, 0x83b18c)
        )
        hash_code = words(*address_words(0, 0 if mode == 3 else 0x758f7eaf), RETURN)
        if mode == 9:
            # Add the upper halfword of the NSO base to the context slot address.
            hash_code = words(*address_words(9, 0x59ed670), 0xf2c00809, 0xf900013f,
                              *address_words(0, 0x758f7eaf), RETURN)
        callees = {
            BASE + name_site: (NAME_TARGET, words(*address_words(0, NAME), RETURN)),
            BASE + hash_site: (BASE + 0x5bd550, hash_code),
            BASE + resolve_site: (BASE + resolve,
                                  words(0xb900003f, 0x52800000 | ((7 if mode == 8 else 9) << 5), RETURN)),
        }
    if number == 1:
        child, length = ENTRIES[2]
        callees[BASE + 0x87aae4] = (BASE + child, bytes(module.image[child:child + length]))
    regs = {UC_ARM64_REG_X0: POINT}
    first = run(BASE + entry, code, regs, tuple(regs), memory, callees=callees)
    second = run(BASE + entry, code, regs, tuple(regs), memory, callees=callees)
    if first != second:
        raise ValueError(f"unstable original function at {entry:#x}")
    calls = [first.registers[UC_ARM64_REG_X0], len(first.calls)]
    for site, _, argument in first.calls:
        calls.append(site - BASE)
        if site - BASE != 0x87aae4:
            calls.append(argument)
    return calls + [value for write in first.writes for value in write]


TRACE = re.compile(
    r"acnh shovel terrain fallback (original|cpp) entry=([0-9a-f]+) "
    r"(call=[0-9a-f]+ .* out=[0-9a-f]+)$"
)
SURVEY = re.compile(r"acnh shovel survey x=(-?\d+) z=(-?\d+) ")


def live_cases(path, entry, expected_route, tile):
    counts = Counter()
    survey_tile = None
    for line in path.read_text(errors="replace").splitlines():
        if match := SURVEY.match(line):
            survey_tile = tuple(map(int, match.groups()))
        elif match := TRACE.fullmatch(line):
            route, target, state = match.groups()
            if int(target, 16) != entry or (tile is not None and survey_tile != tile):
                continue
            if route != expected_route:
                raise ValueError(f"{path}: expected {expected_route} at {entry:#x}, found {route}")
            counts[state] += 1
    if not counts:
        raise ValueError(f"{path}: no {expected_route} calls at {entry:#x}")
    print(f"{path}: {counts.total()} calls to {entry:#x}")
    return counts


SAMPLER_TRACE = re.compile(
    r"acnh shovel sampler terrain fallback (original|cpp) entry=([0-9a-f]+) "
    r"(?:point=[0-9a-f,]+ after=[0-9a-f,]+ )?(call=[0-9a-f]+ .* out=[0-9a-f]+)"
)
SAMPLER_CALLERS = {0x87a980: {"83b0e0", "83ab0c"}, 0x87aa70: {"83ab28"}, 0x1285040: {"87aae4"}}


def compare_sampler_calls(paths, tile, entries):
    original, native = (
        calls_before_survey(path, route, tile, entries, SAMPLER_TRACE)
        for path, route in zip(paths, ("original", "cpp"))
    )
    for entry in entries:
        states = original[entry]
        if any("moved=1" in state for state in (*states, *native[entry])):
            raise SystemExit(f"{entry:#x}: expected the sampler fixture to leave the input position unchanged")
        callers = {state.split()[0].removeprefix("call=") for state in states}
        if callers != SAMPLER_CALLERS[entry]:
            raise SystemExit(f"{entry:#x}: expected call sites {SAMPLER_CALLERS[entry]}, found {callers}")
        if states != native[entry]:
            raise SystemExit(f"{entry:#x}: original only {states - native[entry]}, "
                             f"C++ only {native[entry] - states}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--entry", type=lambda text: int(text, 16), choices=[address for address, _ in ENTRIES])
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--sampler-live", action="store_true")
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    sizes = {f.start: f.size for f in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected terrain fallback boundary")
    if args.sampler_live:
        if not args.live:
            parser.error("--sampler-live requires --live")
        entries = tuple(entry for entry, _ in ENTRIES if args.entry in (None, entry))
        compare_sampler_calls(args.live, tuple(args.survey_tile) if args.survey_tile else None, entries)
    rows = []
    for number, (entry, _) in enumerate(ENTRIES):
        if args.entry not in (None, entry):
            continue
        if args.live and not args.sampler_live:
            tile = tuple(args.survey_tile) if args.survey_tile else None
            original = live_cases(args.live[0], entry, "original", tile)
            native = live_cases(args.live[1], entry, "cpp", tile)
            if original != native:
                raise SystemExit(f"{entry:#x}: original only {original - native}, C++ only {native - original}")
        geometry = ((0, 0), (2, 0), (0, 2), (-1, 0), (-4, -3), (80, 90), (16777217, -16777217))
        if number < 2:
            geometry = ((0, 0), (5, 0), (0, 10), (-0.25, -5), (-9.99, -10.01),
                        (10.24, 15.75), (float("nan"), float("inf")), (1e30, -1e30))
        modes = range(10) if number < 2 else (0,)
        layouts = (2,) if number == 0 else range(6)
        rows.extend((number, mode, fixture(number, mode, layout, point))
                    for mode, layout, point in itertools.product(modes, layouts, geometry))
    lines = []
    for number, mode, memory in rows:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{number} {mode} {len(pairs)} " + " ".join(f"{a} {v}" for a, v in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=60).stdout.splitlines()
    if len(native) != len(rows):
        raise SystemExit("candidate returned the wrong number of cases")
    counts = Counter()
    for index, ((number, mode, memory), line) in enumerate(zip(rows, native)):
        actual = list(map(int, line.split()))
        expected = reference(module, number, mode, memory)
        entry = ENTRIES[number][0]
        if actual != expected:
            raise SystemExit(f"{entry:#x} case {index}: original {expected}, C++ {actual}")
        counts[entry] += 1
    for entry, count in counts.items():
        print(f"{entry:#x}: {count} cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
