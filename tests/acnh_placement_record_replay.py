"""Compare placement record initialization, including transient writes, on original AArch64."""
import argparse
from itertools import product
from pathlib import Path
import random
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BASE, BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


ENTRY, STOP = BASE + 0x1298524, 0x80000
DESTINATION, TYPE_TABLE = 0x35000, BASE + 0x4893e98
REGISTERS = (X[0], X[8], X[9], r.UC_ARM64_REG_S0, r.UC_ARM64_REG_NZCV)


def fixtures():
    rng = random.Random(0x1298524)
    for offset, selector, scalar, flag in product(
            (0x28, 0, 0x12), (0, 7, 8, 255),
            (0, 0x80000000, 0x40600000, 0xc0600000, 0x7f800000, 0xff800000,
             0x7fa12345, 0x4f000000, 0xcf000000), (0, 0x80)):
        source = DESTINATION + offset
        data = {address: rng.randrange(256) for address in range(DESTINATION, source + 0x50)}
        put(data, source + 0x31, "B", selector)
        put(data, source + 0x42, "B", flag)
        put(data, source + 0x48, "I", scalar)
        yield DESTINATION, source, data


def original(image, record, source, data):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    for page in {ENTRY & -4096, STOP, *(address & -4096 for address in data)}:
        machine.mem_map(page, 4096)
    machine.mem_write(ENTRY, bytes(image[ENTRY - BASE:ENTRY - BASE + 0x104]))
    for address, value in data.items():
        machine.mem_write(address, bytes([value]))
    for register, value in ((X[0], record), (X[1], source), (X[30], STOP),
                            (r.UC_ARM64_REG_NZCV, 0x90000000), (r.UC_ARM64_REG_S0, 0x3f800000)):
        machine.reg_write(register, value)
    operations = []

    def access(uc, kind, address, width, value, _):
        if kind == UC_MEM_WRITE:
            operations.append(("S", address, width, value & ((1 << (width * 8)) - 1)))
        else:
            operations.append(("L", address, width, int.from_bytes(uc.mem_read(address, width), "little")))

    machine.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access)
    machine.emu_start(ENTRY, STOP, count=80)
    assert machine.reg_read(r.UC_ARM64_REG_PC) == STOP
    assert sum(operation[0] == "S" for operation in operations) == 26
    return tuple(machine.reg_read(reg) for reg in REGISTERS), operations


def candidate_inputs(samples):
    return "".join(" ".join(map(str, (record, source, TYPE_TABLE, len(data),
                                     *(item for pair in data.items() for item in pair)))) + "\n"
                   for record, source, data in samples)


def candidate_output(line):
    tokens = line.split()
    return tuple(map(int, tokens[:len(REGISTERS)])), [
        (kind, int(address), int(width), int(value))
        for kind, address, width, value in zip(*(tokens[i::4] for i in range(len(REGISTERS),
                                                                        len(REGISTERS) + 4)), strict=True)]


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
    for sample, line in zip(samples, output, strict=True):
        stock = original(image, *sample)
        assert stock == original(image, *sample) == candidate_output(line)
    print(f"{len(samples)} placement initializer cases matched two originals, ordered accesses and ABI outputs.")


if __name__ == "__main__":
    main()
