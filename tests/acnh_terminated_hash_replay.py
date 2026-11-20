"""Compare the terminated-string hash with the pinned original function."""
import argparse
import hashlib
import itertools
import random
import re
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x5BD550, 0x104
TRACE = re.compile(r"acnh terminated hash (original|cpp) caller=([0-9a-f]+) "
                   r"seed=([0-9a-f]+) w0=([0-9a-f]+) bytes=([0-9a-f]+)")


def fixtures():
    rng = random.Random(303)
    for length, alignment, seed in itertools.product(range(17), range(4), (0, 1, 0xffffffff)):
        yield 0x20f00 + alignment, seed, bytes(rng.randrange(1, 256) for _ in range(length)) + b"\0", None
    for length in (4, 8, 255, 4095):
        yield 0x21000 - length - 1, 0, bytes(rng.randrange(1, 256) for _ in range(length)) + b"\0", None
    yield 0x20000, 0xaf, b"a\0extra", None


def live_cases(path, expected_route):
    raw = path.read_bytes()
    records = []
    for route, caller, seed, output, data in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        records.append((int(caller, 16), int(seed, 16), bytes.fromhex(data), int(output, 16)))
    if not records:
        raise ValueError(f"{path}: no terminated hash calls")
    print(f"{path}: {len(records)} calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return sorted(records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(module)):
        parser.error("unexpected function boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = list(fixtures())
    if args.live:
        original = live_cases(args.live[0], "original")
        candidate = live_cases(args.live[1], "cpp")
        if original != candidate:
            raise SystemExit("live callers, strings, seeds or results differ")
        cases.extend((0x20000, seed, data, result) for _, seed, data, result in original)
    payload = "".join(f"{addr} {seed} {len(data)} " + " ".join(map(str, data)) + "\n"
                      for addr, seed, data, _ in cases)
    results = subprocess.run([str(args.candidate.resolve())], input=payload, capture_output=True,
                             text=True, check=True, timeout=30).stdout.splitlines()
    if len(results) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    for i, (addr, seed, data, observed) in enumerate(cases):
        regs = {UC_ARM64_REG_X0: addr, UC_ARM64_REG_X1: seed}
        memory = {addr: data}
        original = run(BASE + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory, instruction_limit=8 * len(data) + 128)
        if (original != run(BASE + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory, instruction_limit=8 * len(data) + 128)
                or original.memory != memory or original.writes):
            raise SystemExit(f"unexpected reference effects in case {i}")
        expected = original.registers[UC_ARM64_REG_X0]
        if int(results[i]) != expected or (observed is not None and observed != expected):
            raise SystemExit(f"case {i}: expected hash {expected:#x}, got {int(results[i]):#x}")
    print(f"{len(cases)} cases match two reference runs")


if __name__ == "__main__":
    main()
