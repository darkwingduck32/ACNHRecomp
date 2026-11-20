"""Compare the original height parameter type check and its guarded writes."""

import argparse
from itertools import product
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import words, RETURN
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, SIZE = 0x22688e4, 0x104
TYPE_ADDRESSES = (0x21000, 0x22000)
GUARDS = (0x23000, 0x24000)
STATIC_SOURCES = (0x25000, 0x26000)
OTHER = (0, 0x27000, 0xfffffffffffffff0)

def fixture(first_guard, second_guard, acquired, delegated, requested):
    mem = {}
    for type_slot, guard_slot, source_slot, index, guard in (
            (0x4ac3108, 0x4ac3110, 0x4ab3608, 0, first_guard),
            (0x4ab35f0, 0x4ab35f8, 0x4ab3600, 1, second_guard)):
        for offset, value in ((type_slot, TYPE_ADDRESSES[index]),
                              (guard_slot, GUARDS[index]),
                              (source_slot, STATIC_SOURCES[index])):
            mem[BASE + offset] = struct.pack("<Q", value)
        mem[TYPE_ADDRESSES[index]] = struct.pack("<Q", 0xdeadbeef)
        mem[GUARDS[index]] = bytes((guard,))
    acquire = words(0x52800000 | (acquired << 5), RETURN)
    release = words(0x52800028, 0x39000008, RETURN)
    callees = {BASE + site: (BASE + target, body) for site, target, body in (
        (0x226895c, 0x3f030d0, acquire), (0x22689a8, 0x3f030d0, acquire),
        (0x2268988, 0x3f030e0, release), (0x22689d4, 0x3f030e0, release),
        (0x226893c, 0x6521f0, words(0x52800000 | (delegated << 5), RETURN)),
    )}
    regs = {UC_ARM64_REG_X0: 0x987654321, UC_ARM64_REG_X1: requested}
    return mem, callees, regs

def cases():
    return product((0, 1, 3), (0, 1, 2), (0, 1), (0, 1), (*TYPE_ADDRESSES, *OTHER))

TRACE = re.compile(r"acnh shovel height type (original|cpp) entry=(22688e4) (call=.* out=[0-9a-f]+)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    image = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(image)):
        parser.error("unexpected parameter type check boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,), TRACE)

    scenarios = [(row, fixture(*row)) for row in cases()]
    payload = []
    for (_, _, acquired, delegated, requested), (memory, _, _) in scenarios:
        byte_pairs = [(address + i, value) for address, data in memory.items() for i, value in enumerate(data)]
        payload.append(" ".join(map(str, [acquired, delegated, requested, len(byte_pairs),
                                         *(word for pair in byte_pairs for word in pair)])))
    results = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                             capture_output=True, text=True, check=True, timeout=45).stdout.splitlines()
    if len(results) != len(scenarios):
        raise SystemExit("candidate returned the wrong number of cases")

    code = bytes(image.image[ENTRY:ENTRY+SIZE])
    for (row, (memory, callees, regs)), line in zip(scenarios, results):
        outputs = (UC_ARM64_REG_X0,)
        first = run(BASE + ENTRY, code, regs, outputs, memory, callees=callees)
        if first != run(BASE + ENTRY, code, regs, outputs, memory, callees=callees):
            raise SystemExit(f"unstable original result for {row}")
        expected = [first.registers[UC_ARM64_REG_X0], len(first.calls),
                    *(word for site, _, address in first.calls for word in (site - BASE, address)),
                    *(word for write in first.writes for word in write)]
        if list(map(int, line.split())) != expected:
            raise SystemExit(f"{row}: original {expected}; C++ {line}")
    print(f"{ENTRY:#x}: {len(scenarios)} cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
