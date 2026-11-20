"""Replay the two shovel provider context callbacks against the pinned module."""
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
BASE = 0x4000000000
ENTRIES = ((0x25ee660, 0xf8), (0x25ee758, 0x9c))
OWNER, FLAG, FIRST, SECOND, COUNTER = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000
TRACE = re.compile(r"acnh shovel context (original|cpp) entry=([0-9a-f]+) (state=[^\r\n]+)")


def fixture(mode, first, second, guard, variant, disabled, allowed, count):
    obj = bytearray(0x360)
    struct.pack_into("<QQ", obj, 0x308, FIRST if first else 0, SECOND if second else 0)
    struct.pack_into("<IBxB", obj, 0x358, mode, guard, variant)
    flag = FLAG if disabled else 0
    memory = {OWNER: bytes(obj), FLAG: bytes((int(disabled == 1),)),
              SECOND: struct.pack("<i", count), COUNTER: struct.pack("<12xi", count - 2)}
    return flag, memory


def declarations(acquire, flag, allowed):
    if acquire:
        routes = {
            0x25ee674: (0x2601110, words(*address_words(0, flag), RETURN)),
            0x25ee6b8: (0x3f03880, words(RETURN)),
            0x25ee6c0: (0x3f04c70, words(0x52800000 | (int(allowed) << 5), RETURN)),
            0x25ee6e0: (0x262e9bc, words(RETURN)),
            0x25ee710: (0x37567d0, words(*address_words(0, COUNTER), RETURN)),
            0x25ee728: (0x37e2e80, words(RETURN)),
            0x25ee740: (0x37569c0, words(RETURN)),
        }
    else:
        routes = {
            0x25ee78c: (0x3f038c0, words(RETURN)),
            0x25ee7b0: (0x37568c0, words(RETURN)),
            0x25ee7c0: (0x37568c0, words(RETURN)),
            0x25ee7d4: (0x37568c0, words(RETURN)),
            0x25ee7e4: (0x3756a80, words(RETURN)),
        }
    return {BASE + site: (BASE + target, body) for site, (target, body) in routes.items()}


def live(path, expected):
    states = {entry: Counter() for entry, _ in ENTRIES}
    for route, entry, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected:
            raise ValueError(f"{path}: expected {expected}, found {route}")
        states[int(entry, 16)][state] += 1
    if not all(states.values()):
        raise ValueError(f"{path}: failed to capture both context callbacks")
    print(f"{path}: {[(hex(entry), rows.total()) for entry, rows in states.items()]}")
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path)
    args = parser.parse_args()
    module = nso.load(args.module)
    sizes = {f.start: f.size for f in ehframe.parse(module)}
    if build_id(args.module) != BUILD or any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected build or function boundaries")
    if args.live and live(args.live[0], "original") != live(args.live[1], "cpp"):
        raise SystemExit("live context inputs or results differ")

    cases = []
    for acquire, mode, handles, guard, variant, disabled, allowed, count in itertools.product(
            (True, False), (0, 1, 2, 3), ((False, True), (True, False), (True, True)),
            (False, True), (False, True), (0, 1, 2), (False, True), (-1, 2, 3)):
        flag, memory = fixture(mode, *handles, guard, variant, disabled, allowed, count)
        cases.append((acquire, flag, allowed, memory))
    lines = []
    for acquire, flag, allowed, memory in cases:
        pairs = [(address + i, value) for address, data in memory.items() for i, value in enumerate(data)]
        lines.append(f"{int(acquire)} {OWNER} {flag} {int(allowed)} {COUNTER} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    for i, ((acquire, flag, allowed, memory), row) in enumerate(zip(cases, native)):
        entry, size = ENTRIES[not acquire]
        body = bytes(module.image[entry:entry + size])
        regs, outputs = {UC_ARM64_REG_X0: OWNER}, (UC_ARM64_REG_X0,) if acquire else ()
        stubs = declarations(acquire, flag, allowed)
        result = run(BASE + entry, body, regs, outputs, memory, callees=stubs)
        if result != run(BASE + entry, body, regs, outputs, memory, callees=stubs):
            raise SystemExit(f"unstable context replay at case {i}")
        expected = [result.registers.get(UC_ARM64_REG_X0, 0), len(result.calls)]
        for _, target, receiver in result.calls:
            expected.extend((target - BASE, receiver))
        expected.extend(field for write in result.writes for field in write)
        if [int(field) for field in row.split()] != expected:
            raise SystemExit(f"context replay case {i} at {entry:#x} expected {expected}, got {row}")
    print(f"{len(cases) // 2} independent cases per entry matched both AArch64 runs")


if __name__ == "__main__":
    main()
