"""Replay the original shovel intermediate check and its callback arguments."""
import argparse
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
import re
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_S0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import words, RETURN
from tests.acnh_shovel_point_classifier_replay import immed
from tests.function_replay import run
from tools.acnh_version import build_id
from tests.acnh_shovel_target_replay import compare_live
from tests.acnh_shovel_intermediate_fixtures import synthetic_cases

BUILD = "FF1D1C05670DB6021C85B624A710B963"
TRACE = re.compile(r"acnh shovel intermediate (original|cpp) entry=(15e9c80) (call=.* out=[0-9a-f]+)")
ENTRY = 0x15e9c80
SIZE=0x448
FRAME=0xc0
TAPE=0x80000000
REGS=(UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3,UC_ARM64_REG_S0)
SHAPES={
    0x15e9ccc:(0,4,8,"s0"),
    **{site:(0,4,8,"s0","w1","w2") for site in (0x15e9cec,0x15e9d68,0x15e9f9c)},
    **{site:(0,4,8,"h3","b32","b34","w1","w2","w4") for site in (0x15e9df8,0x15ea088)},
    **{site:("w0","hi","w1") for site in (0x15e9d2c,0x15e9d38,0x15e9fe0,0x15e9fec)},
    **{site:tuple(range(0,28,4)) for site in (0x15e9e68,0x15e9edc,0x15e9f80)},
}

@dataclass
class Capture:
    base: int
    stack: int
    regs: tuple
    memory: dict = field(default_factory=dict)
    calls: list = field(default_factory=list)
    answers: list = field(default_factory=list)
    result: int | None = None

    def add(self,address,data):
        for offset,value in enumerate(data):
            self.memory.setdefault(address+offset,value)

    def regions(self):
        current=None
        blocks={}
        for address,byte in sorted(self.memory.items()):
            if current is None or address!=current+1:
                anchor=address
                blocks[anchor]=bytearray()
            blocks[anchor].append(byte)
            current=address
        return {address:bytes(block) for address,block in blocks.items()}

def captures(path):
    rows={}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag,raw,*data = line.split()
        key=int(raw,16)
        if key>>32 != ENTRY:
            continue
        if tag=="HEIGHT_BEGIN":
            base,resume,stack,*values=(int(s,16) for s in data)
            rows[key]=Capture(base,stack,(*values[:4],values[-1]))
            continue
        cap=rows[key]
        if tag=="HEIGHT_BYTES" and data[0]=="arg":
            cap.add(int(data[2],16),bytes.fromhex(data[3]))
        elif tag=="HEIGHT_LOAD":
            addr,size,value=int(data[0],16),int(data[1]),int(data[2],16)
            if not cap.stack-FRAME<=addr<cap.stack:
                cap.add(addr,(value%(1<<(8*size))).to_bytes(size,"little"))
        elif tag=="HEIGHT_CALL":
            cap.calls.append(tuple(int(s,16) for s in data[:2]))
        elif tag=="HEIGHT_RETURN":
            site,ret,bits=(int(s,16) for s in data)
            if cap.calls[len(cap.answers)][0]!=site:
                raise ValueError("captured call did not return in order")
            cap.answers.append((ret,bits))
        elif tag=="HEIGHT_END":
            cap.result=int(data[0],16)
    return rows

def shape_ops(site,i):
    ops=immed(9,TAPE+16+40*i)
    for j,value in enumerate(SHAPES[site]):
        register=10
        if isinstance(value,int):
            ops.append(0xb940000a | ((value//4)<<10))
        elif value=="s0":
            ops.append(0x1e26000a)
        elif value=="h3":
            ops.append(0x7940006a)
        elif value in ("b32","b34"):
            ops.append(0x3940006a | (int(value[-1])<<10))
        elif value=="hi":
            ops.append(0xd360fc0a)
        else:
            register=int(value[1:])
        ops.append(0xb9000120 | (j<<10) | register)
    return ops

def callees(cap):
    by_target=defaultdict(list)
    for i,((site,target),(x0,s0)) in enumerate(zip(cap.calls,cap.answers,strict=True)):
        body=shape_ops(site,i)+immed(9,TAPE)+immed(10,i+1)+[0xb900012a]
        body.extend(immed(0,x0)+immed(8,s0)+[0x1e270100,RETURN])
        by_target[target].append((site,i,body))
    result={}
    for target,bodies in by_target.items():
        dispatch=immed(9,TAPE)+[0xb9400128]
        branches=[]
        for _,i,_ in bodies:
            dispatch.append(0x7100011f | (i<<10))  # cmp w8, #i
            branches.append(len(dispatch))
            dispatch.append(0)
        dispatch.append(0xd4200000)
        for branch,(_,_,body) in zip(branches,bodies):
            dispatch[branch]=0x54000000 | ((len(dispatch)-branch)<<5)
            dispatch.extend(body)
        for site,_,_ in bodies:
            result[cap.base+site]=(cap.base+target,words(*dispatch))
    return result

def payload(cap):
    point,w1,w2,w3,radius=cap.regs
    flags=(w1&1) | ((w2&1)<<1) | ((w3&1)<<2)
    values=[cap.base,point,radius,flags,len(cap.memory),len(cap.calls)]
    values.extend(n for pair in cap.memory.items() for n in pair)
    for (site,_),(result,height) in zip(cap.calls,cap.answers,strict=True):
        values.extend((site,result,height))
    return " ".join(map(str,values))

def expected(image,cap):
    mem=cap.regions()
    mem[TAPE]=bytes(16+40*len(cap.calls))
    ext=callees(cap)
    regs=dict(zip(REGS,cap.regs))
    body=bytes(image.image[ENTRY:ENTRY+SIZE])
    first=run(cap.base+ENTRY,body,regs,(UC_ARM64_REG_X0,),mem,callees=ext)
    if first!=run(cap.base+ENTRY,body,regs,(UC_ARM64_REG_X0,),mem,callees=ext):
        raise ValueError("unstable original")
    if cap.result!=first.registers[UC_ARM64_REG_X0]:
        raise ValueError("original result differs from capture")
    if [(site-cap.base,entry-cap.base) for site,entry,_ in first.calls]!=cap.calls:
        raise ValueError("original call sequence differs")
    trace=first.memory[TAPE]
    outputs=[cap.result,len(first.calls)]
    for i,(site,_) in enumerate(cap.calls):
        start=16+40*i
        outputs.extend(int.from_bytes(trace[j:j+4],"little") for j in range(start,start+4*len(SHAPES[site]),4))
    return outputs

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
    image = nso.load(args.module)
    if not any((fn.start, fn.size) == (ENTRY, SIZE) for fn in ehframe.parse(image)):
        parser.error("unexpected intermediate function boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,), TRACE)
    rows = synthetic_cases(Capture)
    if args.capture:
        observed = captures(args.capture)
        if not observed:
            parser.error("no intermediate captures")
        rows.update(observed)
    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    result = subprocess.run(command, text=True, capture_output=True, timeout=30,
                            input="\n".join(map(payload, rows.values())) + "\n")
    if result.returncode:
        raise SystemExit(result.stderr)
    lines=result.stdout.splitlines()
    if len(lines)!=len(rows):
        raise ValueError("wrong case count")
    for (key,cap),line in zip(rows.items(),lines):
        ref=expected(image,cap)
        actual=list(map(int,line.split()))
        if ref!=actual:
            raise ValueError(f"{key=}: original {ref}; C++ {actual}")
    print(f"{len(lines)} intermediate cases match both original AArch64 runs and child shapes")

if __name__=="__main__":
    main()
