"""Replay the original shovel step check, including callback argument bytes."""
import argparse
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
import re
import subprocess

from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0,
)

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import words, RETURN
from tests.acnh_shovel_point_classifier_replay import immed
from tests.acnh_shovel_step_fixtures import synthetic_cases
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
ENTRY, SIZE, FRAME_SIZE = 0x15f0c64, 0x484, 0xd0
BUFFER_REGS = {0x15f10bc: 1}
REGISTERS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0)
TRACE = re.compile(r"acnh shovel step (original|cpp) entry=(15f0c64) (call=.* out=[0-9a-f]+)")

TAPE = 0x80000000
SHAPES = {
    0x15f0d14: ("s0", "s1"),  # heading vector
    **{site: tuple(range(0, 28, 4)) for site in (0x15f0f10, 0x15f0f48, 0x15f0fc4, 0x15f1024, 0x15f1088)},
    **{site: (0,4,8,"s0","w1","w2","w3") for site in (0x15f0fe0,0x15f1040,0x15f10a4)},
    0x15f10bc: tuple(range(0,32,4)),
}
SLOTS = {site: 32*i for i,site in enumerate(SHAPES)}

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

def position_trace(site):
    if site not in SHAPES:
        return []
    ops = immed(9,TAPE+SLOTS[site])
    for number, field in enumerate(SHAPES[site]):
        if isinstance(field,int):
            ops.append(0xb940000a | ((field//4)<<10))
            register = 10
        elif field.startswith("s"):
            ops.append(0x1e26000a | (int(field[1:])<<5))
            register = 10
        else:
            register = int(field[1:])
        ops.append(0xb9000120 | (number<<10) | register)
    return ops

def dependencies(capture):
    targets = defaultdict(list)
    for site,target,*args in capture.calls:
        targets[target].append((site,position_trace(site)+stub(capture,site,args)))
    output = {}
    for target, bodies in targets.items():
        code = []
        slots = []
        for site,_ in bodies:
            code.extend(immed(9,capture.base+site+4))
            code.extend((0xeb0903df,0))
            slots.append(len(code)-1)
        code.append(0xd4200000)
        for branch,(_,body) in zip(slots,bodies):
            code[branch] = 0x54000000 | ((len(code)-branch)<<5)
            code.extend(body)
        binary = words(*code)
        output.update({capture.base+site:(capture.base+target,binary) for site,_ in bodies})
    return output

def payload(cap):
    pairs = [(address+i, byte) for address,data in memory_blocks(cap).items() for i,byte in enumerate(data)]
    data = [*cap.regs[:2],len(pairs),len(cap.calls)]
    data.extend(n for pair in pairs for n in pair)
    for site,_,*_ in cap.calls:
        address, effect = cap.effects.get(site,(0,b""))
        writes = [int.from_bytes(effect[i:i+4],"little") for i in range(0,len(effect),4)]
        data.extend((site,cap.outcomes[site][0],address,len(writes),*writes))
    return " ".join(map(str,data))

def verify(module,cap):
    mem = memory_blocks(cap)
    mem[TAPE] = bytes(32*len(SHAPES))
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    regs = dict(zip(REGISTERS,cap.regs))
    callee_stubs = dependencies(cap)
    ref = run(cap.base+ENTRY,code,regs,(UC_ARM64_REG_X0,),mem,callees=callee_stubs)
    if ref != run(cap.base+ENTRY,code,regs,(UC_ARM64_REG_X0,),mem,callees=callee_stubs):
        raise ValueError("unstable original")
    if ref.registers[UC_ARM64_REG_X0] != cap.returned[0]:
        raise ValueError("original return differs from capture")
    if [(site-cap.base,target-cap.base) for site,target,_ in ref.calls] != [row[:2] for row in cap.calls]:
        raise ValueError("original call order differs from capture")
    output = [ref.registers[UC_ARM64_REG_X0],len(ref.calls)]
    if address := cap.regs[1]:
        base,data = next((start,data) for start,data in ref.memory.items() if start<=address<start+len(data))
        data = data[address-base:address-base+12]
        if cap.exit and data != cap.exit[1][1]:
            raise ValueError("original output differs from capture")
        output.extend(int.from_bytes(data[i:i+4],"little") for i in range(0,12,4))
    tape = ref.memory[TAPE]
    for site,_,*_ in cap.calls:
        if site in SHAPES:
            start = SLOTS[site]
            output.extend(int.from_bytes(tape[i:i+4],"little") for i in range(start,start+4*len(SHAPES[site]),4))
    output.extend(n for w in ref.writes if not TAPE<=w[0]<TAPE+len(tape) for n in w)
    return output

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
    if not any((fn.start, fn.size) == (ENTRY, SIZE) for fn in ehframe.parse(module)):
        parser.error("unexpected step function boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,), TRACE)
    captures = synthetic_cases(Capture)
    if args.capture:
        observed = read_file(args.capture)
        if not observed:
            raise SystemExit("no step captures")
        captures.update(observed)

    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    result = subprocess.run(command, text=True, input="\n".join(map(payload, captures.values())) + "\n",
                            capture_output=True, timeout=30)
    if result.returncode:
        raise SystemExit(result.stderr)
    outputs = result.stdout.splitlines()
    if len(outputs) != len(captures):
        raise SystemExit("candidate returned the wrong number of cases")
    for (key, cap), line in zip(captures.items(), outputs):
        try:
            expected = verify(module, cap)
        except Exception as error:
            raise ValueError(f"case {key}: {error}") from error
        actual = list(map(int, line.split()))
        if actual != expected:
            diff = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), min(len(expected), len(actual)))
            raise SystemExit(f"case {key}, word {diff}: original {expected}; C++ {actual}")
    print(f"{ENTRY:#x}: {len(outputs)} cases match both original AArch64 runs and callback shapes")


if __name__ == "__main__":
    main()
