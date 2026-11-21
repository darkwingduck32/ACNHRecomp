"""Replay the format leaf and the original pass reader with its real AArch64 child."""
import argparse
from itertools import product
from pathlib import Path
import struct
import subprocess

from recomp import nso
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, FORMAT, READER = 0x4000000000, 0x29e460, 0x38d6688
FORMAT_TABLE = BASE + 0x4035398
OWNER, ROWS, DESCRIPTOR, SELECTION = 0x21000, 0x23000, 0x25000, 0x26000
TABLE, BUFFER, SP, RETURN = 0x28000, 0x29000, 0x31000, 0x34000
X = tuple(getattr(r, f"UC_ARM64_REG_X{i}") for i in range(31))


def put(data, address, form, *values):
    data.update(enumerate(struct.pack("<" + form, *values), address))


def format_table():
    data = {}
    put(data, FORMAT_TABLE, "3Q", 24, 48, 24)
    return data


def format_cases():
    for code in (*range(31), 0x80000000, 0xffffffff):
        yield 0, code, 0, 0, format_table()


def reader_cases():
    for pass_index, request, desc_index in product((-2, 0, 3), (-1, 0, 1, 3, 4, 0x7fffffff), (-2, -1, 0, 7)):
        for format_code in ((0,) if desc_index == -1 else (0, 1, 15, 16, 27, 28, 29, 30)):
            data = format_table()
            row = ROWS + pass_index * 0x80
            desc = TABLE + desc_index * 0x20
            for address, form, value in ((OWNER + 0x120, "Q", ROWS), (row, "Q", DESCRIPTOR),
                                         (DESCRIPTOR + 0x80, "Q", SELECTION),
                                         (SELECTION + 0xc, "h", desc_index),
                                         (DESCRIPTOR + 0x50, "Q", TABLE),
                                         (desc + 0x10, "B", format_code), (row + 0x48, "Q", BUFFER),
                                         (desc + 0x12, "H", 0x30)):
                put(data, address, form, value)
            for slot in range(-1, 16):
                put(data, BUFFER + 0x30 + 4 * slot, "I", (0x7fc00080 + slot) ^ (format_code * 0x1070))
            yield 1, OWNER, pass_index & 0xffffffff, request & 0xffffffff, data


def original(image, case):
    mode, value, pass_index, request, data = case
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    for offset in (FORMAT & ~0xfff, READER & ~0xfff):
        machine.mem_map(BASE + offset, 0x1000)
        machine.mem_write(BASE + offset, bytes(image[offset:offset + 0x1000]))
    for page in {SP - 0x1000, RETURN, *(address & ~0xfff for address in data)}:
        machine.mem_map(page, 0x1000)
    for address, byte in data.items():
        machine.mem_write(address, bytes([byte]))
    for index, register in enumerate(X):
        machine.reg_write(register, 0xf1f2f30000000000 + index)
    for register, arg in ((X[0], value), (X[1], pass_index), (X[2], request), (X[30], RETURN),
                          (r.UC_ARM64_REG_SP, SP), (r.UC_ARM64_REG_Q0, (0xfc << 96) | 0x3fa00000),
                          (r.UC_ARM64_REG_NZCV, 0x90000000)):
        machine.reg_write(register, arg)
    reads, writes, child = [], [], []

    def code(uc, pc, size, user):
        if pc == BASE + FORMAT and mode:
            child.append(uc.reg_read(X[0]))

    def read(uc, access, address, size, value, user):
        if SP - 0x30 <= address and address + size <= SP:
            return
        assert all(byte in data for byte in range(address, address + size)), hex(address)
        reads.append((address, size))

    def write(uc, access, address, size, value, user):
        writes.append((address, size, value))

    machine.hook_add(UC_HOOK_CODE, code)
    machine.hook_add(UC_HOOK_MEM_READ, read)
    machine.hook_add(UC_HOOK_MEM_WRITE, write)
    machine.emu_start(BASE + (READER if mode else FORMAT), RETURN, count=256)
    assert machine.reg_read(r.UC_ARM64_REG_PC) == RETURN and machine.reg_read(r.UC_ARM64_REG_SP) == SP
    assert all(SP - 0x30 <= address and address + size <= SP for address, size, _ in writes)
    regs = tuple(machine.reg_read(reg) for reg in X)
    scalar = machine.reg_read(r.UC_ARM64_REG_Q0)
    return regs, scalar, machine.reg_read(r.UC_ARM64_REG_NZCV), tuple(reads), tuple(child)


def compare(image, candidate, samples):
    payload = []
    for mode, value, pass_index, request, data in samples:
        header = (mode, BASE, value, pass_index, request, len(data))
        payload.append(" ".join(map(str, (*header, *(v for item in data.items() for v in item)))))
    lines = subprocess.run([str(candidate.resolve())], input="\n".join(payload) + "\n",
                           check=True, capture_output=True, text=True).stdout.splitlines()
    counts = [0, 0, 0]
    for case, line in zip(samples, lines, strict=True):
        stock = original(image, case)
        assert stock == original(image, case)
        regs, scalar, flags, reads, child = stock
        numbers = tuple(map(int, line.split()))
        if case[0] == 0:
            assert numbers[0] == regs[0]
            counts[0] += 1
            read_fields = numbers[1:]
        else:
            kind, raw, fmt, width, index, values, buffer, descriptor = numbers[:8]
            assert raw == scalar  # Original loads and FMOV clear the upper vector bits.
            if child:
                assert (kind, fmt, width, index, values, buffer) == (2, child[0], regs[0], *regs[8:11])
                counts[2] += 1
            else:
                assert (kind, width, descriptor, regs[9], flags) == (1, 0, regs[8], 2**64-1, 0x60000000)
                counts[1] += 1
            read_fields = numbers[8:]
        assert read_fields == tuple(v for pair in reads for v in pair)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    counts = compare(nso.load(args.module).image, args.candidate, [*format_cases(), *reader_cases()])
    print(f"{counts[0]} format cases and {sum(counts[1:])} reader cases matched two originals "
          f"({counts[1]} sentinel, {counts[2]} sampled), ordered reads and no external stores.")


if __name__ == "__main__":
    main()
