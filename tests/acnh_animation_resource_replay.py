"""Differential replay for the packed animation lookup and nonnull link writes."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BASE, BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


OWNER, RESOURCE_OWNER, TABLE, PACK, BUCKETS, POINTER, RESOURCE, ROWS, OTHER = range(0x21000, 0x2a000, 0x1000)
SCOPES = ((0x374f7d8, 0x374f84c, (8, 9, 10, 11)), (0x374f84c, 0x374f880, (8, 9, 10)))
MASK = 2**64 - 1


def lookup_case(index, packed, count, bucket_count, value):
    data = {}
    slot = index if count > index else 0
    bucket = packed >> 16
    if bucket & 0x8000:
        bucket |= 0xffff0000
    bucket = bucket if bucket_count > bucket else 0
    for address, form, word in (
            (OWNER + 0xa0, "Q", RESOURCE_OWNER), (RESOURCE_OWNER + 0x50, "Q", TABLE),
            (TABLE + 0x38, "I", count), (TABLE + 0x40, "Q", PACK),
            (PACK + slot * 0xb8, "I", packed), (TABLE + 0x80, "I", bucket_count),
            (TABLE + 0x88, "Q", BUCKETS), (BUCKETS + bucket * 0x28, "Q", POINTER),
            (POINTER, "Q", RESOURCE if value is not None else 0),
            (RESOURCE + 8, "Q", 0 if value is None else value)):
        put(data, address, form, word)
    return 0, OWNER, index, 0, data


def links_case(count, index, token, alias=None):
    owner = OWNER if alias is None else ROWS + (0x58 if alias == "count" else 0x50)
    data = {}
    put(data, owner + 0x80, "I", count)
    put(data, owner + 0x88, "Q", ROWS)
    return 1, owner, index, token, data


def cases():
    for values in product((0, 1, 0x7fff), (0x10001, 0x20001, 0x80000001, 0xfffeffff, 0xffff0001),
                          (0, 2), (0, 3), (None, 0, 0x81000, MASK - 1)):
        yield lookup_case(*values)
    for count, index, token in product((0, 1, 2, 0xffffffff), (0, 1, 2), (2, OTHER, MASK)):
        yield links_case(count, index, token)
    yield links_case(1, 1, 2, "count")
    yield links_case(1, 1, OTHER, "base")


def original(image, case):
    part, owner, index, token, data = case
    offset, limit, output = SCOPES[part]
    entry, stop = BASE + offset, BASE + limit
    vm = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    destinations = () if part == 0 else (ROWS + 0xd8, ROWS + index * 0xe8 + 0xe0, OTHER + 0xe0)
    for page in {entry & -4096, *(address & -4096 for address in (*data, *destinations))}:
        vm.mem_map(page, 4096)
    vm.mem_write(entry, bytes(image[offset:limit]))
    for address, value in data.items():
        vm.mem_write(address, bytes([value]))
    for register, value in ((X[8], index if part == 0 else token), (X[19], owner), (X[23], index),
                            (X[24], 0xe8), (X[25], 0xb8), (X[26], 0xfffeffff), (X[27], 0xffff),
                            (X[28], 0x28), (r.UC_ARM64_REG_NZCV, 0x90000000)):
        vm.reg_write(register, value)
    accesses, known = [], set(data)

    def access(uc, kind, address, width, value, _):
        if kind == UC_MEM_WRITE:
            known.update(range(address, address + width))
            accesses.append(("S", address, width, value & ((1 << (8 * width)) - 1)))
        else:
            assert all(address + i in known for i in range(width))
            accesses.append(("L", address, width, int.from_bytes(uc.mem_read(address, width), "little")))

    vm.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access)
    vm.emu_start(entry, stop, count=60)
    assert vm.reg_read(r.UC_ARM64_REG_PC) == stop
    assert sum(event[0] == "S" for event in accesses) == 2 * part
    registers = (*[vm.reg_read(X[reg]) for reg in output], vm.reg_read(r.UC_ARM64_REG_NZCV))
    return registers, accesses


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    assert build_id(args.module) == BUILD
    image = nso.load(args.module).image
    samples = list(cases())
    rows = [" ".join(map(str, (*case[:-1], len(case[-1]),
                               *(item for pair in case[-1].items() for item in pair)))) for case in samples]
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(rows) + "\n",
                            capture_output=True, check=True, text=True).stdout.splitlines()
    count = Counter()
    for case, line in zip(samples, output, strict=True):
        part = case[0]
        oracle = original(image, case)
        assert oracle == original(image, case)
        tokens = line.split()
        size = len(SCOPES[part][2]) + 1
        registers = tuple(map(int, tokens[:size]))
        accesses = [(kind, int(address), int(width), int(value)) for kind, address, width, value
                    in zip(*(tokens[i::4] for i in range(size, size + 4)), strict=True)]
        assert oracle == (registers, accesses), (part, case[2:4], oracle, registers, accesses)
        count[part] += 1
    print(f"{count[0]} packed lookup and {count[1]} ordered link-write cases matched two originals.")


if __name__ == "__main__":
    main()
