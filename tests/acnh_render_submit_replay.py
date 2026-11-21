"""Replay the render submit argument preparation up to the generated target load."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import struct
import subprocess

from recomp import nso
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


BUILD = "FF1D1C05670DB6021C85B624A710B963"
CODE, ENTRY, JOIN = 0x38fa000, 0x38fae10, 0x38fb078
STOPS = (JOIN, 0x38fae18, 0x38faeb8, 0x38fb03c)
BINDING, STATE, SINK, RECORDS = 0x21000, 0x23000, 0x25000, 0x200000000000
X = tuple(getattr(r, f"UC_ARM64_REG_X{i}") for i in range(31))


def fixtures():
    for signed_pass, (requested, available), flags, state_flags, receiver in product(
        (-0x80000000, -1, 0, 1, 3, 0x7fffffff), ((1, 2), (1, 1), (8, 8), (2, 2)),
        (0x20, 0x21), (0x12, 0x32), (0, 0x42424242),
    ):
        record = RECORDS + signed_pass * 0x260
        data = {}
        for address, width, value in ((BINDING + 0xe10, 8, RECORDS), (record + 0x258, 1, flags),
                                      (STATE + 0x36, 2, state_flags), (SINK + 0xb8, 8, receiver)):
            data.update(enumerate(value.to_bytes(width, "little"), address))
        yield BINDING, STATE, SINK, signed_pass & 0xffffffff, requested, available, data


def original(image, binding, state, sink, pass_bits, requested, available, data, replacement=None, *,
             incoming=None):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(CODE, 0x2000)
    machine.mem_write(CODE, bytes(image[CODE:CODE + 0x2000]))
    for page in {address & ~0xfff for address in data}:
        machine.mem_map(page, 0x1000)
    for address, value in data.items():
        machine.mem_write(address, bytes([value]))
    for i, reg in enumerate(X):
        machine.reg_write(reg, 0xfe00000000000000 + i)
    for index, value in ((13, binding), (19, state), (20, pass_bits), (21, sink),
                         (24, requested), (9, available)):
        machine.reg_write(X[index], value)
    initial_flags = 0x90000000
    if incoming is not None:
        x0, x1, x2, x10, initial_flags = incoming
        for index, value in ((0, x0), (1, x1), (2, x2), (10, x10)):
            machine.reg_write(X[index], value)
    machine.reg_write(r.UC_ARM64_REG_NZCV, initial_flags)
    endpoints, reads, writes = [], [], []

    def code(uc, pc, size, user):
        if pc in STOPS:
            endpoints.append(pc)
            uc.emu_stop()
        elif not ENTRY <= pc < JOIN:
            raise ValueError(f"Unexpected PC {pc:#x}")

    def read(uc, access, address, size, value, user):
        assert all(byte in data for byte in range(address, address + size))
        reads.append((address, size))

    def write(uc, access, address, size, value, user):
        writes.append((address, size, value))

    machine.hook_add(UC_HOOK_CODE, code)
    machine.hook_add(UC_HOOK_MEM_READ, read)
    machine.hook_add(UC_HOOK_MEM_WRITE, write)
    pc = ENTRY
    if replacement:
        records, record, receiver = replacement
        for index, value in ((0, receiver), (1, 1), (2, record + 0x250),
                             (8, record), (9, records), (10, 0x260)):
            machine.reg_write(X[index], value)
        pc = JOIN
    machine.emu_start(pc, CODE + 0x2000, count=128)
    assert len(endpoints) == 1 and not writes
    return endpoints[0], tuple(machine.reg_read(reg) for reg in X), machine.reg_read(r.UC_ARM64_REG_NZCV), tuple(reads)


def compare(image, candidate, cases):
    payload = []
    for binding, state, sink, pass_bits, requested, available, data in cases:
        header = (binding, state, sink, pass_bits, requested & available, len(data))
        payload.append(" ".join(map(str, (*header, *(v for part in data.items() for v in part)))))
    lines = subprocess.run([str(candidate.resolve())], input="\n".join(payload) + "\n",
                           check=True, capture_output=True, text=True).stdout.splitlines()
    counts = Counter()
    for case, line in zip(cases, lines, strict=True):
        stock = original(image, *case)
        assert stock == original(image, *case)
        resume, records, record, receiver, *pair_data = map(int, line.split())
        reads = tuple(zip(pair_data[::2], pair_data[1::2], strict=True))
        expected = stock[0] == JOIN and not (case[4] & case[5])
        assert resume == expected
        assert reads == stock[3][:len(reads)]
        if resume:
            assert reads == stock[3]
            assert original(image, *case, replacement=(records, record, receiver))[:3] == stock[:3]
        counts[stock[0], resume] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    counts = compare(nso.load(args.module).image, args.candidate, list(fixtures()))
    assert {pc for pc, resume in counts} == set(STOPS)
    print(f"{sum(counts.values())} render submit cases matched two originals, "
          f"{counts[JOIN, 1]} exact indirect preparations, with ordered reads and zero writes.")


if __name__ == "__main__":
    main()
