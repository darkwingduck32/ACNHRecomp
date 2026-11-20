"""Replay stage-specific height and the effects of its declared callbacks."""
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
import argparse
import re
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0

from recomp import nso, ehframe
from tests.acnh_provider_cell_replay import words, RETURN
from tests.acnh_shovel_point_classifier_replay import immed
from tests.acnh_shovel_stage_height_fixtures import synthetic_cases
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
ENTRY, SIZE, FRAME_SIZE = 0x15e06d0, 0x754, 0xb0
REGISTERS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0)
BUFFER_REGS = {0x15e0720: 1, 0x15e0794: 1, 0x15e081c: 0, 0x15e08c0: 1, 0x15e0c50: 1, 0x15e0cb8: 0}
TRACE = re.compile(r"acnh shovel stage height (original|cpp) entry=(15e06d0) (call=.* out=[0-9a-f]+)")


@dataclass
class Capture:
    entry: int
    base: int
    resume: int
    sp: int
    regs: tuple
    operands: dict = field(default_factory=dict)
    exit: dict = field(default_factory=dict)
    calls: list = field(default_factory=list)
    loads: list = field(default_factory=list)
    effects: dict = field(default_factory=dict)
    outcomes: dict = field(default_factory=dict)
    returned: tuple = ()

def read_file(path):
    captures = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, raw_id, *parts = line.split()
        id_ = int(raw_id, 16)
        if id_ >> 32 != ENTRY:
            continue
        if tag == "HEIGHT_BEGIN":
            base, resume, sp, *regs = (int(value,16) for value in parts)
            if id_ in captures:
                raise ValueError(f"duplicate capture {raw_id}")
            captures[id_] = Capture(id_>>32, base, resume, sp, tuple(regs))
        else:
            cap = captures[id_]
            if tag == "HEIGHT_LOAD":
                addr, width, val = parts
                cap.loads.append((int(addr,16), int(width), int(val,16)))
            elif tag == "HEIGHT_BYTES":
                kind, site, addr, data = parts
                target = {"arg": cap.operands, "exit": cap.exit, "child": cap.effects}[kind]
                target[int(site,16)] = (int(addr,16), bytes.fromhex(data))
            elif tag == "HEIGHT_CALL":
                cap.calls.append(tuple(int(value,16) for value in parts))
            elif tag == "HEIGHT_RETURN":
                site, *out = (int(value,16) for value in parts)
                cap.outcomes[site] = tuple(out)
            elif tag == "HEIGHT_END":
                cap.returned = tuple(int(value,16) for value in parts)
    return captures

def is_stack(cap, address):
    return cap.sp - FRAME_SIZE <= address < cap.sp

def memory_blocks(cap):
    seed = {}
    for addr,data in cap.operands.values():
        for at,byte in enumerate(data,addr):
            seed.setdefault(at,byte)
    for addr,width,val in cap.loads:
        if is_stack(cap, addr):
            continue
        for at,byte in enumerate((val % (1 << (8*width))).to_bytes(width,"little"),addr):
            seed.setdefault(at,byte)
    blocks = {}
    previous = 0
    for addr,value in sorted(seed.items()):
        if addr != previous + 1:
            anchor = addr
            blocks[anchor] = bytearray()
        blocks[anchor].append(value)
        previous = addr
    return {addr: bytes(data) for addr,data in blocks.items()}

