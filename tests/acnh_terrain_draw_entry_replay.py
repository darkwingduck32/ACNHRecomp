"""Compare terrain draw entry preparation before its stock list branch."""
import argparse
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BASE, BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


ENTRY, JOIN = BASE+0x146b0a0, BASE+0x146b0e8
MANAGER, DESC, PASS, INDEX, HEAD, CONTEXT, ROWS = range(0x21000,0x28000,0x1000)
STACK, BINDING = 0x31000,0x32000


def fixtures():
    for pass_id,index,count,first,alias in product(
        (0,2,9,12,15,16,255), (0,1,3,0xffff), (0,3),
        (HEAD-0x51,HEAD,HEAD+0x123), (False,True)
    ):
        context = STACK+0x18-0x498 if alias else CONTEXT
        data = {}
        for address, form, value in (
            (DESC,"Q",PASS), (DESC+8,"Q",INDEX), (PASS+0x21,"B",pass_id),
            (INDEX+0x16,"H",index), (HEAD+8,"Q",first), (context+0x498,"I",count),
            (context+0x4a0,"Q",ROWS)
        ):
            put(data,address,form,value)
        yield context,data


def original(image,context,data):
    machine = Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    for address in {ENTRY & -0x1000, STACK, *(a & -0x1000 for a in data)}:
        machine.mem_map(address,0x1000)
    machine.mem_write(ENTRY,bytes(image[ENTRY-BASE:JOIN-BASE]))
    for address,value in data.items():
        machine.mem_write(address,bytes([value]))
    inputs = MANAGER,DESC,HEAD,context,BINDING
    for register,value in zip(X[:5],inputs,strict=True):
        machine.reg_write(register,value)
    for register,value in ((X[29],STACK+0x70),(r.UC_ARM64_REG_SP,STACK),(r.UC_ARM64_REG_NZCV,0x90000000)):
        machine.reg_write(register,value)
    events = []

    def access(uc,kind,address,width,value,_):
        store = kind == UC_MEM_WRITE
        raw = value & ((1<<(8*width))-1) if store else int.from_bytes(uc.mem_read(address,width),"little")
        events.append(("S" if store else "L",address,width,raw))

    machine.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
    machine.emu_start(ENTRY,JOIN,count=24)
    assert machine.reg_read(r.UC_ARM64_REG_PC)==JOIN
    assert tuple(machine.reg_read(reg) for reg in X[:5])==inputs
    assert len(events)==11 and sum(op=="S" for op,*_ in events)==4
    outputs=tuple(machine.reg_read(reg) for reg in X[8:14])
    flags=machine.reg_read(r.UC_ARM64_REG_NZCV)
    assert flags == (0x60000000 if outputs[0]==HEAD else 0x20000000 if outputs[0]>HEAD else 0x80000000)
    return outputs,events,flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module",type=Path)
    parser.add_argument("candidate",type=Path)
    args = parser.parse_args()
    assert build_id(args.module)==BUILD
    image = nso.load(args.module).image
    samples = list(fixtures())
    payload = [" ".join(map(str,(MANAGER,DESC,HEAD,context,BINDING,STACK,len(data),
                                  *(v for pair in data.items() for v in pair)))) for context,data in samples]
    lines = subprocess.run([str(args.candidate.resolve())],input="\n".join(payload)+"\n",
                           capture_output=True,text=True,check=True).stdout.splitlines()
    for case,line in zip(samples,lines,strict=True):
        first = original(image,*case)
        assert first==original(image,*case)
        out,trace,_=first
        values=line.split()
        assert tuple(map(int,values[:6]))==out
        fields=values[6:]
        result=[(op,int(address),int(width),int(value))
                for op,address,width,value in zip(fields[::4],fields[1::4],fields[2::4],fields[3::4],strict=True)]
        assert result==trace
    print(f"{len(samples)} exact original pairs, ordered reads and frame writes, including "
          f"{len(samples)//2} early-binding alias controls.")


if __name__ == "__main__":
    main()
