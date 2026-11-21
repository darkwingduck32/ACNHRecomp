"""Replay the original inline worker plane loop and its proposed stock continuation."""
import argparse
from pathlib import Path
import random
import struct
import subprocess

from recomp import nso
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn.arm64_const import (
    UC_ARM64_REG_NZCV, UC_ARM64_REG_S0, UC_ARM64_REG_S8, UC_ARM64_REG_S9,
    UC_ARM64_REG_S10, UC_ARM64_REG_S11, UC_ARM64_REG_X8, UC_ARM64_REG_X11,
)


BUILD = "FF1D1C05670DB6021C85B624A710B963"
FIRST, HOOK, REJECT, PASS = 0x146aa2c, 0x146aa50, 0x146aa54, 0x146ab10
CODE, PLANES = 0x146a000, 0x21000


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def cases():
    for n in (1, 2, 10, 24):
        for first_reject in range(n+1):
            rows = [(0., 0., 0., -1.)] * n
            if first_reject < n:
                rows[first_reject] = (0., 0., 0., -2.)
            yield tuple(map(bits, (3., 8., 9., 1.))), [tuple(map(bits, row)) for row in rows]
    rng = random.Random(0x146aa50)
    for _ in range(192):
        point = tuple(bits(rng.uniform(-64, 64)) for _ in range(3)) + (bits(rng.uniform(-24, 24)),)
        rows = [tuple(bits(rng.uniform(-1, 1)) for _ in range(4)) for _ in range(10)]
        yield point, rows


def original(image, point, rows, replacement=None):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(CODE, 0x1000)
    machine.mem_write(CODE, bytes(image[CODE:CODE+0x1000]))
    machine.mem_map(PLANES, 0x1000)
    table = b"".join(struct.pack("<4I", *row) for row in rows)
    machine.mem_write(PLANES, table)
    machine.reg_write(UC_ARM64_REG_X8, len(rows))
    machine.reg_write(UC_ARM64_REG_X11, PLANES - 0x43c)
    for reg, raw in zip((UC_ARM64_REG_S9, UC_ARM64_REG_S10, UC_ARM64_REG_S11, UC_ARM64_REG_S8), point):
        machine.reg_write(reg, raw)
    endpoint, first, reads, writes = [], [], [], []

    def code(uc, address, size, user):
        if address == HOOK:
            first.append(uc.reg_read(UC_ARM64_REG_S0))
            if replacement:
                passed, scalar, _ = replacement
                uc.reg_write(UC_ARM64_REG_S0, scalar)
                if passed:
                    uc.reg_write(UC_ARM64_REG_X8, 1)
                else:
                    uc.reg_write(UC_ARM64_REG_NZCV, 0x80000000)
        if address in (REJECT, PASS):
            endpoint.append((int(address == PASS), uc.reg_read(UC_ARM64_REG_S0), uc.reg_read(UC_ARM64_REG_NZCV)))
            uc.emu_stop()

    def read(uc, access, address, size, value, user):
        assert PLANES <= address and address+size <= PLANES+len(table)
        row = (address-PLANES)//16
        if not reads or reads[-1] != row:
            reads.append(row)

    def write(uc, access, address, size, value, user):
        writes.append((address, size, value))

    machine.hook_add(UC_HOOK_CODE, code)
    machine.hook_add(UC_HOOK_MEM_READ, read)
    machine.hook_add(UC_HOOK_MEM_WRITE, write)
    machine.emu_start(FIRST, CODE+0x1000, count=1024)
    assert len(endpoint) == len(first) == 1
    assert not writes
    assert machine.mem_read(PLANES, len(table)) == table
    assert reads == list(range(len(reads)))
    return first[0], endpoint[0], len(reads)-1


def compare(image, candidate, samples):
    references = []
    for point, rows in samples:
        result = original(image, point, rows)
        assert result == original(image, point, rows)
        references.append(result)
    payload = "\n".join(" ".join(map(str, (len(rows), *point, first, *(v for row in rows for v in row))))
                        for (point, rows), (first, _, _) in zip(samples, references, strict=True)) + "\n"
    output = subprocess.run([str(candidate.resolve())], input=payload, check=True,
                            text=True, capture_output=True).stdout.splitlines()
    assert len(output) == len(samples)
    counts = set()
    for index, ((point, rows), (first, (passes, scalar, nzcv), tested), line) in enumerate(
        zip(samples, references, output, strict=True),
    ):
        result = tuple(map(int, line.split()))
        if result != (passes, scalar, tested):
            raise ValueError(f"Case {index}: original {(passes, scalar, tested)}, C++ {result}")
        replaced_first, replaced, replaced_tail = original(image, point, rows, result)
        assert replaced_first == first and replaced == (passes, scalar, nzcv)
        assert replaced_tail == 0
        counts.add((passes, tested))
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    samples = list(cases())
    counts = compare(nso.load(args.module).image, args.candidate, samples)
    assert any(passes for passes, _ in counts)
    assert any(not passes and tested > 0 for passes, tested in counts)
    print(f"{len(samples)} plane cases matched two originals and stock continuation, "
          f"exact S0/NZCV, {len(counts)} outcome/count combinations, no writes")


if __name__ == "__main__":
    main()