def stub(cap, site, args):
    ops = []
    if site in cap.effects:
        addr,data = cap.effects[site]
        arg = BUFFER_REGS[site]
        if addr != args[arg] or len(data)%4:
            raise ValueError(f"unexpected captured output at {site:#x}")
        for i in range(0,len(data),4):
            ops.extend(immed(10,int.from_bytes(data[i:i+4],"little")))
            ops.append(0xb9000000 | ((i//4)<<10) | (arg<<5) | 10)
    x0,s0 = cap.outcomes[site]
    if is_stack(cap,x0):
        ops.append(0x910003e0 | ((x0-cap.sp+FRAME_SIZE)<<10))
    else:
        ops.extend(immed(0,x0))
    ops.extend(immed(8,s0))
    ops.extend((0x1e270100,RETURN))
    return ops

def dependencies(cap):
    targets = defaultdict(list)
    for site,target,*args in cap.calls:
        targets[target].append((site,stub(cap,site,args)))
    out = {}
    for target,bodies in targets.items():
        code = []
        dispatch = []
        for site,_ in bodies:
            code.extend(immed(9,cap.base+site+4))
            code.extend((0xeb0903df,0))
            dispatch.append(len(code)-1)
        code.append(0xd4200000)
        for offset,(_,instructions) in zip(dispatch,bodies):
            code[offset] = 0x54000000 | ((len(code)-offset)<<5)
            code.extend(instructions)
        binary = words(*code)
        for site,_ in bodies:
            out[cap.base+site] = (cap.base+target,binary)
    return out

def verify(module, cap):
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    output = UC_ARM64_REG_S0
    mem = memory_blocks(cap)
    external = dependencies(cap)
    regs = dict(zip(REGISTERS,cap.regs))
    ref = run(cap.base+cap.entry,code,regs,(output,),mem,callees=external)
    if ref != run(cap.base+cap.entry,code,regs,(output,),mem,callees=external):
        raise ValueError("unstable original reference")
    expected = cap.returned[1]
    if ref.registers[output] != expected:
        raise ValueError(f"unexpected return {ref.registers[output]:#x}, captured {expected:#x}")
    calls = [(site-cap.base,target-cap.base) for site,target,_ in ref.calls]
    if calls != [call[:2] for call in cap.calls]:
        raise ValueError(f"unexpected call sequence {calls}")
    for addr,data in cap.exit.values():
        block,data_after = next((a,d) for a,d in ref.memory.items() if a <= addr and addr+len(data) <= a+len(d))
        if data_after[addr-block:addr-block+len(data)] != data:
            raise ValueError(f"unexpected output bytes at {addr:#x}")
    return ref

def payload(cap):
    pairs = [(address + i, byte) for address, data in memory_blocks(cap).items() for i, byte in enumerate(data)]
    point, material, flags, stage, specials, _ = cap.regs
    fields = [cap.base, cap.sp - FRAME_SIZE, point, material, stage, flags, specials, len(pairs), len(cap.calls)]
    fields.extend(n for pair in pairs for n in pair)
    for site, _, *args in cap.calls:
        out, bits = cap.outcomes[site]
        address, data = cap.effects.get(site, (0, b""))
        words = [int.from_bytes(data[i:i+4], "little") for i in range(0, len(data), 4)]
        fields.extend([site, out, bits, address, len(words), *args[:5], *words])
    return " ".join(map(str, fields))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--no-op", action="store_true")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(module)):
        parser.error("unexpected stage-specific height boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,), TRACE)
    captures = synthetic_cases(Capture)
    if args.capture:
        observed = read_file(args.capture)
        if not observed:
            raise SystemExit("no stage-specific height captures")
        captures.update(observed)

    command = [str(args.candidate.resolve())]
    if args.no_op:
        command.append("no-op")
    result = subprocess.run(command, text=True, input="\n".join(map(payload, captures.values())) + "\n",
                            capture_output=True, timeout=30)
    if result.returncode:
        raise SystemExit(result.stderr)
    outputs = result.stdout.splitlines()
    if len(outputs) != len(captures):
        raise SystemExit("candidate returned the wrong number of cases")
    for (name, cap), line in zip(captures.items(), outputs):
        try:
            ref = verify(module, cap)
            expected = [cap.returned[1], len(ref.calls)]
            if 1 in cap.exit:
                expected.append(int.from_bytes(cap.exit[1][1], "little"))
            expected.extend(v for write in ref.writes for v in write)
            if list(map(int, line.split())) != expected:
                raise ValueError(f"original {expected}; C++ {line}")
        except Exception as error:
            raise RuntimeError(f"case {name}: {error}") from error
    print(f"{ENTRY:#x}: {len(outputs)} cases match both original AArch64 runs")


if __name__ == "__main__":
    main()
