"""Replay the original shovel target selector with declared height and point callbacks."""
import argparse
from collections import Counter
import itertools
import math
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
BASE, ENTRY, SIZE = 0x4000000000, 0x15f36e0, 0x7f0
REQUEST, TAPE, TRIG = 0x20000, 0x21000, 0x22000
APPROACH = (0x15f39c4, 0x15f3a20, 0x15f3a74)
RADIUS = 0x15f3b3c
OFFSETS = (0x15f3b80, 0x15f3bd4, 0x15f3c28, 0x15f3c7c, 0x15f3cd0,
           0x15f3d24, 0x15f3d84, 0x15f3dd8, 0x15f3e38)
HEIGHT = (0x15f3928, 0x15f3e70, 0x15f3e88)
TRACE = re.compile(r"acnh shovel target (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")
SURVEY = re.compile(r"acnh shovel survey x=(-?\d+) z=(-?\d+) ")


def live_cases(path, route, tile, entries):
    results = {entry: Counter() for entry in entries}
    pending = {entry: Counter() for entry in entries}
    for line in path.read_text(errors="replace").splitlines():
        if match := TRACE.fullmatch(line):
            found_route, target, state = match.groups()
            entry = int(target, 16)
            if entry in results:
                if found_route != route:
                    raise ValueError(f"{path}: expected {route} at {entry:#x}, got {found_route}")
                (results if tile is None else pending)[entry][state] += 1
        elif tile is not None and (match := SURVEY.match(line)):
            focused = tuple(map(int, match.groups())) == tile
            for entry in entries:
                if focused:
                    results[entry].update(pending[entry])
                pending[entry].clear()
    for entry, states in results.items():
        if not states:
            raise ValueError(f"{path}: no calls to {entry:#x} on {route}")
        print(f"{path}: {states.total()} calls to {entry:#x}")
    return results


def compare_live(paths, tile, entries):
    if paths:
        original = live_cases(paths[0], "original", tile, entries)
        native = live_cases(paths[1], "cpp", tile, entries)
        for entry in entries:
            if original[entry] != native[entry]:
                raise SystemExit(f"{entry:#x}: original only {original[entry] - native[entry]}, "
                                 f"C++ only {native[entry] - original[entry]}")


def float_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def fixture(module, geometry, radius, fixed, heading, outcomes, heights):
    state = bytearray(0x48)
    struct.pack_into("<6f", state, 0, *geometry)
    struct.pack_into("<B", state, 0x28, fixed)
    struct.pack_into("<If", state, 0x38, 0x87654321, radius)
    tape = bytearray(16 * 13)
    for i, result in enumerate(outcomes):
        struct.pack_into("<I", tape, 16 * i + 28, result)
    trig = bytearray()
    for i in range(256):
        a, b = (math.tau * (i + d) / 256 for d in (0, 1))
        trig.extend(struct.pack("<4f", math.sin(a), math.sin(b) - math.sin(a),
                                math.cos(a), math.cos(b) - math.cos(a)))
    memory = {REQUEST: bytes(state), TAPE: bytes(tape), TRIG: bytes(trig),
              BASE + 0x4ab2d00: struct.pack("<Q", TRIG),
              BASE + 0x4076bac: bytes(module.image[0x4076bac:0x4076bac + 9*12])}
    record = words(*address_words(9, TAPE), 0xb9400128, 0x8b28512a, 0x2940300b, 0xb940080d,
                   0x2902314b, 0xb900194d, 0x11000508, 0xb9000128, 0xb9401d40, RETURN)
    callees = {BASE + site: (BASE + 0x15f3ed0, record) for site in (*APPROACH, RADIUS, *OFFSETS)}
    # All sites share the same original callee body. Use X0's target/origin address
    # and a counter to return the fixture's next height.
    height_tape = TAPE + len(tape)
    memory[height_tape] = struct.pack("<4I", 0, *(float_bits(f) for f in heights))
    measure = words(*address_words(9, height_tape), 0xb9400128, 0x11000508, 0xb9000128,
                    0xb8685928, 0x1e270100, RETURN)
    for site in HEIGHT:
        callees[BASE + site] = BASE + 0x15e026c, measure
    # The sample heading is supplied independently of the selector's trigonometry.
    callees[BASE + 0x15f3744] = BASE + 0x5f0400, words(*address_words(0, heading), RETURN)
    return memory, callees


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
        parser.error("unexpected target selector boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,))
    geometry = ((1, 2, 3, 3, 4, 9), (0, 1, 0, 0, 1, 0), (4, -2, -5, 40, 7, -20),
                (40, -10, 20, 5, 0, 8), (2, 4, 9, 2, 3, -16))
    headings = (0, 0x40000000, 0xffffffff, 0x71353758, 0xe1928811)
    answers = ((0,) * 12, (14,) * 12, (5,) + (0,) * 11, (2, 1) + (0,) * 10,
               (14, 0, 5) + (0,) * 9, (0,) * 5 + (8,) + (0,) * 6,
               (14, 14, 10) + (0,) * 9)
    cases = [
        (headings[i], outcomes, heights, *fixture(module, geom, radius, fixed, headings[i], outcomes, heights))
        for (i, geom), radius, fixed, outcomes, heights in itertools.product(
            enumerate(geometry), (0.0, 1.5, -2.75), (0, 1), answers,
            ((0.75, 7.5, 2.5), (-3.25, -11.0, 1.0)))
    ]
    payload = []
    for heading, outcomes, heights, memory, _ in cases:
        copied = {a: b for a, b in memory.items() if REQUEST <= a < TAPE or a == TRIG or a >= BASE}
        pairs = [(a+i, byte) for a,b in copied.items() for i,byte in enumerate(b)]
        payload.append(" ".join(map(str, [heading, *outcomes, *(float_bits(f) for f in heights), len(pairs),
                                          *(v for pair in pairs for v in pair)])))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload)+"\n",
                            text=True, capture_output=True, check=True, timeout=45).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit(f"candidate output count {len(native)}")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    for i, ((_, _, _, memory, callees), line) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: REQUEST}
        first = run(BASE+ENTRY, code, regs, tuple(regs), memory, callees=callees)
        if first != run(BASE+ENTRY, code, regs, tuple(regs), memory, callees=callees):
            raise SystemExit(f"unstable target selector case {i}")
        count, = struct.unpack_from("<I", first.memory[TAPE])
        expected = [first.registers[UC_ARM64_REG_X0], *struct.unpack("<18I", first.memory[REQUEST]),
                    len(first.calls), *(site-BASE for site, _, _ in first.calls)]
        for index in range(count):
            expected.extend(struct.unpack_from("<3I", first.memory[TAPE], 16*(index+1)))
        actual = list(map(int, line.split()))
        if actual != expected:
            at = next((j for j, (a, b) in enumerate(zip(actual, expected)) if a != b),
                      min(len(actual), len(expected)))
            raise SystemExit(f"case {i}, element {at}: original {expected[at:at+5]}, candidate {actual[at:at+5]}")
    print(f"{ENTRY:#x}: {len(cases)} cases match both AArch64 runs")


if __name__ == "__main__":
    main()
