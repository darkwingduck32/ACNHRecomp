"""Replay the pinned AArch64 obstacle query, including each material probe."""
import argparse
import itertools
import math
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_S0, UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x15ea82c, 0x424
POINT, TAPE, TRIG, OWNER_SLOT, OWNER = 0x20000, 0x21000, 0x22000, 0x24000, 0x25000
SITES = (0x15ea868, 0x15ea8cc, 0x15ea950, 0x15ea9c4, 0x15eaa3c, 0x15eaab0, 0x15eab28, 0x15eaba4, 0x15eac20)
TRACE = re.compile(r"acnh shovel point height (original|cpp) entry=(15ea82c) (call=.* out=[0-9a-f]+)")


def bits(value):
    return int.from_bytes(struct.pack("<f", value), "little")


def make_trig():
    result = bytearray()
    for i in range(256):
        first, second = (math.tau * (i + d) / 256 for d in (0, 1))
        result.extend(struct.pack("<4f", math.sin(first), math.sin(second) - math.sin(first),
                                  math.cos(first), math.cos(second) - math.cos(first)))
    return bytes(result)


def fixture(trig, coordinates, heading, hit):
    tape = bytes(0xa0) + struct.pack("<9I", *(0x1c if i == hit else 3 + i for i in range(9)))
    return {
        POINT: struct.pack("<3f", *coordinates), TAPE: tape, TRIG: trig,
        BASE + 0x4ab27c8: struct.pack("<Q", OWNER_SLOT), OWNER_SLOT: struct.pack("<Q", OWNER),
        BASE + 0x4ab2d00: struct.pack("<Q", TRIG), BASE + 0x5140230: struct.pack("<I", heading),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(module)):
        parser.error("unexpected obstruction query boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,), TRACE)

    stub = words(*address_words(9, TAPE), 0xb9400128, 0x9100412a, 0x8b08114a, 0x2940302b, 0xb940082d,
                 0xb940004e, 0x2900314b, 0x2901394d, 0x9102812a, 0xb868594b, 0xb900004b,
                 0x11000508, 0xb9000128, RETURN)
    callees = {BASE + site: (BASE + 0x83a950, stub) for site in SITES}
    trig = make_trig()
    scenarios = []
    payload = []
    for coordinates, radius, heading, hit in itertools.product(
            ((0., 0., 0.), (-0.0, 1., 2.), (405.875, 15., -689.625)),
            (0., -0.0, 2**-23, -2**-23, 2**-22, -0.5, 2.75),
            (0, 0x12345678, 0x20000000, 0xfedcba98), range(10)):
        fixture_bytes = fixture(trig, coordinates, heading, hit)
        scenarios.append((coordinates, radius, heading, hit, fixture_bytes))
        pairs = [(address + index, byte) for address, data in fixture_bytes.items() if address != TAPE
                 for index, byte in enumerate(data)]
        payload.append(" ".join(map(str, [bits(radius), hit, len(pairs), *(n for pair in pairs for n in pair)])))
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                            capture_output=True, text=True, check=True, timeout=45).stdout.splitlines()
    if len(output) != len(scenarios):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    for (coordinates, radius, heading, hit, memory), line in zip(scenarios, output):
        registers = {UC_ARM64_REG_X0: POINT, UC_ARM64_REG_S0: bits(radius)}
        first = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory, callees=callees)
        if first != run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory, callees=callees):
            raise SystemExit("unstable original obstruction case")
        tape = first.memory[TAPE]
        expected = [first.registers[UC_ARM64_REG_X0], len(first.calls)]
        for i, (site, _, owner) in enumerate(first.calls):
            expected.extend([site - BASE, owner, *struct.unpack_from("<4I", tape, 0x10 + 16*i)])
        if list(map(int, line.split())) != expected:
            raise SystemExit(f"{coordinates=}, {radius=}, {heading=:#x}, {hit=}: original {expected}; C++ {line}")
    print(f"{ENTRY:#x}: {len(scenarios)} cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
