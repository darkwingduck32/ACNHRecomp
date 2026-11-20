"""Replay the provider binding resolver and its encoded context thunks."""
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
ENTRIES = ((0x1267360, 0x7c), (0x260b860, 0x34), (0x260b934, 0x30))
MANAGER, POINTER, BINDING, VTABLE = 0x20000, 0x21000, 0x22000, 0x23000
CONTEXT, OBJECT, TARGET = 0x24000, 0x25000, BASE + 0x120000
TRACE = re.compile(r"acnh shovel provider binding (original|cpp) entry=([0-9a-f]+) (state=[^\r\n]+)")


def binding(mode):
    manager = bytearray(0x454)
    struct.pack_into("<Q", manager, 0x10, 0 if mode == 2 else POINTER)
    struct.pack_into("<B", manager, 0x453, int(mode in (2, 3, 4)))
    pointer = bytearray(0x138)
    struct.pack_into("<Q", pointer, 0, BINDING)
    struct.pack_into("<Q", pointer, 0x130, BINDING if mode == 4 else 0)
    return {
        BASE + 0x5474040: struct.pack("<Q", 0 if mode == 0 else MANAGER),
        MANAGER: bytes(manager),
        POINTER: bytes(pointer),
        BINDING: struct.pack("<Q", VTABLE),
        VTABLE: struct.pack("<56xQ", TARGET),
    }


def member(mode, adjustment, virtual_slot):
    receiver = OBJECT if mode != 0 else 0
    function = 0 if mode in (0, 1) else TARGET if mode == 2 else virtual_slot
    encoded_bias = ((adjustment << 1) | int(mode == 3)) & ((1 << 64) - 1)
    argument = OBJECT + adjustment
    return {
        CONTEXT: struct.pack("<QQQQ", 0, receiver, function, encoded_bias),
        argument: struct.pack("<Q", VTABLE),
        VTABLE + virtual_slot: struct.pack("<Q", TARGET),
    }


def live_cases(path, expected_route):
    result = {e: Counter() for e, _ in ENTRIES}
    for route, entry, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        result[int(entry, 16)][state] += 1
    if not all(result.values()):
        raise ValueError(f"{path}: missing calls at {[hex(e) for e,hits in result.items() if not hits]}")
    print(f"{path}: {[(hex(entry), states.total()) for entry, states in result.items()]}")
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
    sizes = {f.start: f.size for f in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected provider binding boundaries")
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("provider binding gameplay traces differ")
    cases = [(0, result, binding(mode)) for mode, result in itertools.product(
                 range(5), (0, 0x30000, 0x401234))]
    cases.extend((index, 0, member(mode, bias, slot))
                 for index, mode, bias, slot in itertools.product(
                     (1, 2), range(4), (-32, -8, 0, 8, 128), (0, 8, 56)))
    lines = []
    for index, result, memory in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{index} {BASE} {CONTEXT} {result} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts = [0, 0, 0]
    for (index, result, memory), line in zip(cases, native):
        entry, size = ENTRIES[index]
        regs = {UC_ARM64_REG_X0: CONTEXT}
        outputs = (UC_ARM64_REG_X0,)
        if index == 0:
            stub = words(*address_words(0, result), RETURN)
            callees = {BASE + site: (TARGET, stub) for site in (0x1267390, 0x12673c0)}
        else:
            callees = {BASE + (0x260b888 if index == 1 else 0x260b95c): (TARGET, words(RETURN))}
        code = bytes(module.image[entry:entry + size])
        first = run(BASE + entry, code, regs, outputs, memory, callees=callees)
        if first != run(BASE + entry, code, regs, outputs, memory, callees=callees):
            raise SystemExit(f"unstable original provider binding at {entry:#x}")
        expected = [first.registers[UC_ARM64_REG_X0]]
        for site, target, receiver in first.calls:
            expected.extend((site - BASE if index == 0 else target, receiver))
        if list(map(int, line.split())) != expected:
            raise SystemExit(f"{entry:#x}: original {expected}, C++ {line}")
        counts[index] += 1
    for (entry, _), count in zip(ENTRIES, counts):
        print(f"{entry:#x}: {count} independent cases match both AArch64 runs")


if __name__ == "__main__":
    main()
