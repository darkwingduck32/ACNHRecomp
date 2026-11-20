"""Compare the five observed field-provider helpers with the original AArch64 bodies."""
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
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, OBJECT = 0x4000000000, 0x20000
ROUTES = ((0x11AC1E8, 0x10), (0x11AC290, 8), (0x261157C, 8), (0x2611584, 8), (0x261158C, 0x14))
RESOLVER = bytes.fromhex("001c0091c0035fd6")  # add x0, x0, #7; ret


def fixtures():
    for target in (0, BASE + 0x200000, BASE + 0x300000):
        calls = {BASE + 0x11ac1f0: (target, RESOLVER)} if target else {}
        yield 0, OBJECT, 0, 0, {OBJECT + 8: struct.pack("<Q", target)}, calls
    for mode, obj in itertools.product((1, 2, 3), (0, OBJECT, 0xffffffffffffffff)):
        yield mode, obj, 0, 0, {}, {}
    for start, x, z in itertools.product(
        (0, BASE + 0x800), (0, 1, 0x11f, 0x80000000, 0xffffffff),
        (0, 1, 0xbf, 0x7fffffff, 0xffffffff),
    ):
        yield 4, OBJECT, x, z, {OBJECT + 0x10: struct.pack("<Q", start)}, {}


TRACE = re.compile(
    r"acnh shovel provider method (original|cpp) entry=([0-9a-f]+) caller=([0-9a-f]+) "
    r"base=([0-9a-f]+) object=([0-9a-f]+) x=([0-9a-f]+) z=([0-9a-f]+) "
    r"data=([0-9a-f]+) out=([0-9a-f]+)"
)


def return_word(value):
    # Return the recorded output of the generated resolver.
    code = [0xd2800000 | ((value & 0xffff) << 5)]
    code.extend(0xf2800000 | (part << 21) | (((value >> (16 * part)) & 0xffff) << 5)
                for part in range(1, 4))
    return struct.pack("<5I", *code, 0xd65f03c0)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    cases = []
    counts = collections.Counter()
    states = collections.Counter()
    for route, entry, caller, base, obj, x, z, data, out in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        entry, base, obj, x, z, data, out = (
            int(value, 16) for value in (entry, base, obj, x, z, data, out)
        )
        mode = next(index for index, (offset, _) in enumerate(ROUTES) if entry == offset)
        memory = {}
        callees = {}
        resolved = 0
        if mode == 0:
            memory[obj + 8] = struct.pack("<Q", data)
            if data:
                callees[base + 0x11ac1f0] = data, return_word(out)
                resolved = out
            key = (data - base if data else None, out - base if out else None)
        elif mode == 4:
            memory[obj + 0x10] = struct.pack("<Q", data)
            key = (x, z, (out - data) & 0xffffffffffffffff)
        else:
            key = (out,)
        states[(mode, caller, key)] += 1
        counts[mode] += 1
        cases.append((base, mode, obj, x, z, memory, callees, resolved, out))
    if set(counts) != set(range(len(ROUTES))):
        raise ValueError(f"{path}: missing provider methods, captured {dict(counts)}")
    print(f"{path}: {dict(counts)}, SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
    ranges = {function.start: function.size for function in ehframe.parse(module)}
    if any(ranges.get(entry) != size for entry, size in ROUTES):
        parser.error("unexpected provider method boundary")
    cases = []
    for mode, obj, x, z, memory, callees in fixtures():
        resolved = next(iter(callees.values()))[0] + 7 if callees else 0
        cases.append((BASE, mode, obj, x, z, memory, callees, resolved, None))
    if args.live:
        original_states, original = live_cases(args.live[0], "original")
        replacement_states, replacement = live_cases(args.live[1], "cpp")
        if original_states != replacement_states:
            raise SystemExit("provider method callers, inputs or outputs differ")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for _, mode, obj, x, z, memory, _, resolved, _ in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{mode} {obj} {x} {z} {resolved} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")

    counts, failures = {}, {}
    for i, ((base, mode, obj, x, z, memory, callees, _, observed), line) in enumerate(zip(cases, native)):
        entry, size = ROUTES[mode]
        code = bytes(module.image[entry:entry+size])
        regs = {UC_ARM64_REG_X0: obj, UC_ARM64_REG_X1: x, UC_ARM64_REG_X2: z}
        reference = run(base + entry, code, regs, (UC_ARM64_REG_X0,), memory, callees=callees)
        if reference != run(base + entry, code, regs, (UC_ARM64_REG_X0,), memory, callees=callees) or reference.writes:
            raise SystemExit(f"unexpected original effects in case {i}")
        expected = [reference.registers[UC_ARM64_REG_X0], *(target for _, target, _ in reference.calls)]
        actual = list(map(int, line.split()))
        if actual != expected or (observed is not None and observed != expected[0]):
            failures.setdefault(entry, f"{entry:#x}, case {i}: expected {expected}, got {actual}")
        counts[entry] = counts.get(entry, 0) + 1
    if failures:
        raise SystemExit("\n".join(failures.values()))
    for entry, count in counts.items():
        print(f"{entry:#x}: {count} cases match two reference runs")


if __name__ == "__main__":
    main()
