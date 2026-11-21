"""Check setup before the generated first-node test in the terrain drawer."""
import argparse
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BASE, BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


ENTRY, JOIN = BASE+0x146b0ec, BASE+0x146b19c
HEAD, DESC, MANAGER, HEADER, STACK = range(0x21000,0x26000,0x1000)
MASK = 2**64-1


def fixtures():
    for pass_id, displacement, index, flags, alias in product(
            (0,2,9,12,255), (-0x80000000,-24,0,8,0x7fffffff), (0,3,0xffff),
            (0x20000000,0x90000000), (False,True)):
        data = {}
        header=STACK if alias else HEADER
        for addr,form,value in ((STACK+0x58,"Q",pass_id),(STACK+0x60,"Q",MANAGER),(STACK,"Q",header),
                                (HEAD+0x14,"i",displacement),(header+8,"Q",0x88aa55bb3355)):
            put(data,addr,form,value)
        yield HEAD,HEAD+0x253,2**64-0x110,index,STACK,flags,data


def original(image,case):
    head,first,rows,index,sp,flags,data=case
    fp=sp+0x70
    machine=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    for page in {ENTRY&-0x1000,*(address&-0x1000 for address in data)}:
        machine.mem_map(page,0x1000)
    machine.mem_write(ENTRY,bytes(image[ENTRY-BASE:JOIN-BASE]))
    for address,value in data.items():
        machine.mem_write(address,bytes([value]))
    for register,value in ((X[1],DESC),(X[2],head),(X[8],first),(X[9],rows),(X[10],index),
                           (X[29],fp),(r.UC_ARM64_REG_SP,sp),(r.UC_ARM64_REG_NZCV,flags)):
        machine.reg_write(register,value)
    reads,stores=[],[]
    instructions = 0

    def code(uc,pc,size,_):
        nonlocal instructions
        instructions += 1

    def access(uc,kind,address,width,value,_):
        if kind==UC_MEM_WRITE:
            stores.append((address,width,value&((1<<(8*width))-1)))
        else:
            assert not stores
            reads.append((address,width,int.from_bytes(uc.mem_read(address,width),"little")))

    machine.hook_add(UC_HOOK_CODE,code)
    machine.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
    machine.emu_start(ENTRY,JOIN,count=40)
    assert machine.reg_read(r.UC_ARM64_REG_PC)==JOIN and machine.reg_read(r.UC_ARM64_REG_NZCV)==flags
    assert machine.reg_read(X[1])==DESC and machine.reg_read(X[2])==head
    assert machine.reg_read(X[20])==DESC and machine.reg_read(X[21])==0xffffffff
    assert instructions == 26
    scratch=tuple(machine.reg_read(reg) for reg in X[8:16])+(machine.reg_read(X[19]),)
    return scratch,reads,stores


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module",type=Path)
    parser.add_argument("candidate",type=Path)
    args=parser.parse_args()
    assert build_id(args.module)==BUILD
    image=nso.load(args.module).image
    samples=list(fixtures())
    payload=[" ".join(map(str,(*case[:5],len(case[-1]),*(word for pair in case[-1].items() for word in pair))))
             for case in samples]
    result=subprocess.run([str(args.candidate.resolve())],input="\n".join(payload)+"\n",
                          capture_output=True,check=True,text=True).stdout.splitlines()
    for case,line in zip(samples,result,strict=True):
        stock=original(image,case)
        assert stock==original(image,case)
        scratch,reads,stores=stock
        a,b,row,row_flags,manager,binding,signed_pass,pass_id,offset,gated,*raw=map(int,line.split())
        assert scratch==(row_flags,row,-offset&MASK,0x108,gated,pass_id,binding,offset,a)
        assert reads==list(zip(raw[::3],raw[1::3],raw[2::3],strict=True))
        sp=case[4]
        assert stores==[(sp+8,8,signed_pass),(sp+0x68,8,manager),(sp+0x40,8,b),
                        (sp+0x10,8,binding),(sp+0x24,4,gated),(sp+0x48,8,-offset&MASK),
                        (sp+0x50,8,offset),(sp+0x28,8,row_flags)]
    print(f"{len(samples)} nonempty originals matched twice with all five ordered reads, "
          f"eight frame values, signed link displacement and exact scratch.")


if __name__=="__main__":
    main()
