"""Compare the 25-slot column lookup and its fixed-slot bit-query sibling."""
import argparse
import collections
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
ROUTES = ((0x1146470, 0xfc), (0x113d890, 0x148))
HEADER, ROW, READY, SLOTS = 0x22000, 0x22200, 0x25000, 0x26000
HASH = 0x623dc307


def fixtures():
    for mode, init, cached, variant, match, index in itertools.product(
            (0, 1), (0, 1), (-2, -1, 0, 12), (0, 1), (0, 1, 2), (0, 3, 24, 25, 0xffffffff)):
        header = bytearray(0x40)
        struct.pack_into("<HB", header, 8, 2, variant)
        start = 0x1c if variant else 0xc
        struct.pack_into("<IIII", header, start, HASH ^ 1, 12, HASH, 0xffffffff if match == 2 else 28)
        row = bytearray([0b10101010] * 0x40)
        struct.pack_into("<I", row, 0, ROW - HEADER)
        if match == 0:
            struct.pack_into("<I", header, start + 8, HASH ^ 2)
        memory = {
            BASE + 0x4abbc70: struct.pack("<QQ", READY, SLOTS), READY: bytes([init]),
            SLOTS: struct.pack("<25i", *([cached] * 25)), HEADER: bytes(header), ROW: bytes(row),
        }
        yield mode, BASE, ROW, index, HASH, memory
    yield 0, BASE, ROW, 0, HASH, {ROW: bytes(4)}
    yield 1, BASE, 0, 0, HASH, {}
    yield 1, BASE, ROW, 0, HASH, {ROW: bytes(4)}


TRACE = re.compile(
    r"acnh extended field (original|cpp) entry=([0-9a-f]+) caller=([0-9a-f]+) "
    r"base=([0-9a-f]+) row=([0-9a-f]+) index=([0-9a-f]+) hash=([0-9a-f]+) "
    r"ready=([0-9a-f]+) slots=([0-9a-f]+) initial=([0-9a-f]+) final=([0-9a-f]+) "
    r"before=([0-9a-f]{200}) after=([0-9a-f]{200}) out=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    counts = collections.Counter()
    for route, entry, caller, base, row, index, hash_value, ready, slots, initial, final, before, after, out, fields in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        entry, caller, base, row, index, hash_value, ready, slots, initial, final, out = (
            int(value, 16) for value in (entry, caller, base, row, index, hash_value, ready, slots, initial, final, out)
        )
        mode = next(i for i, (offset, _) in enumerate(ROUTES) if entry == offset)
        reads, memory = decode_reads(fields)
        memory[ready] = bytes([initial])
        memory[slots] = bytes.fromhex(before)
        observed = out, ready, bytes([final]), slots, bytes.fromhex(after)
        cases.append(((mode, base, row, index, hash_value, memory), observed))
        pointers = {0: 0, row: 1}
        normalized = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                           for _, width, value in reads)
        result = (bool(out), out - row if out else 0) if mode == 0 else out
        states[(mode, caller, index, hash_value if mode == 0 else 0, initial, final, before, after, result, normalized)] += 1
        counts[mode] += 1
    if sorted(counts) != [0, 1]:
        raise ValueError(f"{path}: both extended field entries must appear, got {dict(counts)}")
    print(f"{path}: {dict(counts)} calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
    ranges = {f.start: f.size for f in ehframe.parse(module)}
    if any(ranges.get(entry) != size for entry, size in ROUTES):
        parser.error("unexpected function boundary")
    cases = [(case, None) for case in fixtures()]
    if args.live:
        original_states, original = live_cases(args.live[0], "original")
        replacement_states, replacement = live_cases(args.live[1], "cpp")
        if original_states != replacement_states:
            raise SystemExit("live states, counts or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for (mode, base, row, index, hash_value, memory), _ in cases:
        values = [(address+i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{mode} {base} {row} {index} {hash_value} {len(values)} " +
                     " ".join(f"{a} {v}" for a, v in values))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines)+"\n",
                            capture_output=True, text=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    failures, counts = {}, collections.Counter()
    for i, ((mode, base, row, index, hash_value, memory), observed) in enumerate(cases):
        entry, size = ROUTES[mode]
        regs = {UC_ARM64_REG_X0: row, UC_ARM64_REG_X1: index, UC_ARM64_REG_X2: hash_value}
        code = bytes(module.image[entry:entry + size])
        want = run(base + entry, code, regs, (UC_ARM64_REG_X0,), memory)
        if want != run(base + entry, code, regs, (UC_ARM64_REG_X0,), memory):
            raise SystemExit(f"reference changed on repeat in case {i}")
        expected = (want.registers[UC_ARM64_REG_X0], *(v for event in want.writes for v in event))
        if observed is not None:
            out, ready, flag, slots, offsets = observed
            if (out != want.registers[UC_ARM64_REG_X0] or want.memory.get(ready) != flag
                    or want.memory.get(slots) != offsets):
                raise SystemExit(f"live case {i} differs from Unicorn")
        actual = tuple(map(int, native[i].split()))
        if actual != expected:
            failures.setdefault(mode, f"entry {entry:#x}, case {i}: expected {expected}, got {actual}")
        counts[mode] += 1
    if failures:
        raise SystemExit("\n".join(failures.values()))
    for mode, count in counts.items():
        print(f"entry {ROUTES[mode][0]:#x}: {count} cases match two reference runs")


if __name__ == "__main__":
    main()
