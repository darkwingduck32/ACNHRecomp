"""Replay the shovel's terrain setup and layer-one field predicate."""
import argparse
import itertools
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.acnh_shovel_terrain_fallbacks_replay import (
    BASE, BUILD, POINT, OWNER, GRID, TERRAIN, CONTEXT, NAME_TARGET, NAME,
    fixture as field_fixture, pointer,
)
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

ENTRIES = ((0x87b460, 0x120), (0x1284ee0, 0x15c))
ROUTES = ((0x87b544, 0x1285040, 0x14c), (0x87b56c, 0x170cee0, 4), (0x87b57c, 0x1284ee0, 0x15c))
OWNER_SLOT, CONTEXT_SLOT, ALT_STAGE = 0x30000, 0x31000, 0x32000


def fixture(mode, layout, point):
    memory = field_fixture(2, mode % 4, layout, point)
    owner = bytearray(memory[OWNER])
    grid_node, = struct.unpack_from("<Q", owner, 0x1f8)
    struct.pack_into("<Q", owner, 0x38, grid_node)
    memory[OWNER] = bytes(owner)
    terrain = bytearray(memory[TERRAIN])
    struct.pack_into("<Q", terrain, 0x90, 1 if mode == 7 else GRID)
    memory[TERRAIN] = bytes(terrain)
    memory[BASE+0x4ab27a8] = pointer(OWNER_SLOT)
    memory[OWNER_SLOT] = pointer(OWNER)
    memory[BASE+0x4ab2720] = pointer(CONTEXT_SLOT)
    memory[CONTEXT_SLOT] = pointer(0 if mode == 1 else CONTEXT)
    memory[BASE+0x4ac3a80] = pointer(ALT_STAGE)
    memory[BASE+0x54c04d8] = struct.pack("<I", 0xe820ff3d if mode == 5 else 0)
    memory[ALT_STAGE+0x38] = struct.pack("<I", 0xdb5d97b8 if mode == 6 else 0xe820ff3d if mode == 4 else 0)
    if mode == 0:
        memory[BASE+0x4bfae30] = pointer(0)
    return memory


def reference(module, number, mode, memory):
    entry, size = ENTRIES[number]
    code = bytes(module.image[entry:entry+size])
    callees = {}
    if not number:
        callees = {
            BASE+0x87b4bc: (NAME_TARGET, words(*address_words(0, NAME), RETURN)),
            BASE+0x87b4c8: (BASE+0x5bd550, words(*address_words(0, 0 if mode == 3 else 0x758f7eaf), RETURN)),
        }
        for site, target, length in ROUTES:
            body = words(0x52800020, RETURN) if target == 0x170cee0 else bytes(module.image[target:target+length])
            callees[BASE+site] = BASE+target, body
    regs = {UC_ARM64_REG_X0: POINT}
    first = run(BASE+entry, code, regs, tuple(regs), memory, callees=callees)
    if first != run(BASE+entry, code, regs, tuple(regs), memory, callees=callees):
        raise ValueError(f"unstable terrain setup at {entry:#x}")
    return [first.registers[UC_ARM64_REG_X0], *(site-BASE for site, _, _ in first.calls),
            *(value for write in first.writes for value in write)]


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
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected terrain setup boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None,
                 tuple(entry for entry, _ in ENTRIES))
    geometry = ((0, 0), (2, 0), (0, 2), (-1, 0), (-4, -3), (80, 90), (16777217, -16777217))
    cases = [(number, mode, fixture(mode, layout, point))
             for number in range(len(ENTRIES))
             for mode, layout, point in itertools.product(range(8) if not number else (0,), range(6), geometry)]
    payload = []
    for number, mode, memory in cases:
        pairs = [(address+i, byte) for address,data in memory.items() for i,byte in enumerate(data)]
        payload.append(" ".join(map(str, [number, mode, len(pairs), *(v for pair in pairs for v in pair)])))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload)+"\n",
                            text=True, capture_output=True, check=True, timeout=45).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts = [0,0]
    for index, ((number, mode, memory), line) in enumerate(zip(cases,native)):
        expected = reference(module, number, mode, memory)
        actual = list(map(int, line.split()))
        if actual != expected:
            raise SystemExit(f"{ENTRIES[number][0]:#x} case {index}: original {expected}, candidate {actual}")
        counts[number] += 1
    for (entry, _), count in zip(ENTRIES, counts):
        print(f"{entry:#x}: {count} cases match both AArch64 runs")


if __name__ == "__main__":
    main()
