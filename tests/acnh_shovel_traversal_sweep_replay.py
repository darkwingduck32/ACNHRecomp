"""Replay the traversal sweeps with modeled stage and collision effects."""
import argparse
from itertools import product
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import (UC_ARM64_REG_S0, UC_ARM64_REG_S1,
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3)

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import words, RETURN
from tests.acnh_shovel_traversal_sweep_captures import candidate_input, compare_live, read_captures, replay
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
SIZE = 0x424
BASE, ENTRY = 0x4000000000, 0x15f23b0
REQUEST, OUTPUT, SCENE, TRIG, RECORD = 0x31000, 0x32000, 0x33000, 0x34000, 0x36000
OUT = struct.pack("<3I", 0xdeadbeef, 0x50607080, 0x10203040)
STAGE = 0x758f7eaf


def original(code, mode, output, record, accept, blocked, angle, request, samples):
    expected_address = OUTPUT if output else 0
    memory = {
        REQUEST: request,
        OUTPUT: OUT,
        BASE + 0x4ab2720: struct.pack("<Q", SCENE),
        BASE + 0x4ab2010: struct.pack("<Q", 0x13000),
        BASE + 0x4abfe68: struct.pack("<Q", 0x14000),
        BASE + 0x54c04d8: struct.pack("<I", STAGE),
        BASE + 0x4ab2d00: struct.pack("<Q", TRIG),
        TRIG + 16*(angle >> 24): struct.pack("<4f", *samples),
        SCENE: bytes(8),
        RECORD: struct.pack("<h", 0x1234 if record else -2),
    }
    registers = {
        UC_ARM64_REG_X0: REQUEST, UC_ARM64_REG_X1: expected_address, UC_ARM64_REG_X2: mode,
    }
    trace = []
    attempts = 0
    def intercept(uc, site, target):
        nonlocal attempts
        offset = site - BASE
        x0, x1, x2, x3 = (uc.reg_read(register) for register in (
             UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3))
        if offset in (0x15f2488, 0x15f251c):
            assert target == BASE + 0x15e026c and x1 == 0 and x2 == 0 and uc.reg_read(UC_ARM64_REG_S0) == 0
            trace.extend((1, *struct.unpack("<3I", uc.mem_read(x0, 12))))
            uc.reg_write(UC_ARM64_REG_S0, bits(11. if offset == 0x15f2488 else 13.))
        elif offset == 0x15f2548:
            assert target == BASE + 0x5f0400
            trace.extend((2, uc.reg_read(UC_ARM64_REG_S0), uc.reg_read(UC_ARM64_REG_S1)))
            uc.reg_write(UC_ARM64_REG_X0, angle)
        elif offset == 0x15f2654:
            assert target == BASE + 0x16baf44 and x0 == STAGE
            uc.reg_write(UC_ARM64_REG_X0, 0)
        elif offset == 0x15f2670:
            assert target == BASE + 0x195190c and x1 == 0
            trace.extend((3, *struct.unpack("<2I", uc.mem_read(x2 + 8, 8))))
            uc.mem_write(x3 + 8, struct.pack("<2I", 81, 139))
            uc.reg_write(UC_ARM64_REG_X0, RECORD)
        elif offset == 0x15f2780:
            assert target == BASE + 0x15ed290 and bool(x3) == record
            attempts += 1
            trace.extend((4, *struct.unpack("<3I", uc.mem_read(x0, 12)), int(bool(x3))))
            if x3:
                trace.extend(struct.unpack("<2I", uc.mem_read(x3 + 8, 8)))
            if attempts in (accept, blocked):
                uc.mem_write(x1, struct.pack("<3f", 401.25, 15., 682.5))
                if attempts != blocked:
                    uc.mem_write(x2, b"\0")
                uc.reg_write(UC_ARM64_REG_X0, 1)
            else:
                uc.reg_write(UC_ARM64_REG_X0, 0)
        else:
            return False
        return True

    targets = {0x15f2488: 0x15e026c, 0x15f251c: 0x15e026c, 0x15f2548: 0x5f0400,
               0x15f2654: 0x16baf44, 0x15f2670: 0x195190c, 0x15f2780: 0x15ed290}
    callees = {BASE + site: (BASE + target, words(RETURN)) for site, target in targets.items()}
    result = run(BASE + ENTRY, code, registers, (UC_ARM64_REG_X0,), memory,
                 callees=callees, call_model=intercept)
    writes = [(address-OUTPUT+32, width, value) for address, width, value in result.writes]
    return (result.registers[UC_ARM64_REG_X0], len(writes), *(item for write in writes for item in write), *trace)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path, help="Replay original sweep captures")
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected sweep function boundary")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = []
    for mode, out, record, case, plan in product((0, 1), (0, 1), (0, 1), range(3), ((0, 0), (1, 0), (19, 0), (2, 1))):
        points = ((401.25, 17., 682., 408., 5., 691.25), (-4.5, -0., 3.5, 2.5, 12., -5.),
                  (1., 2., 3., 1., 2., 3.))[case]
        samples = ((0.45, 0.03125, 0.89, -0.05), (-0.5, 0.14, -0.61, -0.3), (-0., 0., -1., -0.831))[case]
        descriptor = struct.pack("<6fQ", *points, 0x1000100000000)
        angle = (0x11aabbcc, 0x81234567, 0xee000200)[case]
        cases.append((mode, out, record, *plan, angle, descriptor, samples))
    def prepare(c):
        mode, output, record, accept, blocked, angle, descriptor, samples = c
        heights = (bits(11.), 0, 0, 0, 0, bits(13.), 0, 0, 0, 0)
        xyz = tuple(bits(value) for value in (401.25, 15., 682.5))
        casts = tuple(value for index in range(1, 20)
                      for value in (int(index in (accept, blocked)), int(index == blocked), *xyz))
        return " ".join(map(str, (mode, output, record, 81, 139, angle, *struct.unpack("<8I", descriptor),
                                  *(bits(value) for value in samples), *heights, *casts)))
    captures = read_captures(args.capture) if args.capture else ()
    if args.live:
        if not captures:
            parser.error("--live requires the original console log via --capture")
        compare_live(captures, *args.live)
    rows = [*map(prepare, cases), *(" ".join(map(str, candidate_input(case))) for case in captures)]
    proc = subprocess.run([str(args.candidate.resolve())],
                          input="\n".join(rows) + "\n",
                          text=True, capture_output=True, check=True, timeout=30)
    lines = proc.stdout.splitlines()
    if len(lines) != len(rows):
        raise SystemExit("candidate returned the wrong number of rows")
    for i, (entry, line) in enumerate(zip(cases, lines)):
        expected = original(code, *entry)
        assert expected == original(code, *entry)
        actual = tuple(map(int, line.split()))
        if expected != actual:
            raise SystemExit(f"case {i}: expected {expected}, got {actual}")
    for i, (entry, line) in enumerate(zip(captures, lines[len(cases):])):
        expected = replay(entry, code)
        if expected != replay(entry, code) or expected != tuple(map(int, line.split())):
            raise SystemExit(f"captured sweep case {i} differs")
    print(f"{len(cases)} portable and {len(captures)} captured sweep cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
