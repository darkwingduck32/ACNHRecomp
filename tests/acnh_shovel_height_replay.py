"""Replay the original shovel height dispatcher with declared sampler and scene calls."""
import argparse
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_S0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x15e026c, 0x464
POINT, OUT, OWNER_SLOT, OWNER, GRID, GRID_FLAGS = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000, 0x25000
SCENE, FLAGS, ENV_SLOT, ENV, AREA, ACTOR, LINK, HOLDER, PARAMETERS, VT, DB = (
    0x26000, 0x27000, 0x28000, 0x29000, 0x2a000, 0x2b000, 0x2c000, 0x2d000,
    0x2e000, 0x30000, 0x31000)
ACCESSOR = 0x60cba4
CACHE = BASE + 0x4bfab80
ROW_TABLE, ROW_HEADER, ROW_DATA = 0x35000, 0x36000, 0x37000
TRACE = re.compile(r"acnh shovel height (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")


def compare_height_live(paths, tile, entries):
    compare_live(paths, tuple(tile) if tile else None, entries, TRACE)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def pointer(value):
    return struct.pack("<Q", value)


def fixture(module, field_record, field_column, types, flags, stage, wet, mode):
    base_kind, overlay_kind = types
    owner, grid, env, area, actor, holder, parameters = (
        bytearray(size) for size in (0xb0, 0x78, 0x58, 0x120, 0x288, 0x4b0, 0x1588))
    struct.pack_into("<Q", owner, 0xa8, GRID)
    struct.pack_into("<II", grid, 0x38, 256, 256)
    struct.pack_into("<I4xQ", grid, 0x68, 1 if mode != 2 else 65536, GRID_FLAGS)
    struct.pack_into("<Q", env, 0x50, AREA)
    struct.pack_into("<Q", area, 0x118, ACTOR)
    struct.pack_into("<Q", actor, 0x280, LINK if stage != 2 else 0)
    struct.pack_into("<Qf", holder, 0x488, VT, 0)
    struct.pack_into("<f", holder, 0x4ac, 3)
    struct.pack_into("<Q", parameters, 0x330, VT)
    struct.pack_into("<f", parameters, 0x354, 2)
    struct.pack_into("<Q", parameters, 0x1560, VT)
    struct.pack_into("<f", parameters, 0x1584, 1)
    flag_memory = bytearray(16)
    struct.pack_into("<I", flag_memory, 8, flags)
    database = bytearray(0x1d0)
    struct.pack_into("<I4xQI", database, 8 + 0x1b0, 10, ROW_TABLE, 10)
    header = bytearray(0x14)
    struct.pack_into("<H", header, 8, 1)
    struct.pack_into("<II", header, 0xc, 0x58da05ed, 0x20)
    rows = bytearray(10*0x30)
    addresses = []
    for i in range(10):
        address = ROW_DATA + i*0x30
        addresses.append(address)
        struct.pack_into("<I", rows, i*0x30, address - ROW_HEADER)
        rows[i*0x30+0x20] = int(i == 3 or (i == 8 and mode == 1))
    memory = {
        POINT: struct.pack("<3f", 0, 4, 300 if mode != 1 else -300), OUT: struct.pack("<I", 0xdeadbeef),
        BASE+0x4ab27c8: pointer(OWNER_SLOT), OWNER_SLOT: pointer(OWNER), OWNER: bytes(owner),
        GRID: bytes(grid), GRID_FLAGS + 4 * 60 * 256: struct.pack("<I", 8 if wet else 0),
        BASE+0x4ab6298: pointer(SCENE), SCENE: struct.pack("<h", -1 if stage == 0 else 1),
        BASE+0x4ac39f8: pointer(FLAGS), FLAGS: bytes(flag_memory),
        BASE+0x4ab2200: pointer(ENV_SLOT), ENV_SLOT: pointer(0 if stage == 1 else ENV),
        ENV: bytes(env), AREA: bytes(area), ACTOR: bytes(actor), LINK: pointer(0)+pointer(HOLDER),
        HOLDER: bytes(holder), PARAMETERS: bytes(parameters), VT: bytes(0xf0)+pointer(BASE+ACCESSOR),
        BASE+0x4bfab78: pointer(DB), BASE+0x4ab5d10: pointer(DB+8), DB: bytes(database),
        ROW_TABLE: struct.pack("<10Q", *addresses), ROW_HEADER: bytes(header),
        ROW_DATA: bytes(rows), CACHE: bytes(60),
    }
    def sample(kind, height):
        return words(*address_words(8, kind), 0xb9000048,
                     *address_words(8, bits(height)), 0x1e270100, RETURN)
    callees = {}
    for site, target, body in (
        (0x15e02dc, 0x83b024, sample(base_kind, -4.5)),
        (0x15e02f4, 0x83a950, sample(overlay_kind, 7.5)),
        *[(site, 0x241e98c, words(*address_words(0, PARAMETERS), RETURN))
          for site in (0x15e0300, 0x15e067c)],
        *[(site, ACCESSOR, bytes(module.image[ACCESSOR:ACCESSOR+8]))
          for site in (0x15e0314, 0x15e0670, 0x15e0694)],
        *[(site, 0x8807cc, field_record)
          for site in (0x15e034c, 0x15e0394, 0x15e03e8, 0x15e0544, 0x15e058c)],
        *[(site, 0x8823a0, field_column)
          for site in (0x15e0370, 0x15e03a8, 0x15e0408, 0x15e0568, 0x15e05a0)],
    ):
        callees[BASE+site] = BASE+target, body
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
    sizes = {f.start: f.size for f in ehframe.parse(module)}
    if sizes.get(ENTRY) != SIZE:
        parser.error("unexpected shovel height boundary")
    compare_height_live(args.live, args.survey_tile, (ENTRY,))
    field_record = bytes(module.image[0x8807cc:0x8807cc + sizes[0x8807cc]])
    field_column = bytes(module.image[0x8823a0:0x8823a0 + sizes[0x8823a0]])
    cases = []
    for types, flags, stage, mode, controls in itertools.product(
            ((3,8),(4,8),(3,9),(3,3)), (0,2,8,10), (0,1,2,3), (0,1,2), (0,1,2)):
        use_output = controls != 0
        query_flags = int(controls == 2)
        wet = controls == 2
        memory, callees = fixture(module, field_record, field_column, types, flags, stage, wet, mode)
        regs = {UC_ARM64_REG_X0: POINT, UC_ARM64_REG_X1: OUT if use_output else 0,
                UC_ARM64_REG_X2: query_flags, UC_ARM64_REG_S0: bits(1.25)}
        cases.append((types, controls, memory, callees, regs))
    payload = []
    for types, controls, memory, _, _ in cases:
        pairs = [(a+i, byte) for a,data in memory.items() for i,byte in enumerate(data)]
        payload.append(" ".join(map(str, [*types, controls, len(pairs), *(value for pair in pairs for value in pair)])))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload)+"\n",
                            text=True, capture_output=True, check=True, timeout=60).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    for i, ((_, _, memory, callees, regs), line) in enumerate(zip(cases,native)):
        outputs = (UC_ARM64_REG_S0,)
        reference = run(BASE+ENTRY, code, regs, outputs, memory, callees=callees)
        if reference != run(BASE+ENTRY, code, regs, outputs, memory, callees=callees):
            raise SystemExit(f"unstable height case {i}")
        expected = [reference.registers[UC_ARM64_REG_S0], *struct.unpack("<I", reference.memory[OUT]),
                    *struct.unpack("<15I", reference.memory[CACHE]),
                    *(site-BASE for site,_,_ in reference.calls)]
        actual = list(map(int,line.split()))
        if actual != expected:
            raise SystemExit(f"case {i}: original {expected}, C++ {actual}")
    print(f"{ENTRY:#x}: {len(cases)} cases match both AArch64 runs")


if __name__ == "__main__":
    main()
