"""Replay both animation sidecar updates against the original AArch64 slices."""
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


OWNER, ROWS, COLLECTION, NAME = 0x24040, 0x25000, 0x27000, 0x28000
STRIDE = 0xe8
SCOPES = ((0x374f524, 0x374f5b8, (8, 9, 10, 11, 21, 26, 28)),
          (0x374f658, 0x374f6cc, (8, 9, 10)))


def scalar_case(count, index, equal, counter, bits, alias=False):
    data = {}
    base = OWNER + 0x80 - 0xc0 if alias else ROWS
    for i in (0, 1, 2):
        row = base + i * STRIDE
        put(data, row + 0xc4, "I", 7 if equal else 6)
        put(data, row + 0xc8, "I", counter)
    put(data, OWNER + 0x80, "I", count)
    put(data, OWNER + 0x88, "Q", base)
    put(data, OWNER + 0xb0, "I", 7)
    return 0, OWNER, index, 0, 0, 0, bits, data


def post_case(count, collection_count, index, name_byte, alias):
    data = {}
    previous = OWNER + 0x80 - 0x58 if alias else ROWS + STRIDE
    for i in (0, 1, 2):
        row = ROWS + i * STRIDE
        address = NAME + i * 4
        put(data, row + 0x68, "Q", address)
        put(data, row + 0xb8, "I", 2)
        put(data, row + 0xd8, "Q", 0x990000 | i)
        put(data, address, "B", name_byte if i % 2 == 0 else name_byte ^ 0x61)
    put(data, OWNER + 0x80, "I", count)
    put(data, OWNER + 0x88, "Q", ROWS)
    put(data, OWNER + 0x78, "Q", 0 if collection_count < 0 else COLLECTION)
    put(data, COLLECTION + 0x18, "I", max(0, collection_count))
    return 1, OWNER, index, ROWS, previous, 1, 0x80000000, data


def fixtures():
    for args in product((0, 1, 2, 3), (0, 1, 5), (False, True),
                        (0, 0x2932e0, 0x2932e1, 0xffffffff), (0, 0x80000000, 0x7fa12345, 0x40700000)):
        yield scalar_case(*args)
    yield scalar_case(1, 1, False, 9, 2, alias=True)
    for args in product((0, 1, 2), (-1, 0, 2), (0, 1, 5), (0, 0x61), (False, True)):
        yield post_case(*args)


def original(image, case):
    part, owner, index, cached_base, cached_row, cached_index, scalar, data = case
    start, end, result_regs = SCOPES[part]
    entry, stop = BASE + start, BASE + end
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    for page in {entry & -4096, *(address & -4096 for address in data)}:
        machine.mem_map(page, 4096)
    machine.mem_write(entry, bytes(image[start:end]))
    for address, value in data.items():
        machine.mem_write(address, bytes([value]))
    values = ((X[19], owner), (X[25], index), (X[27], STRIDE), (X[28], cached_base),
              (X[26], cached_row), (X[21], cached_index), (r.UC_ARM64_REG_S0, scalar),
              (r.UC_ARM64_REG_NZCV, 0x90000000))
    for reg, value in values:
        machine.reg_write(reg, value)
    accesses = []
    valid = set(data)

    def access(uc, kind, address, width, value, _):
        if kind == UC_MEM_WRITE:
            valid.update(range(address, address + width))
            accesses.append(("S", address, width, value & ((1 << (width * 8)) - 1)))
        else:
            assert all(byte in valid for byte in range(address, address + width))
            accesses.append(("L", address, width, int.from_bytes(uc.mem_read(address, width), "little")))

    machine.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access)
    machine.emu_start(entry, stop, count=64)
    assert machine.reg_read(r.UC_ARM64_REG_PC) == stop
    assert machine.reg_read(r.UC_ARM64_REG_S0) == scalar
    assert machine.reg_read(X[19]) == owner and machine.reg_read(X[25]) == index
    return (*[machine.reg_read(X[reg]) for reg in result_regs], machine.reg_read(r.UC_ARM64_REG_NZCV)), accesses


def candidate_inputs(samples):
    return "".join(" ".join(map(str, (*case[:-1], len(case[-1]),
                                      *(item for pair in case[-1].items() for item in pair)))) + "\n"
                   for case in samples)


def candidate_output(part, line):
    tokens = line.split()
    length = len(SCOPES[part][2]) + 1
    return tuple(map(int, tokens[:length])), [
        (kind, int(address), int(width), int(value))
        for kind, address, width, value in zip(*(tokens[i::4] for i in range(length, length + 4)), strict=True)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    assert build_id(args.module) == BUILD
    image = nso.load(args.module).image
    samples = list(fixtures())
    output = subprocess.run([str(args.candidate.resolve())], input=candidate_inputs(samples),
                            capture_output=True, text=True, check=True).stdout.splitlines()
    cases = Counter()
    for sample, line in zip(samples, output, strict=True):
        part = sample[0]
        stock = original(image, sample)
        assert stock == original(image, sample) == candidate_output(part, line)
        cases[part] += 1
    print(f"{cases[0]} scalar and {cases[1]} post-name cases matched two originals, ordered accesses and join state.")


if __name__ == "__main__":
    main()
