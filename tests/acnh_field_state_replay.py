"""Replay the inline string constructor and gameplay RNG step."""
import argparse
import collections
import hashlib
import itertools
import random
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ROUTES = ((0x1632D80, 0xF4), (0x112B8EC, 0x3C))


def fixtures():
    rng = random.Random(303)
    empty = BASE + 0x3f7ebb1
    for obj, descriptor in itertools.product(
        (0x22000, 0x25001, empty - 0x14, empty - 0x94, empty - 0x114, empty - 0x194),
        (0, 1, BASE + 0x4910000),
    ):
        yield 0, BASE, obj, {obj: rng.randbytes(0x200), BASE + 0x4ab2bc0: struct.pack("<Q", descriptor)}
    edges = (0, 1, 0x80000000, 0xffffffff)
    states = itertools.chain(itertools.product(edges, repeat=4),
                             (tuple(rng.getrandbits(32) for _ in range(4)) for _ in range(256)))
    for state in states:
        yield 1, BASE, 0, {BASE + 0x504cb58: struct.pack("<4I", *state)}


TRACE = re.compile(r"acnh field state (original|cpp) entry=([0-9a-f]+) base=([0-9a-f]+) "
                   r"object=([0-9a-f]+) descriptor=([0-9a-f]+) before=([0-9a-f]+) "
                   r"after=([0-9a-f]+) out=([0-9a-f]+)")


STRING_INPUT = re.compile(r"acnh field state (original|cpp) entry=1632d80 sample=(\d+) "
                          r"base=([0-9a-f]+) object=([0-9a-f]+) descriptor=([0-9a-f]+) before=([0-9a-f]{1024})")
STRING_OUTPUT = re.compile(r"acnh field state result sample=(\d+) after=([0-9a-f]{1024}) out=([0-9a-f]+)")


def live_cases(path, expected_route):
    raw = path.read_bytes()
    counts = collections.Counter()
    cases = []
    log = raw.decode(errors="replace")
    traces = TRACE.findall(log)
    outputs = {sample: (after, out) for sample, after, out in STRING_OUTPUT.findall(log)}
    for route, sample, base, obj, descriptor, before in STRING_INPUT.findall(log):
        after, out = outputs.pop(sample)
        traces.append((route, "1632d80", base, obj, descriptor, before, after, out))
    if outputs:
        raise ValueError(f"{path}: result without matching constructor entry")
    for route, entry, base, obj, descriptor, before, after, out in traces:
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        entry, base, obj, descriptor, out = (int(value, 16) for value in (entry, base, obj, descriptor, out))
        mode = next(i for i, (offset, _) in enumerate(ROUTES) if entry == offset)
        address = obj if mode == 0 else base + 0x504cb58
        memory = {address: bytes.fromhex(before)}
        if mode == 0:
            memory[base + 0x4ab2bc0] = struct.pack("<Q", descriptor)
        cases.append(((mode, base, obj, memory), (address, bytes.fromhex(after), out)))
        counts[mode] += 1
    if sorted(counts) != [0, 1]:
        raise ValueError(f"{path}: both field state entries must appear, got {dict(counts)}")
    print(f"{path}: {dict(counts)} calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return counts, cases


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
        original_counts, original = live_cases(args.live[0], "original")
        replacement_counts, replacement = live_cases(args.live[1], "cpp")
        if original_counts != replacement_counts:
            raise SystemExit("live call counts differ")
        # Allocator contents and RNG seed can differ across game processes.
        # Each exact live snapshot independently has to match the original.
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for (mode, base, obj, memory), _ in cases:
        values = [(address+i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{mode} {base} {obj} {len(values)} " + " ".join(f"{a} {v}" for a, v in values))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines)+"\n",
                            capture_output=True, text=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts, failures = collections.Counter(), {}
    for i, ((mode, base, obj, memory), observed) in enumerate(cases):
        entry, size = ROUTES[mode]
        code = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: obj}
        want = run(base + entry, code, regs, (UC_ARM64_REG_X0,), memory)
        if want != run(base + entry, code, regs, (UC_ARM64_REG_X0,), memory):
            raise SystemExit(f"reference changed on repeat in case {i}")
        expected = (want.registers[UC_ARM64_REG_X0], *(v for event in want.writes for v in event))
        if observed is not None:
            address, after, out = observed
            if want.memory.get(address) != after or expected[0] != out:
                raise SystemExit(f"live case {i} differs from Unicorn")
        actual = tuple(map(int, native[i].split()))
        if actual != expected:
            failures.setdefault(mode, f"entry {entry:#x}, case {i}: first mismatch, expected {expected[:10]}, got {actual[:10]}")
        counts[mode] += 1
    if failures:
        raise SystemExit("\n".join(failures.values()))
    for mode, count in counts.items():
        print(f"entry {ROUTES[mode][0]:#x}: {count} cases match two reference runs")


if __name__ == "__main__":
    main()
