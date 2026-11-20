"""Compare ACNH's angular-difference leaf with an isolated native candidate."""
import argparse
import hashlib
import math
import random
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x5F05F8, 0xE4
OUTPUT = 0x20000


def pairs():
    edges = (-3.4028234663852886e38, -2**42, -720, -360, -180, -5, -2**-149,
             -0.0, 0.0, 2**-149, 4, 180 - 2**-16, 180, 180 + 2**-16,
             359, 360 - 2**-15, 360, 360 + 2**-15, 720, 2**42, 3.4028234663852886e38)
    yield from ((a, b) for a in edges for b in edges)
    rng = random.Random(303)
    for _ in range(512):
        yield rng.uniform(-1440, 1440), rng.uniform(-1440, 1440)
    for _ in range(256):
        pair = struct.unpack("<ff", rng.randbytes(8))
        if all(math.isfinite(x) for x in pair):
            yield pair


TRACE = re.compile(
    r"acnh angle difference (original|cpp) caller=([0-9a-f]{8}) "
    r"s0=([0-9a-f]{8}) s1=([0-9a-f]{8}) "
    r"w0=([0-9a-f]{8}) distance=([0-9a-f]{8})"
)


def live_cases(path, expected_route):
    observed = {}
    raw = path.read_bytes()
    for match in TRACE.finditer(raw.decode("utf-8", errors="replace")):
        route, *words = match.groups()
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, saw {route}")
        caller, a, b, direction, distance = (int(word, 16) for word in words)
        result = direction, distance
        if observed.setdefault((caller, a, b), result) != result:
            raise ValueError(f"{path}: inconsistent result at caller {caller:#x} for {a:08x} {b:08x}")
    if not observed:
        raise ValueError(f"{path}: no {expected_route} angular-difference calls")
    print(f"{path}: {len(observed)} unique call inputs; SHA-256 {hashlib.sha256(raw).hexdigest()}")
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
    cases = [struct.unpack("<II", struct.pack("<ff", *pair)) for pair in pairs()]
    observed = {}
    if args.live:
        original = live_cases(args.live[0], "original")
        replacement = live_cases(args.live[1], "cpp")
        if original != replacement:
            raise SystemExit("original and C++ logs differ in observed calls or results")
        for (_caller, a, b), result in original.items():
            if observed.setdefault((a, b), result) != result:
                raise SystemExit(f"live results disagree across callers for {a:08x} {b:08x}")
        cases.extend(observed)
    payload = "".join(f"{a} {b}\n" for a, b in cases)
    candidate = subprocess.run([str(args.candidate.resolve())], input=payload, text=True,
                               capture_output=True, check=True, timeout=30)
    actual = candidate.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    memory = {OUTPUT: bytes.fromhex("a5a5a5a5")}
    expected_writes = frozenset(range(OUTPUT, OUTPUT + 4))
    for i, (a, b) in enumerate(cases):
        registers = {UC_ARM64_REG_X0: OUTPUT, UC_ARM64_REG_S0: a, UC_ARM64_REG_S1: b}
        original = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory)
        repeat = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory)
        if original != repeat or original.written_bytes != expected_writes:
            raise SystemExit(f"unexpected reference effects in case {i}, s0={a:08x}, s1={b:08x}")
        expected = original.registers[UC_ARM64_REG_X0], int.from_bytes(original.memory[OUTPUT], "little")
        if (a, b) in observed and observed[a, b] != expected:
            raise SystemExit(f"live trace differs from Unicorn for {a:08x} {b:08x}")
        got = tuple(map(int, actual[i].split()))
        if got != expected:
            raise SystemExit(f"case {i}, s0={a:08x}, s1={b:08x}: expected {expected}, got {got}")
    print(f"{len(cases)} comparisons passed twice")


if __name__ == "__main__":
    main()
