"""Replay the pinned tile-record bit query."""
import argparse
import hashlib
import itertools
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_field_record_replay import decode_reads
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x1218220, 0xE8
QUERY, MANAGER, NODE, GRID, RECORDS = (0x20000, 0x21000, 0x22000, 0x23000, 0x24000)
TRACE = re.compile(r"acnh tile record flag (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
                   r"query=([0-9a-f]+) w0=([0-9a-f]+) reads=([0-9a-f:,]+)")


def fixtures():
    for x, z, secondary, count, fallback in itertools.product(
            (-4, -2, 0, 2, 5), (-3, -1, 0, 3), (0, 1), (0, 4, 16), (0, 2)):
        grid = bytearray(0xa8)
        struct.pack_into("<II", grid, 0x18, 2, 2)
        struct.pack_into("<iiI", grid, 0x28, -2, -1, count)
        struct.pack_into("<Q", grid, 0x38, RECORDS)
        struct.pack_into("<B", grid, 0x88, secondary)
        struct.pack_into("<I", grid, 0x8c, 4)
        struct.pack_into("<iiII", grid, 0x98, 0, 0, 3, 2)
        records = bytearray(16 * 0x68)
        for i in (0, 2, 5, 7):
            records[i * 0x68 + 0xc] = 2
        yield BASE, QUERY, {
            BASE + 0x509D040: struct.pack("<Q", MANAGER),
            BASE + 0x50A50FC: bytes([fallback]),
            QUERY: struct.pack("<Qii", 0, x, z),
            MANAGER + 0x1F8: struct.pack("<Q", NODE),
            NODE + 0xB0: struct.pack("<Q", GRID),
            GRID: bytes(grid),
            RECORDS: bytes(records),
        }, None
    yield BASE, QUERY, {BASE + 0x509D040: struct.pack("<Q", MANAGER),
                        MANAGER + 0x1F8: bytes(8)}, None
    yield BASE, QUERY, {BASE + 0x509D040: struct.pack("<Q", MANAGER),
                        MANAGER + 0x1F8: struct.pack("<Q", NODE), NODE + 0xB0: bytes(8)}, None


def live_cases(path, expected_route):
    raw = path.read_bytes()
    states = set()
    cases = []
    for route, caller, base, query, output, fields in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        reads, memory = decode_reads(fields)
        base, query, output = (int(value, 16) for value in (base, query, output))
        pointers = {0: 0, query: 1}
        fields = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                       for _, width, value in reads)
        states.add((int(caller, 16), fields, output))
        cases.append((base, query, memory, output))
    if not cases:
        raise ValueError(f"{path}: no tile record flag calls")
    print(f"{path}: {len(cases)} calls, {len(states)} states, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


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
        parser.error("unexpected function boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = list(fixtures())
    if args.live:
        original_states, original = live_cases(args.live[0], "original")
        replacement_states, replacement = live_cases(args.live[1], "cpp")
        if original_states != replacement_states or len(original) != len(replacement):
            raise SystemExit("live states, counts or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for base, query, memory, _ in cases:
        values = [(address + i, value) for address, data in memory.items() for i, value in enumerate(data)]
        lines.append(f"{base} {query} {len(values)} " + " ".join(f"{a} {b}" for a, b in values))
    proc = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                          capture_output=True, text=True, check=True, timeout=30)
    actual = proc.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    for i, (base, query, memory, observed) in enumerate(cases):
        regs = {UC_ARM64_REG_X0: query}
        want = run(base + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory)
        if want != run(base + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory):
            raise SystemExit(f"reference changed on repeat in case {i}")
        if want.writes or want.memory != memory:
            raise SystemExit(f"reference changed guest memory in case {i}")
        result = want.registers[UC_ARM64_REG_X0]
        got = int(actual[i])
        if result != got or (observed is not None and observed != result):
            raise SystemExit(f"case {i}: expected {result}, got {got}, observed {observed}")
    print(f"{len(cases)} cases match two reference runs")


if __name__ == "__main__":
    main()
