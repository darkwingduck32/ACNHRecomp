"""Portable cold and warm terrain property cache checks."""
import argparse
from collections import Counter
from dataclasses import dataclass
import itertools
from pathlib import Path
import re
import struct
import subprocess

from recomp import ehframe, nso
from tests.acnh_field_record_replay import decode_reads
from tests.function_replay import run
from tools.acnh_version import build_id
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

BUILD = "FF1D1C05670DB6021C85B624A710B963"
ENTRIES = (0x880630, 0x881090)
SIZES = (0x19c, 0x1b0)
BASE, TABLE, CACHE_OFFSET = 0x4000000000, 0x28000, 0x4bfab80
HEADER, ROW, LIST = 0x29000, 0x29200, 0x2a000
HASHES = (0x58da05ed, 0x3e78dc38)
CALLS = ((0x87c0bc, 0x87c0fc, 0x87c13c, 0x87c17c), (0x87c0ec, 0x87c12c, 0x87c16c, 0x87c1ac))
TRACE = re.compile(
    r"acnh shovel terrain pair (original|cpp) entry=([0-9a-f]+) call=([0-9a-f]+) "
    r"index=([0-9a-f]+) out=([0-9a-f]+) before=([0-9a-f]{120}) after=([0-9a-f]{120}) "
    r"base=([0-9a-f]+) table=([0-9a-f]+) reads=(.*)"
)


@dataclass
class Case:
    mode: int
    index: int
    memory: dict
    base: int = BASE
    table: int = TABLE
    live: tuple | None = None



def fixture(ready, cached, layout, row_type, visible, kind):
    owner = bytearray(0x1cc)
    struct.pack_into("<I", owner, 0x1b8, 1)
    struct.pack_into("<Qi", owner, 0x1c0, LIST, 2)
    header = bytearray(64)
    struct.pack_into("<HB", header, 8, len(visible), layout)
    first_slot = 0x1c if layout else 0xc
    for i, selector in enumerate(visible):
        struct.pack_into("<II", header, first_slot + 8 * i, HASHES[selector], 28 + selector)
    row = bytearray(64)
    struct.pack_into("<I", row, 0, ROW - HEADER if row_type != 1 else 0)
    row[28:30] = bytes(kind)
    return {
        TABLE: bytes(owner), HEADER: bytes(header), ROW: bytes(row),
        LIST: struct.pack("<Q", 0 if row_type == 2 else ROW),
        BASE + CACHE_OFFSET: struct.pack("<B3x14i", ready, *([cached] * 14)),
    }


def cases():
    rows = []
    for mode, ready, cached, visible, row_type, index, kind in itertools.product(
            (0, 1), (0, 1), (-2, -1, 28), ((), (0,), (1,), (0, 1)), (0, 1, 2), (0, 1, 2, 0xffffffff), ((0, 2), (2, 0))):
        rows.append(Case(mode, index, fixture(ready, cached, int(index == 1), row_type, visible, kind)))
    for ready, cached, visible, layout in itertools.product((0, 1), (-2, -1, 28), ((), (0,), (1,), (0, 1)), (0, 1)):
        rows.append(Case(2, 0, fixture(ready, cached, layout, 0, visible, (0, 2))))
    return rows


