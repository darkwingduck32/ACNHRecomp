"""Replay the shovel gather's item metadata classification."""
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
BASE, ENTRY, SIZE = 0x4000000000, 0x19ee3b4, 0x404
DESCRIPTOR, OWNER_SLOT, OWNER, CATALOG, ENTRIES, HEADER, EXT_HEADER = (
    0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x27000, 0x2b000
)


def table(valid):
    result = bytearray(0x80)
    if valid:
        # The row is one stride after the first packed row.
        struct.pack_into("<IIHB", result, 0, 4, 32, 1, 0)
        struct.pack_into("<I", result, 0x34, 0x34)
    return bytes(result)


def fixture(source, flags, mode, enum_type, direct, extra_code, extended):
    descriptor = struct.pack("<QHBBIQ", 0, 7, flags, 0, 0,
                             0 if source == 0 else HEADER + 0x34)
    catalog = bytearray(0x200)
    struct.pack_into("<I4xQ16xH", catalog, 0x1d8, int(mode & 8 != 0), ENTRIES, 7)
    memory = {
        DESCRIPTOR: descriptor,
        BASE + 0x4ab43c0: struct.pack("<Q", OWNER_SLOT),
        OWNER_SLOT: struct.pack("<Q", OWNER),
        OWNER + 0x57e58: struct.pack("<Q", CATALOG),
        CATALOG: bytes(catalog),
        ENTRIES: struct.pack("<Q", HEADER + 0x34),
        HEADER: table(source == 1),
        EXT_HEADER: table(bool(mode & 16)),
    }
    for address, values, code in ((0x5226ec0, 0x30000, enum_type),
                                  (0x5226e60, 0x31000, direct),
                                  (0x5226af0, 0x32000, extra_code),
                                  (0x5226b70, 0x33000, extended)):
        memory[BASE + address] = struct.pack("<I4xQ", 2, values)
        memory[values] = struct.pack("<HH", 0, code)
    return memory


def dependencies(mode):
    def immediate(value):
        return words(0x52800000 | (value << 5), RETURN)
    calls = {
        0x19ee3e0: (0x267d6c4, immediate(7)),
        0x19ee424: (0x19f93e8, immediate(10 if mode & 1 else 0)),
        0x19ee718: (0x19da7a0, words(*address_words(0, EXT_HEADER + 0x34 if mode & 4 else 0), RETURN)),
    }
    for site in (0x19ee440, 0x19ee44c, 0x19ee46c):
        calls[site] = 0x19e59b0, immediate(int(mode & 2 != 0))
    return {BASE + site: (BASE + target, body) for site, (target, body) in calls.items()}


TRACE = re.compile(r"acnh shovel metadata (original|cpp) (item=[0-9a-f]+ row=[01] out=[0-9a-f]+)")


def live_cases(path, expected_route):
    result = Counter()
    for route, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        result[state] += 1
    if not result:
        raise ValueError(f"{path}: no {expected_route} metadata classifications")
    print(f"{path}: {result.total()} metadata classifications")
    return result


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
        parser.error("unexpected shovel metadata bounds")
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("shovel metadata gameplay traces differ")
    cases = [(mode, fixture(source, flags, mode, enum_type, direct, extra_code, extended))
             for (source, flags, mode), (enum_type, direct, extra_code, extended) in itertools.product(
                 itertools.product((0, 1, 2), (0, 4, 8, 12), (0, 1, 2, 4, 7, 8, 9, 11, 13, 29)),
                 ((0, 2, 0, 0), (0xa4, 15, 0, 0), (0xa6, 18, 0, 0),
                  (0, 0x1c, 0x21, 11), (0xbc, 0x1b, 0x21, 28), (0xbd, 0x15, 0x21, 3)))]
    lines = []
    for mode, memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{BASE} {DESCRIPTOR} {mode} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for i, ((mode, memory), line) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: DESCRIPTOR}
        outputs = (UC_ARM64_REG_X0,)
        first = run(BASE + ENTRY, code, regs, outputs, memory, callees=dependencies(mode))
        if first != run(BASE + ENTRY, code, regs, outputs, memory, callees=dependencies(mode)):
            raise SystemExit(f"unstable original metadata case {i}")
        expected = [first.registers[UC_ARM64_REG_X0], *(site - BASE for site, _, _ in first.calls)]
        actual = list(map(int, line.split()))
        if actual != expected:
            raise SystemExit(f"metadata case {i}, mode {mode}: original {expected}, C++ {actual}")
    print(f"{len(cases)} independent metadata cases match the result and calls of both original runs")


if __name__ == "__main__":
    main()
