"""Compare each of the five record category predicates as its own function."""
import argparse
import collections
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
BASE = 0x4000000000
ENTRIES = {3: 0x23255E4, 4: 0x2325688, 5: 0x232572C, 6: 0x23257D0, 7: 0x2325874}
SIZE = 0xA4
DESCRIPTOR, HEADER, TYPES = BASE + 0x5390120, 0x24000, 0x25000


def fixtures():
    for fmt, index, category in itertools.product((0, 1, 0xff), (0, 1, 2, 4),
                                                   (*range(11), 0xff, 0xffff)):
        prefix = (0x1c if fmt else 0xc) + 16
        row = HEADER + prefix + 4 * index
        types = [(category + slot + 1) & 0xffff for slot in range(4)]
        if index < len(types):
            types[index] = category
        memory = {
            row: struct.pack("<I", row - HEADER),
            HEADER: struct.pack("<IIHB", 4, 4, 2, fmt),
            DESCRIPTOR: struct.pack("<I4xQ", 4, TYPES),
            TYPES: struct.pack("<4H", *types),
        }
        yield row, memory

    yield HEADER, {HEADER: bytes(4)}
    yield HEADER, {HEADER: struct.pack("<I", HEADER)}
    for rows, stride, field_count, cached_count, index in (
        (0, 4, 2, 4, 0), (4, 0, 2, 4, 0), (4, 4, 0, 4, 0), (4, 4, 2, 0, 0), (4, 4, 2, 1, 3)
    ):
        row = HEADER + 0x1c + 4 * index
        yield row, {row: struct.pack("<I", row - HEADER),
                    HEADER: struct.pack("<IIHB", rows, stride, field_count, 0),
                    DESCRIPTOR: struct.pack("<I4xQ", cached_count, TYPES)}


TRACE = re.compile(
    r"acnh record category (original|cpp) entry=([0-9a-f]+) caller=([0-9a-f]+) "
    r"base=([0-9a-f]+) row=([0-9a-f]+) w0=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    category_by_entry = {entry: label for label, entry in ENTRIES.items()}
    states = {label: set() for label in ENTRIES}
    captures = []
    counts = collections.Counter()
    for match in TRACE.finditer(raw.decode("utf-8", errors="replace")):
        route, *state, memory_trace = match.groups()
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, saw {route}")
        entry, caller, base, row, output = (int(word, 16) for word in state)
        label = category_by_entry[entry]
        reads, memory = decode_reads(memory_trace)
        pointers = {0: 0, row: 1}
        fields = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                       for _, width, value in reads)
        states[label].add((caller, fields, output))
        captures.append((base, label, row, memory, output))
        counts[label] += 1
    missing = sorted(set(ENTRIES) - counts.keys())
    if missing:
        raise ValueError(f"{path}: no {expected_route} calls for categories {missing}")
    print(f"{path}: SHA-256 {hashlib.sha256(raw).hexdigest()}")
    for label in ENTRIES:
        print(f"  category {label}: {counts[label]} calls, {len(states[label])} normalized states")
    return {label: (counts[label], states[label]) for label in ENTRIES}, captures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, metavar=("ORIGINAL_LOG", "CPP_LOG"), type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module build {BUILD}")
    module = nso.load(args.module)
    ranges = {f.start: f.size for f in ehframe.parse(module)}
    for label, entry in ENTRIES.items():
        if ranges.get(entry) != SIZE:
            parser.error(f"unexpected unwind range for category {label}")
    cases = [(BASE, label, row, memory, None) for label in ENTRIES for row, memory in fixtures()]
    if args.live:
        original_states, original = live_cases(args.live[0], "original")
        replacement_states, replacement = live_cases(args.live[1], "cpp")
        for label in ENTRIES:
            if original_states[label] != replacement_states[label]:
                raise SystemExit(f"category {label} has different call counts, normalized states, or outcomes")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for base, label, row, memory, _ in cases:
        values = [(addr + i, byte) for addr, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{base} {row} {label} {len(values)} " + " ".join(f"{a} {b}" for a, b in values))
    proc = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                          capture_output=True, check=True, timeout=30)
    actual = proc.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    counts, errors = collections.Counter(), {}
    for i, (base, label, row, memory, observed) in enumerate(cases):
        entry = ENTRIES[label]
        code = bytes(module.image[entry:entry + SIZE])
        registers = {UC_ARM64_REG_X0: row}
        original = run(base + entry, code, registers, (UC_ARM64_REG_X0,), memory)
        repeat = run(base + entry, code, registers, (UC_ARM64_REG_X0,), memory)
        if original != repeat or original.memory != memory or original.written_bytes:
            raise SystemExit(f"unexpected reference effects at entry {entry:#x}, case {i}")
        want, got = original.registers[UC_ARM64_REG_X0], int(actual[i])
        if observed is not None and observed != want:
            errors.setdefault(label, f"live case {i} at entry {entry:#x}: expected {want}, captured {observed}")
        if got != want:
            errors.setdefault(label, f"entry {entry:#x}, case {i}: expected {want}, got {got}")
        counts[label] += 1
    if errors:
        raise SystemExit("\n".join(errors.values()))
    for label, count in counts.items():
        print(f"entry {ENTRIES[label]:#x}, category {label}: {count} cases matched twice")


if __name__ == "__main__":
    main()
