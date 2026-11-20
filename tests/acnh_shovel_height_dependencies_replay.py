"""Replay both terrain samplers and both height parameter accessors."""
import argparse
import itertools
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_S0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.acnh_shovel_height_replay import BASE, BUILD, bits, pointer, compare_height_live
from tests.function_replay import run
from tools.acnh_version import build_id

ENTRIES = ((0x83b024,0x168),(0x83a950,0x214),(0x241e98c,0x128),(0x60cba4,8))
OWNER, POINT, OUT, GRID, SCENE, FLAGS, VTABLE = (0x20000,0x21000,0x22000,0x23000,0x24000,0x25000,0x26000)
OBJECTS = (0x27000,0x28000,0x29000)
TARGETS = (BASE+0x2000,BASE+0x3000,BASE+0x4000)
SLOT, ENTRIES_BASE, TYPE_GUARD, TYPE_INFO = (0x2a000,0x2b000,0x2c000,0x2d000)
DEFAULT_GUARD = BASE+0x53f4258
DEFAULT_OBJECT = BASE+0x53f1368


def immediate(value):
    return words(*address_words(0, value), RETURN)


def samplers(number, mode, output, scene, list_size):
    owner = bytearray(0x88)
    struct.pack_into("<Q", owner, 0x80, GRID if mode != 0 else 0)
    struct.pack_into("<Q", owner, 0x50, OBJECTS[0]+0x20 if list_size else OWNER+0x48)
    struct.pack_into("<i", owner, 0x5c, 0x20)
    memory = {
        OWNER: bytes(owner), POINT: struct.pack("<3f", 1,2,3), OUT: struct.pack("<I",0xdeadbeef),
        BASE+0x4ab6298: pointer(SCENE), SCENE: struct.pack("<h",scene),
        BASE+0x4b487d4: struct.pack("<h",scene), BASE+0x4ac39f8: pointer(FLAGS),
        FLAGS: struct.pack("<III", 2 if mode & 1 else 0, 0, 2 if not mode & 1 else 0),
    }
    for i in range(list_size):
        address = OBJECTS[i]
        child = bytearray(0x30)
        struct.pack_into("<Q", child, 0, VTABLE+0x40*i)
        heights = (-550.0, -500.0, -900.0) if mode == 3 else (9.0, 13.0, 5.0)
        struct.pack_into("<IB3xf", child, 0x10, 8+i, mode == 1 and i == 1, heights[i])
        struct.pack_into("<Q", child, 0x28, OBJECTS[i+1]+0x20 if i+1 < list_size else OWNER+0x48)
        memory[address] = bytes(child)
        memory[VTABLE+0x40*i] = bytes(0x30)+pointer(TARGETS[i])
    regs = {UC_ARM64_REG_X0: OWNER, UC_ARM64_REG_X1: POINT, UC_ARM64_REG_X2: OUT if output else 0}
    callees = {}
    if number == 0:
        callees[BASE+0x83b0c8] = BASE+0x862700, words(
            0xb9400048, 0x11000508, 0xb9000048, *address_words(8,bits(4.5)), 0x1e270100, RETURN)
        callees[BASE+0x83b0e0] = BASE+0x87a980, immediate(5 if mode == 2 else 7)
    else:
        for site,target in ((0x83aa00,0x3f03880),(0x83aafc,0x3f038c0)):
            callees[BASE+site] = BASE+target, words(RETURN)
        callees[BASE+0x83ab0c] = BASE+0x87a980, immediate(5 if mode == 2 else 7)
        callees[BASE+0x83ab28] = BASE+0x87aa70, immediate(6 if mode == 3 else 7)
        # Each virtual call site has one declared dynamic target. The list
        # deliberately uses the same vtable entry for each member.
        for i in range(list_size):
            memory[VTABLE+0x40*i] = bytes(0x30)+pointer(TARGETS[0])
        for site in (0x83aa74,0x83aad4):
            callees[BASE+site] = TARGETS[0], words(
                0xb9401008, 0xb9000048, 0x39405008, 0x39000088, 0xbd401800, RETURN)
    return memory, callees, regs