def live_cases(path, route):
    cases, states = [], []
    sites = set()
    for line in path.read_text(errors="replace").splitlines():
        match = TRACE.fullmatch(line)
        if not match:
            continue
        seen, entry, call, index, output, before, after, base, table, fields = match.groups()
        if seen != route:
            raise ValueError(f"{path}: expected {route}, got {seen}")
        entry, call, index, output, base, table = (int(v, 16) for v in (entry, call, index, output, base, table))
        mode = ENTRIES.index(entry)
        if call not in CALLS[mode]:
            raise ValueError(f"{path}: unexpected caller {call:#x}")
        reads, memory = decode_reads(fields)
        memory[base + CACHE_OFFSET] = bytes.fromhex(before)
        pointers = {0: 0, table: 1}
        normalized = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                           for _, width, value in reads)
        states.append((entry, call, index, output, before, after, normalized))
        cases.append(Case(mode, index, memory, base, table, (output, bytes.fromhex(after))))
        sites.add(call)
    if missing := set(itertools.chain.from_iterable(CALLS)) - sites:
        raise ValueError(f"{path}: missing callers {', '.join(hex(site) for site in sorted(missing))}")
    for previous, current in zip(states, states[1:]):
        if current[0] != ENTRIES[1]:
            continue
        if (previous[0] != ENTRIES[0] or previous[1] + 0x30 != current[1]
            or previous[2] != current[2] or previous[3] or previous[5] != current[4]):
            raise ValueError(f"{path}: requirement call did not consume its paired cache state")
    counts = Counter(state[0] for state in states)
    print(f"{path}: {counts[ENTRIES[0]]} rejection and {counts[ENTRIES[1]]} requirement calls", flush=True)
    return states, cases


def unique_captures(cases):
    unique = {}
    for case in cases:
        key = (case.mode, case.index, case.base, case.table, case.live, tuple(sorted(case.memory.items())))
        unique.setdefault(key, case)
    print(f"{len(unique)} distinct captured inputs across {len(cases)} gameplay calls", flush=True)
    return list(unique.values())


def payload(case):
    values = [(address + offset, value) for address, data in case.memory.items() for offset, value in enumerate(data)]
    header = [case.mode, case.base, case.table, case.index, len(values)]
    return " ".join(map(str, (*header, *(value for pair in values for value in pair))))


def aarch64(module, case):
    memory = case.memory
    results, writes = [], []
    for family in ((0, 1) if case.mode == 2 else (case.mode,)):
        entry, size = ENTRIES[family], SIZES[family]
        args = (case.base + entry, bytes(module.image[entry:entry+size]),
                {UC_ARM64_REG_X0: case.table, UC_ARM64_REG_X1: case.index}, (UC_ARM64_REG_X0,), memory)
        expected = run(*args)
        if expected != run(*args):
            raise ValueError("unstable original")
        results.append(expected.registers[UC_ARM64_REG_X0])
        writes.extend(expected.writes)
        memory = expected.memory
    if case.live and case.live != (results[0], memory[case.base + CACHE_OFFSET]):
        raise ValueError("AArch64 output or cache differs from the gameplay capture")
    return (*results, *(word for write in writes for word in write))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--no-op", action="store_true")
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    extents = {(fn.start, fn.size) for fn in ehframe.parse(module)}
    if any(pair not in extents for pair in zip(ENTRIES, SIZES)):
        parser.error("unexpected terrain property boundaries")
    rows = cases()
    if args.live:
        original, captured = live_cases(args.live[0], "original")
        native, _ = live_cases(args.live[1], "cpp")
        if original != native:
            raise ValueError("terrain call order, outcomes, or cache states differ")
        if not args.capture:
            rows.extend(unique_captures(captured))
    if args.capture:
        _, captured = live_cases(args.capture, "original")
        rows.extend(unique_captures(captured))
    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    completed = subprocess.run(command, text=True, capture_output=True, timeout=30,
                               input="\n".join(map(payload, rows)) + "\n", check=True)
    outputs = completed.stdout.splitlines()
    if len(outputs) != len(rows):
        raise ValueError("candidate returned the wrong number of cases")
    counts = Counter()
    for i, (case, output) in enumerate(zip(rows, outputs, strict=True)):
        expected = aarch64(module, case)
        actual = tuple(map(int, output.split()))
        if expected != actual:
            raise SystemExit(f"case {i}: original {expected}; C++ {actual}")
        counts[case.mode] += 1
    print(f"{counts[0]} independent rejection cases, {counts[1]} independent requirement cases, "
          f"{counts[2]} paired cache cases match both original AArch64 runs and ordered writes")


if __name__ == "__main__":
    main()
