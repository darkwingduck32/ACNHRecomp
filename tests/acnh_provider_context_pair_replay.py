"""Replay the paired provider-context acquire helper with controlled counter callbacks."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x262e9bc, 0xb0
PAIR, FIRST, SECOND, COUNTER, REPLACEMENT = (i * 0x1000 for i in range(32, 37))


def fixture(first, second, counter, replace):
    memory = {
        PAIR: struct.pack("<QQ", FIRST if first is not None else 0, SECOND if second is not None else 0),
        FIRST: struct.pack("<i", first or 0),
        SECOND: struct.pack("<i", second or 0),
        COUNTER: struct.pack("<12xi", counter),
        REPLACEMENT: struct.pack("<i", 2),
    }
    writes = (*address_words(8, PAIR), *address_words(9, REPLACEMENT), 0xf9000109) if replace else ()
    body = words(*writes, *address_words(0, COUNTER), RETURN)
    return memory, {
        BASE + 0x262e9f4: (BASE + 0x37567d0, body),
        BASE + 0x262ea34: (BASE + 0x37567d0, body),
        BASE + 0x262ea0c: (BASE + 0x37e2e80, words(RETURN)),
        BASE + 0x262ea4c: (BASE + 0x37e2e80, words(RETURN)),
    }


def live(path, expected):
    pattern = re.compile(r"acnh shovel context pair (original|cpp) (caller=[^\r\n]+)")
    states = Counter()
    for route, state in pattern.findall(path.read_text(errors="replace")):
        if route != expected:
            raise ValueError(f"{path}: expected {expected}, found {route}")
        states[state] += 1
    if not states:
        raise ValueError(f"{path}: missing context-pair acquisitions")
    print(f"{path}: {states.total()} pair acquisitions, {dict(states)}")
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path)
    args = parser.parse_args()
    module = nso.load(args.module)
    if build_id(args.module) != BUILD or not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected build or function boundary")

    if args.live and live(args.live[0], "original") != live(args.live[1], "cpp"):
        raise SystemExit("live paired handle inputs or caller counts differ")

    counts = (2, 3, 1, 0, -1, None)
    cases, lines = [], []
    for first, second, counter, replace in itertools.product(counts, counts, (-1, 0, 1, 0x7fffffff), (False, True)):
        memory, calls = fixture(first, second, counter, replace)
        cases.append((memory, calls))
        pairs = [(address + i, value) for address, data in memory.items() for i, value in enumerate(data)]
        lines.append(f"{PAIR} {COUNTER} {REPLACEMENT if replace else 0} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for index, ((memory, calls), row) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: PAIR}
        original = run(BASE + ENTRY, code, regs, (), memory, callees=calls)
        if original != run(BASE + ENTRY, code, regs, (), memory, callees=calls):
            raise SystemExit(f"unstable reference case {index}")
        expected = [len(original.calls)]
        for site, target, receiver in original.calls:
            expected.extend((site - BASE, target - BASE, receiver))
        expected.extend(field for write in original.writes for field in write)
        if [int(field) for field in row.split()] != expected:
            raise SystemExit(f"context pair case {index}: expected {expected}, got {row}")
    print(f"{len(cases)} cases matched both AArch64 runs")


if __name__ == "__main__":
    main()