def parameters(mode, index):
    memory = {
        BASE+0x4ab2f58: pointer(SLOT), SLOT: pointer(ENTRIES_BASE if mode != 1 else 0),
        ENTRIES_BASE: (pointer(0 if mode == 2 else OBJECTS[0])+pointer(0))*236,
        OBJECTS[0]: pointer(VTABLE), VTABLE: bytes(0x10)+pointer(TARGETS[0]),
        BASE+0x4ac3110: pointer(TYPE_GUARD), TYPE_GUARD: bytes([int(mode != 3)]),
        BASE+0x4ac3108: pointer(TYPE_INFO), TYPE_INFO: pointer(0),
        BASE+0x4ab3608: pointer(0x2f000), DEFAULT_GUARD: bytes([int(mode != 4)]),
        BASE+0x4ab2020: pointer(0x31000), BASE+0x4ac3100: pointer(0x32000),
    }
    calls = {
        BASE+0x241e9e4:(TARGETS[0], immediate(int(mode not in (4,5)))),
        BASE+0x241ea20:(BASE+0x3f030d0, immediate(int(mode in (3,4)))),
        BASE+0x241ea84:(BASE+0x3f030d0, immediate(int(mode in (3,4)))),
    }
    for site,target in ((0x241ea50,0x225e000),(0x241ea68,0x1d0),
                        (0x241ea74,0x3f030e0),(0x241eaac,0x3f030e0)):
        calls[BASE+site] = BASE+target, words(RETURN)
    return memory,calls,{UC_ARM64_REG_X0:index}


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
    sizes = {f.start:f.size for f in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry,size in ENTRIES):
        parser.error("unexpected sampler or accessor boundary")
    compare_height_live(args.live, args.survey_tile, tuple(entry for entry, _ in ENTRIES))
    cases = []
    for number,mode,output,scene,count in itertools.product(range(2),range(4),(0,1),(-1,1,-2,256),range(4)):
        cases.append((number,mode,*samplers(number,mode,output,scene,count)))
    for mode,index in itertools.product(range(6),(0,1,235,236,0xffffffff)):
        cases.append((2,mode,*parameters(mode,index)))
    for point in (0,0x10000,0xfffffffffffffff0):
        cases.append((3,0,{}, {}, {UC_ARM64_REG_X0:point}))
    payload = []
    for number,mode,memory,_,regs in cases:
        pairs = [(address+i,byte) for address,data in memory.items() for i,byte in enumerate(data)]
        payload.append(" ".join(map(str,[number,mode,regs[UC_ARM64_REG_X0],regs.get(UC_ARM64_REG_X2,0),
                                         len(pairs),*(v for pair in pairs for v in pair)])))
    native = subprocess.run([str(args.candidate.resolve())],input="\n".join(payload)+"\n",
                            text=True,capture_output=True,check=True,timeout=60).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts = [0]*len(ENTRIES)
    for i,((number,_,memory,callees,regs),line) in enumerate(zip(cases,native)):
        entry,size = ENTRIES[number]
        code = bytes(module.image[entry:entry+size])
        result = UC_ARM64_REG_S0 if number < 2 else UC_ARM64_REG_X0
        first = run(BASE+entry,code,regs,(result,),memory,callees=callees)
        if first != run(BASE+entry,code,regs,(result,),memory,callees=callees):
            raise SystemExit(f"unstable sampler case {i}")
        expected = [first.registers[result]]
        if number < 2:
            expected.append(struct.unpack("<I",first.memory[OUT])[0])
        elif number == 2:
            expected.append(struct.unpack("<Q",first.memory[TYPE_INFO])[0])
        expected.extend(site-BASE for site,_,_ in first.calls)
        actual = list(map(int,line.split()))
        if actual != expected:
            raise SystemExit(f"{entry:#x} case {i}: original {expected}, C++ {actual}")
        counts[number] += 1
    for (entry,_),count in zip(ENTRIES,counts):
        print(f"{entry:#x}: {count} cases match both AArch64 runs")


if __name__ == "__main__":
    main()
