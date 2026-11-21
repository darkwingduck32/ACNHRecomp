"""Check selector indirect submission up to the effectful original call."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


ENTRY, JOIN, DIRECT, SKIP = 0x38eadf4, 0x38eaec4, 0x38eae44, 0x38eae48
STOPS = (JOIN, DIRECT, SKIP)
PAGE = ENTRY & ~0xfff
STATE, SLOTS, RESOURCE, BINDING, SINK, STREAM = range(0x21000, 0x27000, 0x1000)
OUTPUTS = (0, 1, 2, 8, 9, 23)


def fixtures():
    for lane, descriptor, flags, receiver, seed in product(
            range(4), (0x100, 0x120, 0x130), (0, 1, 2, 3, 0xff), (0, 0x71000), range(2)):
        data = {}
        row = BINDING + 0x20 + lane * 0x260
        hint = 0 if not seed else 2
        direct_record = STREAM + (hint + (3 << 2)) * 0x38
        for address, form, value in (
            (SLOTS + 8,"Q",RESOURCE), (RESOURCE + 0x2e5,"B",0xa0 | lane),
            (STATE + 0x36,"H",descriptor), (row + 0x258,"B",flags),
            (STATE + 0x18,"Q",SINK), (SINK + 0xb8,"Q",receiver), (STATE + 0x21,"B",3),
            (STATE + 0x20,"B",7), (direct_record + 0x30,"Q",0x72000)
        ):
            put(data, address, form, value)
        yield hint, seed, data


def original(image, hint, seed, data, injection=None):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(PAGE, 0x1000)
    machine.mem_write(PAGE, bytes(image[PAGE:PAGE+0x1000]))
    for page in {address & ~0xfff for address in data}:
        machine.mem_map(page, 0x1000)
    for address, value in data.items():
        machine.mem_write(address, bytes([value]))
    initial = [0xabcdef0000000000 + 31 * seed + i for i in range(31)]
    for i,value in ((19,STATE), (20,SLOTS), (21,hint), (22,STREAM), (28,BINDING)):
        initial[i] = value
    if injection is not None:
        for i,value in zip(OUTPUTS,injection,strict=True):
            initial[i] = value
    for register,value in zip(X,initial,strict=True):
        machine.reg_write(register,value)
    flags = 0x90000000 if seed else 0x60000000
    machine.reg_write(r.UC_ARM64_REG_NZCV, flags)
    end, reads, writes = [], [], []
    instructions = 0

    def code(uc, pc, size, user):
        nonlocal instructions
        if pc in STOPS:
            end.append(pc)
            uc.emu_stop()
        else:
            assert ENTRY <= pc < JOIN
            instructions += 1

    def read(uc, access, address, size, value, user):
        assert all(byte in data for byte in range(address,address+size))
        reads.append((address,size))

    machine.hook_add(UC_HOOK_CODE,code)
    machine.hook_add(UC_HOOK_MEM_READ,read)
    machine.hook_add(UC_HOOK_MEM_WRITE,lambda uc,k,a,n,v,d:writes.append((a,n,v)))
    machine.emu_start(JOIN if injection is not None else ENTRY,PAGE+0x1000,count=64)
    final_flags = machine.reg_read(r.UC_ARM64_REG_NZCV)
    assert len(end) == 1 and not writes and final_flags == flags
    return end[0], tuple(machine.reg_read(register) for register in X), final_flags, reads, instructions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module",type=Path)
    parser.add_argument("candidate",type=Path)
    args = parser.parse_args()
    assert build_id(args.module) == BUILD
    image = nso.load(args.module).image
    samples = list(fixtures())
    payload = [" ".join(map(str,(STATE,SLOTS,BINDING,len(data),*(v for pair in data.items() for v in pair))))
               for _,_,data in samples]
    lines = subprocess.run([str(args.candidate.resolve())],input="\n".join(payload)+"\n",
                           capture_output=True,check=True,text=True).stdout.splitlines()
    coverage, indirect_lengths = Counter(), set()
    for case,line in zip(samples,lines,strict=True):
        reference = original(image,*case)
        assert reference == original(image,*case)
        pc, regs, nzcv, stock_reads, instructions = reference
        submit, lane, row, receiver, *data = map(int,line.split())
        reads = list(zip(data[::2],data[1::2],strict=True))
        actual_lane = case[2][RESOURCE + 0x2e5] & 3
        assert submit == (pc == JOIN and actual_lane < 2) and reads == stock_reads[:len(reads)]
        if submit:
            assert reads == stock_reads
            out = (receiver,1,row+0x250,row,0x260,lane)
            assert original(image,*case,injection=out)[:3] == (pc,regs,nzcv)
            coverage["typed plan"] += 1
            coverage["scoped"] += bool(case[0] == 0 and receiver)
            indirect_lengths.add(instructions)
        coverage[pc] += 1
    assert set(STOPS).issubset(coverage)
    print(f"{len(samples)} original pairs with ordered reads and no stores. "
          f"{coverage['typed plan']} exact indirect plans ({coverage['scoped']} in live scope), "
          f"{coverage[JOIN]-coverage['typed plan']} other indirect states, "
          f"{coverage[DIRECT]} direct fallback and {coverage[SKIP]} skips.")
    assert len(indirect_lengths) == 1
    print(f"Original indirect preparation: {indirect_lengths.pop()} instructions before the stock target load.")


if __name__ == "__main__":
    main()
