"""Replay the 3.0.3 position approach leaf against its C++ candidate."""
import argparse
import hashlib
import random
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import (
    UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2,
    UC_ARM64_REG_X0, UC_ARM64_REG_X1,
)

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x3DB4B60, 0x100
POSITION, TARGET = 0x20000, 0x20020


def inputs():
    yield 0, 0, 0, 3, 4, 12, 0.5, 10, 0.2
    yield -0.0, 0, 0, 0, -0.0, 0, 1, 10, 0.2
    for gain in (-1, -2**-23, -0.0, 2**-23, 0.01, 0.2, 1, 4):
        for distance in (2**-24, 2**-23, 0.2 - 2**-25, 0.2, 0.2 + 2**-25, 10, 20):
            for lo, hi in ((0.2, 10), (0, 0), (-10, -1), (2, 1)):
                yield 0, 0, 0, distance, 0, 0, gain, hi, lo
    rng = random.Random(303)
    for _ in range(512):
        position = [rng.uniform(-1280, 1280) for _ in range(3)]
        target = [x + rng.uniform(-20, 20) for x in position]
        yield *position, *target, rng.uniform(-2, 2), rng.uniform(0, 25), rng.uniform(0, 5)


    for _ in range(256):
        yield tuple(rng.uniform(-2**20, 2**20) for _ in range(9))


TRACE = re.compile(
    r"acnh position approach (original|cpp) p=([0-9a-f,]+) "
    r"t=([0-9a-f,]+) args=([0-9a-f,]+) after=([0-9a-f,]+) s0=([0-9a-f]{8})"
)


def live_cases(path, expected_route):
    observed = {}
    raw = path.read_bytes()
    for match in TRACE.finditer(raw.decode("utf-8", errors="replace")):
        route, position, target, arguments, after, remaining = match.groups()
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, saw {route}")
        key = tuple(int(word, 16) for word in ",".join((position, target, arguments)).split(","))
        result = tuple(int(word, 16) for word in ",".join((after, remaining)).split(","))
        if len(key) != 9 or len(result) != 4:
            raise ValueError(f"{path}: malformed approach trace")
        if observed.setdefault(key, result) != result:
            raise ValueError(f"{path}: inconsistent result for {key}")
    if not observed:
        raise ValueError(f"{path}: no {expected_route} position approach calls")
    print(f"{path}: {len(observed)} unique inputs; SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return observed


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
    cases = [struct.unpack("<9I", struct.pack("<9f", *case)) for case in inputs()]
    observed = {}
    if args.live:
        observed = live_cases(args.live[0], "original")
        replacement = live_cases(args.live[1], "cpp")
        if observed != replacement:
            raise SystemExit("original and C++ logs differ in observed inputs or results")
        cases.extend(observed)
    payload = "".join(" ".join(map(str, case)) + "\n" for case in cases)
    candidate = subprocess.run([str(args.candidate.resolve())], input=payload, text=True,
                               capture_output=True, check=True, timeout=30)
    actual = candidate.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    all_writes = frozenset(range(POSITION, POSITION + 12))
    for i, case in enumerate(cases):
        memory = {POSITION: struct.pack("<3I", *case[:3]), TARGET: struct.pack("<3I", *case[3:6])}
        gain, maximum, minimum = case[6:]
        registers = {UC_ARM64_REG_X0: POSITION, UC_ARM64_REG_X1: TARGET,
                     UC_ARM64_REG_S0: gain, UC_ARM64_REG_S1: maximum, UC_ARM64_REG_S2: minimum}
        original = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_S0,), memory)
        repeat = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_S0,), memory)
        if (original != repeat or original.memory[TARGET] != memory[TARGET]
                or original.written_bytes not in (all_writes, frozenset())):
            raise SystemExit(f"unexpected reference effects in case {i}: {case}")
        expected = (*struct.unpack("<3I", original.memory[POSITION]),
                    original.registers[UC_ARM64_REG_S0], int(bool(original.written_bytes)))
        if case in observed and observed[case] != expected[:4]:
            raise SystemExit(f"live trace differs from Unicorn for {case}")
        got = tuple(map(int, actual[i].split()))
        if got != expected:
            raise SystemExit(f"case {i}, input {case}: expected {expected}, got {got}")
    print(f"{len(cases)} comparisons passed twice")


if __name__ == "__main__":
    main()
