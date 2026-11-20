"""Compare the action mask initializer, including ordered writes."""
import argparse
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
BASE, ENTRY, SIZE, OBJECT = 0x4000000000, 0x24699B8, 0x68, 0x20000
TRACE = re.compile(r"acnh action mask (original|cpp) caller=([0-9a-f]+) "
                   r"before=([0-9a-f,]+) after=([0-9a-f,]+)")


def inputs():
    masks = (0, 1 << 30, 1 << 31, 1 << 32, (1 << 64) - 1)
    yield from itertools.product(masks, masks, (0, 1, 2, 255))
    rng = random.Random(303)
    for _ in range(128):
        yield rng.getrandbits(64), rng.getrandbits(64), rng.choice((0, 1))


def live_cases(path, route):
    raw = path.read_bytes()
    rows = []
    for backend, caller, before, after in TRACE.findall(raw.decode(errors="replace")):
        if backend != route:
            raise ValueError(f"{path}: expected {route}, found {backend}")
        words = lambda value: tuple(int(word, 16) for word in value.split(","))
        rows.append((int(caller, 16), words(before), words(after)))
    if not rows:
        raise ValueError(f"{path}: no action mask calls")
    print(f"{path}: {len(rows)} calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return sorted(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected function boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = [(case, None) for case in inputs()]
    if args.live:
        original = live_cases(args.live[0], "original")
        replacement = live_cases(args.live[1], "cpp")
        if original != replacement:
            raise SystemExit("live callers, inputs or outcomes differ")
        cases.extend((before, after) for _, before, after in original)
    payload = "".join(f"{source} {working} {initialized}\n"
                      for (source, working, initialized), _ in cases)
    native = subprocess.run([str(args.candidate.resolve())], input=payload, text=True,
                            capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    for i, ((source, working, initialized), observed) in enumerate(cases):
        before = bytearray([0xa5] * 0x29)
        struct.pack_into("<Q", before, 0x10, source)
        struct.pack_into("<QB", before, 0x20, working, initialized)
        regs = {UC_ARM64_REG_X0: OBJECT}
        memory = {OBJECT: bytes(before)}
        result = run(BASE + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory)
        if result != run(BASE + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory):
            raise SystemExit(f"reference changed on repeat in case {i}")
        after = result.memory[OBJECT]
        if after[:0x20] != before[:0x20] or result.registers[UC_ARM64_REG_X0] != OBJECT:
            raise SystemExit(f"reference changed input memory or X0 in case {i}")
        values = (*struct.unpack_from("<Q", after, 0x10), *struct.unpack_from("<QB", after, 0x20))
        if observed is not None and observed != values:
            raise SystemExit(f"live capture differs from Unicorn in case {i}")
        events = tuple(word for addr, width, value in result.writes for word in (addr - OBJECT, width, value))
        got = tuple(map(int, native[i].split()))
        if got != values + events:
            raise SystemExit(f"case {i}: expected {values + events}, got {got}")
    print(f"{len(cases)} cases match two reference runs")


if __name__ == "__main__":
    main()
