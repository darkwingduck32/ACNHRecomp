"""Compare the empty render binding cut against original AArch64, before refresh effects."""
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
CODE, ENTRY, JOIN = 0x38ea000, 0x38ead64, 0x38eadf4
REFRESH = (0x38eada8, 0x38eadec)
STATE, CONTEXT = 0x21000, 0x23000
ABSENT = 0xffffffff
X = tuple(getattr(r, f"UC_ARM64_REG_X{i}") for i in range(31))


def fixtures():
    for enable, id0, id1, new0, new1, old0, old1 in product(
        (0, 1, 3), (ABSENT, 0x7fffffff), (ABSENT, 0), (0, 0x42000),
        (0, 0x43000), (0, 0x42000), (0, 0x43000),
    ):
        data = {}
        for offset, fmt, values in (
            (STATE + 0x18, "Q", (0x1f11f11f,)),
            (STATE + 0x188, "2I2Q", (id0, id1, old0, old1)),
            (CONTEXT + 0x238, "2Q", (new0, new1)),
            (CONTEXT + 0x261, "B", (enable,)),
        ):
            data.update(enumerate(struct.pack("<" + fmt, *values), offset))
        yield STATE, CONTEXT, data


def original(image, state, context, data, resume=False):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(CODE, 0x1000)
    machine.mem_write(CODE, bytes(image[CODE:CODE + 0x1000]))
    for page in {address & ~0xfff for address in data}:
        machine.mem_map(page, 0x1000)
    for address, value in data.items():
        machine.mem_write(address, bytes([value]))
    for i, reg in enumerate(X):
        machine.reg_write(reg, 0xf0f1234000000000 + i)
    machine.reg_write(X[19], state)
    machine.reg_write(X[20], context)
    machine.reg_write(r.UC_ARM64_REG_NZCV, 0x90000000)
    endpoints, reads, writes = [], [], []

    def code(uc, pc, size, user):
        if pc in (JOIN, *REFRESH):
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
    if resume:
        for index, value in ((8, ABSENT), (23, state + 0x190), (24, context + 0x238), (25, 0)):
            machine.reg_write(X[index], value)
        machine.reg_write(r.UC_ARM64_REG_NZCV, 0x60000000)
        pc = JOIN
    machine.emu_start(pc, CODE + 0x1000, count=128)
    assert len(endpoints) == 1 and not writes
    return endpoints[0], tuple(machine.reg_read(reg) for reg in X), machine.reg_read(r.UC_ARM64_REG_NZCV), tuple(reads)


def compare(image, candidate, samples):
    payload = [" ".join(map(str, (state, context, len(data), *(v for pair in data.items() for v in pair))))
               for state, context, data in samples]
    lines = subprocess.run([str(candidate.resolve())], input="\n".join(payload) + "\n",
                           check=True, capture_output=True, text=True).stdout.splitlines()
    counts = Counter()
    for (state, context, data), line in zip(samples, lines, strict=True):
        stock = original(image, state, context, data)
        assert stock == original(image, state, context, data)
        resume, *pair_data = map(int, line.split())
        reads = tuple(zip(pair_data[::2], pair_data[1::2], strict=True))
        empty = ((context + 0x261, 1), (state + 0x188, 4), (state + 0x18c, 4))
        expected = stock[0] == JOIN and stock[3] == empty and stock[1][25] == 0
        assert resume == expected
        assert reads == stock[3][:len(reads)]
        if resume:
            assert reads == empty
            assert original(image, state, context, data, resume=True)[:3] == stock[:3]
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
    assert set(counts) == {(JOIN, 1), (JOIN, 0), (REFRESH[0], 0), (REFRESH[1], 0)}
    print(f"{sum(counts.values())} render binding cases matched two originals, "
          f"{counts[JOIN, 1]} exact empty continuations, with ordered reads and zero writes.")


if __name__ == "__main__":
    main()
