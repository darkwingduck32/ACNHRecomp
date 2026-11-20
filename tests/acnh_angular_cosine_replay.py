"""Compare the angular cosine leaf with the pinned AArch64 routine."""
import argparse
import random
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_S0, UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x5F06DC, 0x54
GOT, TABLE = 0x4AB2D00, 0x4099798


def inputs():
    edges = (0, 1, 0xfffffe, 0xffffff, 0x1000000, 0x7fffffff,
             0x80000000, 0x80000001, 0xffffffff)
    yield from ((a, b) for a in edges for b in edges)
    for index in range(129):
        for delta in (-1, 0, 1):
            difference = (index * 0x1000000 + delta) & 0xffffffff
            yield 0, difference
            yield difference, 0
    rng = random.Random(303)
    for _ in range(512):
        yield rng.getrandbits(32), rng.getrandbits(32)


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
    if not any(r.offset == GOT and r.type == nso.R_AARCH64_RELATIVE and r.addend == TABLE
               for r in module.relocs):
        parser.error("unexpected lookup table relocation")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    table = bytes(module.image[TABLE:TABLE + 129 * 16])
    table_words = [word for _, _, base, slope in struct.iter_unpack("<4I", table) for word in (base, slope)]
    cases = list(inputs())
    payload = " ".join(map(str, table_words)) + "\n" + "".join(f"{a} {b}\n" for a, b in cases)
    candidate = subprocess.run([str(args.candidate.resolve())], input=payload, text=True,
                               capture_output=True, check=True, timeout=30)
    actual = candidate.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    memory = {BASE + GOT: struct.pack("<Q", BASE + TABLE), BASE + TABLE: table}
    for i, (a, b) in enumerate(cases):
        registers = {UC_ARM64_REG_X0: a, UC_ARM64_REG_X1: b}
        original = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_S0,), memory)
        repeat = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_S0,), memory)
        if original != repeat or original.memory != memory or original.written_bytes:
            raise SystemExit(f"unexpected reference effects in case {i}")
        expected, got = original.registers[UC_ARM64_REG_S0], int(actual[i])
        if got != expected:
            raise SystemExit(f"case {i}, w0={a:08x}, w1={b:08x}: expected {expected:08x}, got {got:08x}")
    print(f"{len(cases)} comparisons passed twice")


if __name__ == "__main__":
    main()
