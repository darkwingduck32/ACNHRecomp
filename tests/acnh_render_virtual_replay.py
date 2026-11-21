"""Compare the memory operations around the selector's generated virtual calls."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


PRE, POST = 0x38eb068, 0x38eb0d4
END_PRE, END_POST, LOW_FF, SENTINEL = 0x38eb0b4, 0x38eb4bc, 0x38eb110, 0x38eb19c
PAGE = PRE & -0x1000
STATE, SLOTS, RESOURCE, RECORD, OBJECT, SINK, BASE, STACK = range(0x21000,0x29000,0x1000)
PRE_REGS = (0,8,9,10,11,21,23,24,25,26,27,28)
POST_REGS = (8,9,10,11,22,23,24)


def fixtures():
    for word,count,alias,sink in product(
        (42,0xffffff05,0xffffffff,0x7fffffff), (0,0xffffffff), (0,1,2), (SINK,0xffffffff)
    ):
        data = {}
        record = STACK+0x18-0xbc if alias==1 else RECORD
        object = STACK+0x18-0x30 if alias==2 else OBJECT
        for addr,width,value in (
            (STATE+0x1c8,"Q",record), (SLOTS+8,"Q",RESOURCE), (RESOURCE+0x58,"Q",object),
            (STATE+0x18,"Q",sink), (object+0x70,"Q",BASE), (record+0xbc,"I",word),
            (object+0x39,"B",255), (object+0x3a,"H",65535), (object+0x30,"I",count)
        ):
            put(data,addr,width,value)
        yield (0,STATE,SLOTS,STACK,0,0,0,0,0,0,0,0), data
    for word, (scale,stride), count, first, second, offset in product(
        (0,0xffffff05,0xffffff00,0x100000ff,0xffffffff), ((0,1),(255,65535)),
        (0,0xffffffff), (0,0x100,0xffffffff), (0,0x100,0xffffffff), (0xbc,0xdc)
    ):
        data = {}
        field = RECORD+offset
        for addr,width,value in ((field,"I",word), (STACK+0x18,"Q",SINK),
                                 (OBJECT+0x30,"I",count ^ 0x101), (SINK+0xb8,"Q",0x55555)):
            put(data,addr,width,value)
        yield (1,0,0,STACK,OBJECT,field,BASE,scale,stride,count,first,second),data


def original(image, inputs, data):
    post,state,slots,stack,object,field,base,scale,stride,count,first,second = inputs
    machine = Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    for page in {PAGE, STACK, *(address&~0xfff for address in data)}:
        machine.mem_map(page,0x1000)
    machine.mem_write(PAGE,bytes(image[PAGE:PAGE+0x1000]))
    for address,value in data.items():
        machine.mem_write(address,bytes([value]))
    for index,register in enumerate(X):
        machine.reg_write(register,0xabcd000000000000+index)
    for register,value in ((X[19],state),(X[20],slots),(X[29],stack+0x30),(X[21],object),
                           (X[24],(field-RECORD) if post else 0),(X[25],RECORD),(X[23],base),
                           (X[26],scale),(X[27],stride),(X[28],count),(X[22],first),
                           (X[0],0x4321000000000000+second),(r.UC_ARM64_REG_SP,stack),
                           (r.UC_ARM64_REG_NZCV,0x90000000)):
        machine.reg_write(register,value)
    events, endpoints = [], []
    instructions = 0

    def code(uc,pc,size,_):
        nonlocal instructions
        if pc in (END_PRE,END_POST,LOW_FF,SENTINEL):
            endpoints.append(pc)
            uc.emu_stop()
        else:
            assert (PRE <= pc < END_PRE) if not post else (POST <= pc < LOW_FF)
            instructions += 1

    def access(uc,kind,address,width,value,_):
        store = kind == UC_MEM_WRITE
        raw = value&((1<<(8*width))-1) if store else int.from_bytes(uc.mem_read(address,width),"little")
        events.append(("S" if store else "L",address,width,raw))

    machine.hook_add(UC_HOOK_CODE,code)
    machine.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
    machine.emu_start(POST if post else PRE,PAGE+0x1000,count=36)
    assert len(endpoints)==1
    outputs=tuple(machine.reg_read(X[i]) for i in (POST_REGS if post else PRE_REGS))
    flags=machine.reg_read(r.UC_ARM64_REG_NZCV)
    assert sum(op=="S" for op,*_ in events)==(0 if post else 2)
    if post:
        assert machine.reg_read(X[0])==0x4321000000000000+second
    return endpoints[0],outputs,flags,events,instructions


def cmn_one(word):
    result=(word+1)&0xffffffff
    return ((result>>31)<<31) | ((result==0)<<30) | ((word==0xffffffff)<<29) | ((word==0x7fffffff)<<28)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module",type=Path)
    parser.add_argument("candidate",type=Path)
    args=parser.parse_args()
    assert build_id(args.module)==BUILD
    image=nso.load(args.module).image
    samples=list(fixtures())
    payload=[" ".join(map(str,(*inputs,len(data),*(word for pair in data.items() for word in pair))))
             for inputs,data in samples]
    lines=subprocess.run([str(args.candidate.resolve())],input="\n".join(payload)+"\n",
                         capture_output=True,check=True,text=True).stdout.splitlines()
    counts=Counter()
    instruction_counts={}
    for (inputs,data),line in zip(samples,lines,strict=True):
        reference=original(image,inputs,data)
        assert reference==original(image,inputs,data)
        endpoint,state,flags,events,instructions=reference
        words=line.split()
        start=8 if inputs[0] else 14
        candidate=tuple(map(int,words[1:start]))
        observed=[(words[i],*(int(n) for n in words[i+1:i+4])) for i in range(start,len(words),4)]
        assert observed==events[:len(observed)]
        assert bool(int(words[0]))==(endpoint in (END_PRE,END_POST))
        counts[endpoint]+=1
        if endpoint==END_PRE:
            assert candidate[:-1]==state and cmn_one(candidate[-1])==flags and observed==events
            instruction_counts[endpoint]=instructions
        elif endpoint==END_POST:
            assert candidate==state and flags==0 and observed==events
            instruction_counts[endpoint]=instructions
    assert set(counts)=={END_PRE,END_POST,LOW_FF,SENTINEL}
    print(f"{len(samples)} twice-original cases. {counts[END_PRE]} ordered preparations at "
          f"{instruction_counts[END_PRE]} instructions, {counts[END_POST]} ordinary post-call joins at "
          f"{instruction_counts[END_POST]} instructions, {counts[LOW_FF]+counts[SENTINEL]} stock fallbacks.")


if __name__ == "__main__":
    main()
