"""Compare action-bit, half-tile, and vector-multiply leaves independently."""
import argparse
import collections
import itertools
import random
import re
import hashlib
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ROUTES = ((0x24642A8, 0x68), (0x1281C54, 0x15C), (0x3DB4ED0, 0x4C))


def inputs():
    for index, flag, bits in itertools.product((0, 1, 30, 31, 32, 53, 54, 55, 0xffffffff),
                                               (0, 1, 2, 3, 0xffffffff), (0, (1 << 64) - 1)):
        yield 0, 0x22000, index, flag, {0x22000: struct.pack("<Q", bits)}

    grid = bytearray(0xa8)
    struct.pack_into("<II", grid, 0x18, 4, 2)
    struct.pack_into("<iiI", grid, 0x28, -2, -1, 12)
    struct.pack_into("<Q", grid, 0x38, 0x24000)
    struct.pack_into("<B", grid, 0x88, 1)
    struct.pack_into("<I", grid, 0x8c, 8)
    struct.pack_into("<iiII", grid, 0x98, 2, 0, 2, 2)
    for x, z, slot in itertools.product(range(-5, 8), range(-3, 4), range(5)):
        records = bytearray(12 * 0x68)
        fallback = bytearray(0x68)
        if slot < 4:
            for offset in range(12):
                struct.pack_into("<hh", records, offset * 0x68 + 0x34 + slot * 16, x, z)
            struct.pack_into("<hh", fallback, 0x34 + slot * 16, x, z)
        yield 1, 0x23000, 0x25000, BASE, {
            0x23000: bytes(grid), 0x24000: bytes(records),
            0x25000: struct.pack("<Qii", 0, x, z), BASE + 0x50a50f0: bytes(fallback),
        }

    rng = random.Random(303)
    for alias, case in itertools.product((0, 1, 2, 3), range(64)):
        values = struct.pack("<9f", *(rng.uniform(-200, 200) for _ in range(9)))
        vector = struct.pack("<3f", *(rng.uniform(-200, 200) for _ in range(3)))
        matrix = 0x26000
        point = 0x26100 if alias != 3 else matrix
        out = (0x26200, point, matrix + 4, matrix)[alias]
        memory = {matrix: values}
        if point != matrix:
            memory[point] = vector
        if out == 0x26200:
            memory[out] = b"\xa5" * 12
        yield 2, out, matrix, point, memory


HALF_TILE_TRACE = re.compile(
    r"acnh half tile (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"grid=([0-9a-f]+) query=([0-9a-f]+) record=([0-9a-f]+) out=([0-9a-f]+) "
    r"g=([0-9a-f]{336}) q=([0-9a-f]{32}) r=([0-9a-f]{208})"
)


def live_half_tiles(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    for route, caller, base, grid, query, record, out, g, q, r in HALF_TILE_TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        base, grid, query, record, out = map(lambda value: int(value, 16), (base, grid, query, record, out))
        grid_bytes, query_bytes, record_bytes = map(bytes.fromhex, (g, q, r))
        memory = {grid: grid_bytes, query: query_bytes, record: record_bytes}
        coords = struct.unpack_from("<ii", query_bytes, 8)
        slots = tuple(struct.unpack_from("<hh", record_bytes, 0x34 + 16*i) for i in range(4))
        states[(caller, coords, slots, out - record if out else None)] += 1
        cases.append(((1, grid, query, base, memory), out))
    if not cases:
        raise ValueError(f"{path}: no half-tile calls")
    print(f"{path}: {len(cases)} half-tile calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live-half-tile", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    ranges = {f.start: f.size for f in ehframe.parse(module)}
    if any(ranges.get(entry) != size for entry, size in ROUTES):
        parser.error("unexpected function boundary")
    cases = [(case, None) for case in inputs()]
    if args.live_half_tile:
        original_states, original = live_half_tiles(args.live_half_tile[0], "original")
        replacement_states, replacement = live_half_tiles(args.live_half_tile[1], "cpp")
        if original_states != replacement_states:
            raise SystemExit("live callers, query coordinates, record slots or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for (mode, a, b, c, memory), _ in cases:
        pairs = [(address+i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{mode} {a} {b} {c} {len(pairs)} " + " ".join(f"{x} {y}" for x, y in pairs))
    actual = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines)+"\n",
                            capture_output=True, text=True, check=True, timeout=30).stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts, failures = collections.Counter(), {}
    for i, ((mode, a, b, c, memory), observed) in enumerate(cases):
        entry, size = ROUTES[mode]
        code = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: a, UC_ARM64_REG_X1: b, UC_ARM64_REG_X2: c}
        address = (c if mode == 1 else BASE) + entry
        want = run(address, code, regs, (UC_ARM64_REG_X0,), memory)
        if want != run(address, code, regs, (UC_ARM64_REG_X0,), memory):
            raise SystemExit(f"reference changed on repeat in case {i}")
        expected = (want.registers[UC_ARM64_REG_X0], *(v for event in want.writes for v in event))
        if observed is not None and expected[0] != observed:
            raise SystemExit(f"live case {i} differs from Unicorn")
        got = tuple(map(int, actual[i].split()))
        if got != expected:
            failures.setdefault(mode, f"entry {entry:#x}, case {i}: expected {expected}, got {got}")
        counts[mode] += 1
    if failures:
        raise SystemExit("\n".join(failures.values()))
    for mode, count in counts.items():
        print(f"entry {ROUTES[mode][0]:#x}: {count} cases match two reference runs")


if __name__ == "__main__":
    main()
