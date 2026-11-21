"""Check scoped terrain draw planning against the original AArch64 caller."""
import argparse
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BASE, BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as reg


ENTRY, DRAW, RETURN = 0x1461a34, 0x146b080, 0x38ea1e8
SP, MANAGER, DESCRIPTOR, TABLE, BINDING = 0x30000, 0x21000, 0x30000, 0x81000, 0x91000
RESUMES = tuple(BASE + v for v in (0x1461b08, 0x1461b20, 0x1461b38, 0x1461b84))
CELL = SP - 0x50 + 0x18


def cases():
    for pass_id, count, index, special in product(
        (0, 2, 9, 12, 13, 14, 42), (0, 1, 3, 0xffffffff), (0, 1, 3, 0xfffffffe, 0xffffffff), (1, 2)
    ):
        data = {}
        for addr, size, value in (
            (DESCRIPTOR, "Q", 0x23000), (DESCRIPTOR+8, "Q", 0x24000), (0x24008, "Q", 0x25000),
            (0x25070, "Q", 0x26000), (0x23021, "B", pass_id), (0x26004, "I", index),
            (MANAGER+0x500, "I", count), (MANAGER+0x508, "Q", TABLE),
            (BASE+0x4ab2200, "Q", 0x27000), (0x27000, "Q", 0x28000), (0x28150, "I", special),
            (BASE+0x4ab31c8, "Q", 0x29000), (0x29000, "Q", 0x2a000), (0x2a058, "Q", BINDING)
        ):
            put(data, addr, size, value)
        yield pass_id, count, index, special, data


def original(image, data):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    for address in {BASE + (ENTRY & ~0xfff), BASE + (DRAW & ~0xfff), BASE + (RETURN & ~0xfff),
                    SP - 0x1000, *(a & ~0xfff for a in data)}:
        machine.mem_map(address, 0x1000)
    machine.mem_write(BASE+ENTRY, bytes(image[ENTRY:ENTRY+0x168]))
    for address, byte in data.items():
        machine.mem_write(address, bytes([byte]))
    initial_saved = tuple(0xfefe000000000000 + i for i in range(19,26))
    for register, value in zip(X[19:26], initial_saved, strict=True):
        machine.reg_write(register,value)
    for register,value in ((X[0],MANAGER),(X[1],DESCRIPTOR),(X[29],SP+0x20),(X[30],BASE+RETURN),
                           (reg.UC_ARM64_REG_SP,SP),(reg.UC_ARM64_REG_NZCV,0xa0000000)):
        machine.reg_write(register,value)
    reads, calls, writes = [], [], []
    wrapper_instructions = 0

    def code(uc, pc, size, _):
        nonlocal wrapper_instructions
        if pc != BASE+DRAW:
            assert BASE+ENTRY <= pc < BASE+ENTRY+0x168
            wrapper_instructions += 1
            return
        resumes = uc.reg_read(X[30])
        args = tuple(uc.reg_read(i) for i in X[:5])
        assert args[:2] == (MANAGER,DESCRIPTOR)
        assert args[4] == CELL
        assert uc.reg_read(X[29]) == uc.reg_read(reg.UC_ARM64_REG_SP) == SP - 0x50
        current_binding = int.from_bytes(uc.mem_read(CELL,8), "little")
        assert current_binding == BINDING + 29 * len(calls)
        calls.append((RESUMES.index(resumes), args[2], args[3], current_binding))
        uc.mem_write(CELL,(current_binding+29).to_bytes(8,"little"))
        uc.reg_write(X[0],0x10000000+len(calls))
        uc.reg_write(reg.UC_ARM64_REG_NZCV,0x80000000)
        uc.reg_write(reg.UC_ARM64_REG_PC,resumes)

    def read(uc, access, address, size, value, _):
        if SP-0x50 <= address < SP:
            return
        assert all(a in data for a in range(address,address+size))
        reads.append((address,size))

    def write(uc, access, address, size, value, _):
        assert SP-0x50 <= address and address+size <= SP
        writes.append((address,size,value & 0xffffffffffffffff))

    machine.hook_add(UC_HOOK_CODE,code)
    machine.hook_add(UC_HOOK_MEM_READ,read)
    machine.hook_add(UC_HOOK_MEM_WRITE,write)
    machine.emu_start(BASE+ENTRY,BASE+RETURN,count=180)
    assert machine.reg_read(reg.UC_ARM64_REG_PC) == BASE+RETURN
    assert machine.reg_read(reg.UC_ARM64_REG_SP) == SP and machine.reg_read(X[29]) == SP+0x20
    assert tuple(machine.reg_read(i) for i in X[19:26]) == initial_saved
    assert writes[-1] == (CELL,8,BINDING)
    return reads, calls, writes, machine.reg_read(X[0]), wrapper_instructions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    assert build_id(args.module) == BUILD
    image = nso.load(args.module).image
    inputs = list(cases())
    payload = []
    for *_, memory in inputs:
        payload.append(" ".join(map(str, (MANAGER, DESCRIPTOR, BASE, len(memory),
                                           *(v for pair in memory.items() for v in pair)))))
    result = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload)+"\n",
                            capture_output=True, check=True, text=True).stdout.splitlines()
    supported = 0
    admitted_instruction_counts = set()
    for case, line in zip(inputs,result,strict=True):
        pass_id, count, index, _, data = case
        stock = original(image,data)
        assert stock == original(image,data)
        reads, calls, _, _, wrapper_instructions = stock
        use, selected_pass, context, binding, i, selected, size, *events = map(int,line.split())
        scope = pass_id in (0, 2, 9, 12)
        assert bool(use) == scope and (selected_pass,i,selected) == (
            pass_id,index,index if index < count else 0
        )
        read_field = tuple(v for pair in reads for v in pair)
        expected_draws = tuple(v for lane,address,_,_ in calls for v in (lane,address))
        assert tuple(events[:2*size]) == (read_field if scope else read_field[:2*size])
        if scope:
            assert all(ctx==context for _,_,ctx,_ in calls)
            assert binding == calls[0][-1] == BINDING
            assert tuple(events[2*size:]) == expected_draws
            supported += 1
            admitted_instruction_counts.add(wrapper_instructions)
        else:
            assert not events[2*size:]
    print(f"{len(inputs)} deterministic original pairs. {supported} C++ plans matched ordered reads "
          f"and draws. {len(inputs)-supported} out-of-scope plans declined.")
    assert len(admitted_instruction_counts) == 1
    print(f"Original scoped wrapper: {admitted_instruction_counts.pop()} instructions, excluding the draw bodies.")


if __name__ == "__main__":
    main()
