"""Compare the pilot heading calculation against the pinned NSO."""
import argparse
import hashlib
import random
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x5F0400, 0x1F8
TABLE, TABLE_SIZE = 0x409A7A8, 129 * 8


def inputs():
    edges = [-3.4028234663852886e38, -1.0, -1.401298464324817e-45, -0.0,
             0.0, 1.401298464324817e-45, 1.0, 3.4028234663852886e38]
    yield from ((x, z) for x in edges for z in edges)
    for i in range(129):
        for delta in (-2**-22, 0, 2**-22):
            ratio = i / 128 + delta
            for x, z in [(ratio, 1), (1, ratio), (-ratio, 1), (1, -ratio),
                         (ratio, -1), (-1, ratio), (-ratio, -1), (-1, -ratio)]:
                yield x, z
    rng = random.Random(303)
    for _ in range(256):
        yield rng.uniform(-100, 100), rng.uniform(-100, 100)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module build {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("function boundary is absent from the module's unwind table")
    ro_start, ro_size = module.ro
    if not ro_start <= TABLE <= ro_start + ro_size - TABLE_SIZE:
        parser.error("lookup table outside read-only segment")

    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    table = bytes(module.image[TABLE:TABLE + TABLE_SIZE])
    cases = [struct.unpack("<II", struct.pack("<ff", *pair)) for pair in inputs()]
    words = struct.unpack("<258I", table)
    payload = " ".join(map(str, words)) + "\n" + "".join(f"{x} {z}\n" for x, z in cases)
    candidate = subprocess.run([str(args.candidate.resolve())], input=payload, text=True,
                               capture_output=True, check=True, timeout=30)
    actual = candidate.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    for i, (x, z) in enumerate(cases):
        registers = {UC_ARM64_REG_S0: x, UC_ARM64_REG_S1: z}
        memory = {BASE + TABLE: table}
        original = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory)
        repeat = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory)
        if original != repeat or original.memory != memory or original.written_bytes:
            raise SystemExit(f"unexpected reference effects in case {i} (input {x:08x} {z:08x})")
        expected = original.registers[UC_ARM64_REG_X0]
        got = int(actual[i])
        if got != expected:
            raise SystemExit(f"case {i}, s0={x:08x}, s1={z:08x}: expected {expected:#010x}, got {got:#010x}")
    print(f"{len(cases)} comparisons passed twice; input SHA-256 {hashlib.sha256(payload.encode()).hexdigest()}")


if __name__ == "__main__":
    main()
