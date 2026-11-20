"""Compare the field column lookup and its guest cache writes."""
import argparse
import hashlib
import itertools
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.acnh_field_record_replay import decode_reads
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x8823A0, 0xEC
HEADER, ROW = 0x22000, 0x22200
CACHE_OFFSET = 0x4BFAB80
CACHE = BASE + CACHE_OFFSET
HASH = 0x52CC9C37


def fixtures():
    for init, cached, column, variant, search in itertools.product(
        (0, 1), (-2, -1, 0, 12), (0, 1, 13, 14, 0xffffffff), (0, 1), (0, 1, 2, 3)
    ):
        cache = bytearray(struct.pack("<B3x14i", init, *([cached] * 14)))
        header = bytearray(0x40)
        struct.pack_into("<HB", header, 8, 0 if search == 3 else 2, variant)
        fields = 0x1c if variant else 0xc
        struct.pack_into("<IIII", header, fields, HASH ^ 1, 12, HASH, 0xffffffff if search == 1 else 28)
        yield (column, HASH if search != 2 else 0x22,
               {CACHE: bytes(cache), HEADER: bytes(header), ROW: struct.pack("<I", ROW - HEADER)})
    yield 0, HASH, {ROW: bytes(4)}


TRACE = re.compile(
    r"acnh field column (original|cpp) caller=([0-9a-f]+) parent=([0-9a-f]+) row=([0-9a-f]+) "
    r"column=([0-9a-f]+) hash=([0-9a-f]+) cache=([0-9a-f]+) out=([0-9a-f]+) "
    r"before=([0-9a-f]{120}) after=([0-9a-f]{120}) reads=([0-9a-f:,]+)"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    shapes = set()
    captures = []
    for match in TRACE.finditer(raw.decode("utf-8", errors="replace")):
        route, *state, before, after, raw_reads = match.groups()
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, saw {route}")
        caller, parent, row, column, hash_value, cache, returned = (int(word, 16) for word in state)
        reads, memory = decode_reads(raw_reads)
        before, after = bytes.fromhex(before), bytes.fromhex(after)
        memory[cache] = before
        outcome = (True, returned - row) if returned else (False, 0)
        shapes.add((caller, parent, column, hash_value, before, after,
                    tuple((width, value) for _, width, value in reads), outcome))
        captures.append((row, column, hash_value, cache, memory, (returned, after)))
    if not captures:
        raise ValueError(f"{path}: no {expected_route} field column calls")
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
    cases = [(ROW, column, hash_value, CACHE, memory, None)
             for column, hash_value, memory in fixtures()]
    if args.live:
        original_shapes, original = live_cases(args.live[0], "original")
        replacement_shapes, replacement = live_cases(args.live[1], "cpp")
        if original_shapes != replacement_shapes:
            raise SystemExit("original and C++ traces differ in results or normalized states")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for row, column, hash_value, cache, memory, _ in cases:
        values = [(address + offset, byte) for address, block in memory.items()
                  for offset, byte in enumerate(block)]
        lines.append(f"{row} {column} {hash_value} {cache} {len(values)} " +
                     " ".join(f"{a} {b}" for a, b in values))
    proc = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                          capture_output=True, check=True, timeout=30)
    actual = proc.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    for i, (row, column, hash_value, cache, memory, observed) in enumerate(cases):
        registers = {UC_ARM64_REG_X0: row, UC_ARM64_REG_X1: column, UC_ARM64_REG_X2: hash_value}
        entry = cache - CACHE_OFFSET + ENTRY
        original = run(entry, code, registers, (UC_ARM64_REG_X0,), memory)
        repeat = run(entry, code, registers, (UC_ARM64_REG_X0,), memory)
        if original != repeat or any(not cache <= addr < cache + 60 for addr in original.written_bytes):
            raise SystemExit(f"unexpected reference effects in case {i}")
        if any(original.memory[address] != data for address, data in memory.items() if address != cache):
            raise SystemExit(f"reference modified an input record in case {i}")
        footprint = sum(1 << (address - cache) for address in original.written_bytes)
        events = tuple(word for addr, width, value in original.writes for word in (addr - cache, width, value))
        output, cache_after = original.registers[UC_ARM64_REG_X0], original.memory.get(cache, b"")
        if observed is not None and observed != (output, cache_after):
            raise SystemExit(f"live case {i} disagrees with Unicorn")
        expected = output, footprint, *cache_after, *events
        got = tuple(map(int, actual[i].split()))
        if got != expected:
            raise SystemExit(f"case {i}, column {column}, hash {hash_value:#x}:\n"
                             f"expected {expected}\n     got {got}")
    print(f"{len(cases)} comparisons passed twice")


if __name__ == "__main__":
    main()
