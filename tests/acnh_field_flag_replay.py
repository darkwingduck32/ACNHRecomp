"""Compare the field-flag neighborhood query and replay captured inputs."""
import argparse
import hashlib
import random
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_S0, UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.acnh_field_record_replay import decode_reads
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x15E0E30, 0x1E4
TABLE, GOT, STEPS = 0x4099798, 0x4AB27C8, 0x5140230
POSITION, SINGLETON, MANAGER, GRID, CELLS = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def fixture(table, x, z, radius, flags, marked, *, manager=True, count=36, stride=6):
    state = bytearray(0x78)
    struct.pack_into("<II", state, 0x38, stride, 6)
    struct.pack_into("<I", state, 0x68, count)
    struct.pack_into("<Q", state, 0x70, CELLS)
    cells = [8 if i in marked else (1 if i % 2 else 0) for i in range(36)]
    memory = {BASE + TABLE: table, BASE + GOT: struct.pack("<Q", SINGLETON),
              BASE + STEPS: struct.pack("<II", 1 << 29, 1 << 28),
              POSITION: struct.pack("<fff", x, 0, z), SINGLETON: struct.pack("<Q", MANAGER if manager else 0),
              MANAGER + 0xA8: struct.pack("<Q", GRID), GRID: bytes(state),
              CELLS: struct.pack("<36I", *cells)}
    return POSITION, bits(radius), flags, memory


def inputs(table):
    yield fixture(table, 2.5, 2.5, 0, 0, {0})
    yield fixture(table, 2.5, 2.5, 0, 0, {1})
    yield fixture(table, 2.5, 2.5, 5, 0, {1})
    yield fixture(table, 2.5, 2.5, 5, 1, {0}, manager=False)
    yield fixture(table, 2.5, 2.5, 5, 1, {0}, count=0)
    rng = random.Random(303)
    edges = [-10, -0.01, -0.0, 0, 4.999999, 5, 10, 24.999998, 25, 30, 1e10]
    for _ in range(512):
        x, z = (rng.choice(edges) if rng.random() > 0.5 else rng.uniform(-15, 45) for _ in range(2))
        radius = rng.choice([0, 2**-24, 2**-23, -1, -5, 1, 5, 20, rng.uniform(-30, 30)])
        marked = {i for i in range(36) if rng.randrange(10) == 0}
        yield fixture(table, x, z, radius, rng.randrange(4), marked)


TRACE = re.compile(
    r"acnh field flag (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"position=([0-9a-f]+) s0=([0-9a-f]{8}) flags=([0-9a-f]+) "
    r"w0=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    shapes = set()
    captures = []
    for match in TRACE.finditer(raw.decode("utf-8", errors="replace")):
        route, *state, raw_reads = match.groups()
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, saw {route}")
        caller, base, position, radius, flags, output = (int(word, 16) for word in state)
        reads, memory = decode_reads(raw_reads)
        pointers = {0: 0, base: 1, position: 2}
        fields = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                       for _, width, value in reads)
        shapes.add((caller, radius, flags, fields, output))
        captures.append((base, position, radius, flags, memory, output))
    if not captures:
        raise ValueError(f"{path}: no {expected_route} field flag calls")
    print(f"{path}: {len(captures)} calls, {len(shapes)} normalized states; "
          f"SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return shapes, captures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, metavar=("ORIGINAL_LOG", "CPP_LOG"), type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module build {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("function boundary is absent from the module's unwind table")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    table = bytes(module.image[TABLE:TABLE + 256 * 16])
    cases = [(BASE, *case, None) for case in inputs(table)]
    if args.live:
        original_shapes, original = live_cases(args.live[0], "original")
        replacement_shapes, replacement = live_cases(args.live[1], "cpp")
        if original_shapes != replacement_shapes:
            raise SystemExit("original and C++ traces differ in results or normalized states")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for base, position, radius, flags, memory, _ in cases:
        values = [(address + offset, byte) for address, data in memory.items()
                  for offset, byte in enumerate(data)]
        lines.append(f"{base} {position} {radius} {flags} {len(values)} " +
                     " ".join(f"{a} {b}" for a, b in values))
    candidate = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                               capture_output=True, check=True, timeout=30)
    actual = candidate.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    for i, (base, position, radius, flags, memory, observed) in enumerate(cases):
        registers = {UC_ARM64_REG_X0: position, UC_ARM64_REG_S0: radius, UC_ARM64_REG_X1: flags}
        original = run(base + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory)
        repeat = run(base + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory)
        if original != repeat or original.memory != memory or original.written_bytes:
            raise SystemExit(f"unexpected reference effects in case {i}")
        expected, got = original.registers[UC_ARM64_REG_X0], int(actual[i])
        if observed is not None and observed != expected:
            raise SystemExit(f"live case {i} disagrees with Unicorn")
        if got != expected:
            raise SystemExit(f"case {i}, s0={radius:08x}, w1={flags:#x}: expected {expected}, got {got}")
    print(f"{len(cases)} comparisons passed twice")


if __name__ == "__main__":
    main()
