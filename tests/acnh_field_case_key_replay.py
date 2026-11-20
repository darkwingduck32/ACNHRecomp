"""Replay the four-code field key against the original 3.0.3 function."""
import argparse
import collections
import hashlib
import itertools
import re
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x1140FD0, 0x74
TRACE = re.compile(
    r"acnh field case key (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"args=([0-9a-f]+),([0-9a-f]+),([0-9a-f]+),([0-9a-f]+) table=([0-9a-f]{6}) out=([0-9a-f]+)"
)


def fixtures():
    codes = (0, 1, 2, 3, 0x100, 0xff)
    for table in (bytes([0, 1, 2]), bytes([0x80, 0x43, 0xff])):
        for args in itertools.product(codes, repeat=4):
            yield BASE, args, table, None


def live_cases(path, expected_route):
    raw = path.read_bytes()
    records = []
    states = collections.Counter()
    for route, caller, base, a, b, c, d, table, out in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        base, out = int(base, 16), int(out, 16)
        args = tuple(int(value, 16) for value in (a, b, c, d))
        table = bytes.fromhex(table)
        records.append((base, args, table, out))
        states[(caller, tuple(value & 0xff for value in args), table, out)] += 1
    if not records:
        raise ValueError(f"{path}: no field key calls")
    print(f"{path}: {len(records)} calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    routes = parser.add_mutually_exclusive_group()
    routes.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    routes.add_argument("--live-default", nargs=2, type=Path, metavar=("ORIGINAL", "DEFAULT"),
                        help="allow different frequencies for the same call states in a capped trace")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(module)):
        parser.error("unexpected function boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = list(fixtures())
    live = args.live or args.live_default
    if live:
        original_states, original = live_cases(live[0], "original")
        candidate_states, candidate = live_cases(live[1], "cpp")
        if args.live and original_states != candidate_states:
            raise SystemExit("live field key callers, inputs or results differ")
        if args.live_default:
            if len(original) != len(candidate) or candidate_states.keys() - original_states.keys():
                raise SystemExit("default field key had missing calls or an unmatched caller, input or result")
            # The first 12 calls can include different repeats across runs.
            # Every captured default state has a matching original call and is replayed below.
        cases.extend(original)
        cases.extend(candidate)
    payload = "".join(" ".join(map(str, (base, *regs, *table))) + "\n" for base, regs, table, _ in cases)
    results = subprocess.run([str(args.candidate.resolve())], input=payload, capture_output=True,
                             text=True, check=True, timeout=30).stdout.splitlines()
    if len(results) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    for i, (base, args, table, observed) in enumerate(cases):
        regs = dict(zip((UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3), args))
        memory = {base + 0x40749e0: table}
        reference = run(base + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory)
        if (reference != run(base + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory)
                or reference.memory != memory or reference.writes):
            raise SystemExit(f"unexpected reference effects in case {i}")
        expected = reference.registers[UC_ARM64_REG_X0]
        if int(results[i]) != expected or (observed is not None and observed != expected):
            raise SystemExit(f"case {i}: expected field key {expected:#x}, got {int(results[i]):#x}")
    print(f"{len(cases)} cases match two reference runs")


if __name__ == "__main__":
    main()
