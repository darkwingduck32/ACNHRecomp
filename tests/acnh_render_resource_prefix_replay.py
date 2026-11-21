"""Replay the original render prefix to the empty, changed, or matching continuation."""
import argparse
from pathlib import Path
import struct
import subprocess

from recomp import nso
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as arm64


BUILD = "FF1D1C05670DB6021C85B624A710B963"
CODE, CUT = 0x38ea000, 0x38ea930
STOPS = (0x38eb85c, 0x38ea99c, 0x38eac1c)
STATE, SLOTS, RECORD, SP = 0x20000, 0x30000, 0x40000, 0x50000
X = tuple(getattr(arm64, f"UC_ARM64_REG_X{i}") for i in range(31))
NZCV = arm64.UC_ARM64_REG_NZCV


def fixtures():
    for category in (0, 1, 7, 8, 9, 12, 31, 32, 127, 128, 254, 255):
        for request in (-128, -2, -1, 0, 1, 2, 3, 127):
            for route in range(3):
                if 4 * category + request == -2:
                    continue  # This slot would alias the binding field in the fixture.
                data = {}

                def put(address, form, value):
                    data.update(enumerate(struct.pack("<" + form, value), address))

                word = (0xb96c39e4 ^ (category * 0x1415926)) & 0xffffffff
                put(STATE + 0x38, "b", request)
                put(STATE + 0x21, "B", category)
                lane = request
                if request == -1:
                    put(SLOTS + 0x230 + ((category >> 1) & 0x7c), "I", word)
                    lane = (word >> ((category & 7) * 2)) & 3
                put(SLOTS + 0x30 + 8 * (4 * category + lane), "Q", RECORD)
                put(RECORD, "Q", 0x12aabbccdd if route else 0)
                if route:
                    put(RECORD + 8, "Q", 0x21030aabbccdd)
                    put(STATE + 0x1c8, "Q", 0x21030aabbccdd if route == 2 else 0x31030aabbccdd)
                    put(SLOTS + 0x20, "Q", 0x13050)
                if route == 2:
                    put(STATE + 0x1c0, "Q", 0x13090)
                yield STATE, SLOTS, data


def original(image, state, slots, data, replacement=None):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(CODE, 0x2000)
    machine.mem_write(CODE, bytes(image[CODE:CODE + 0x2000]))
    for page in {*[address & ~0xfff for address in data], SP}:
        machine.mem_map(page, 0x1000)
    for address, byte in data.items():
        machine.mem_write(address, bytes([byte]))
    for i, reg in enumerate(X):
        machine.reg_write(reg, 0x1222444410000000 + i)
    for register, value in ((X[0], state), (X[1], slots), (X[29], SP + 0x30),
                            (arm64.UC_ARM64_REG_SP, SP), (NZCV, 0xa0000000)):
        machine.reg_write(register, value)
    reads, writes, stops = [], [], []

    def code(uc, pc, size, user):
        if pc in STOPS:
            stops.append(pc)
            uc.emu_stop()

    def read(uc, access, address, size, value, user):
        if any(byte not in data for byte in range(address, address + size)):
            raise ValueError(f"Uncaptured read {address:#x}+{size} at {uc.reg_read(arm64.UC_ARM64_REG_PC):#x}")
        reads.append((address, size))

    def write(uc, access, address, size, value, user):
        writes.append((address, size, value))

    machine.hook_add(UC_HOOK_CODE, code)
    machine.hook_add(UC_HOOK_MEM_READ, read)
    machine.hook_add(UC_HOOK_MEM_WRITE, write)
    pc = CUT
    if replacement:
        _, lane, record, resource, key, binding, previous, packed, shifted = replacement
        for i, value in ((8, previous), (19, state), (20, slots), (21, lane), (22, binding),
                         (25, resource), (26, key), (28, record)):
            machine.reg_write(X[i], value)
        if data[state + 0x38] == 255:
            machine.reg_write(X[9], shifted)
            machine.reg_write(X[10], packed)
        machine.mem_write(SP + 0x28, struct.pack("<Q", resource))
        machine.reg_write(NZCV, 0x60000000)
        pc = STOPS[2]
    machine.emu_start(pc, CODE + 0x2000, count=128)
    assert len(stops) == 1
    registers = tuple(machine.reg_read(reg) for reg in X)
    return (STOPS.index(stops[0]), registers, machine.reg_read(NZCV), tuple(reads),
            tuple(writes), bytes(machine.mem_read(SP, 0x90)))


def compare(image, candidate, samples):
    payload = []
    for state, slots, data in samples:
        payload.append(" ".join(map(str, (state, slots, len(data), *(n for pair in data.items() for n in pair)))))
    output = subprocess.run([str(candidate.resolve())], input="\n".join(payload) + "\n",
                            check=True, text=True, capture_output=True).stdout.splitlines()
    counts = [0] * len(STOPS)
    for (state, slots, data), row in zip(samples, output, strict=True):
        stock = original(image, state, slots, data)
        assert stock == original(image, state, slots, data)
        route, regs, flags, reads, writes, stack = stock
        expected = (route, regs[21], regs[28], regs[25], regs[26] if route else 0,
                    regs[22] if route else 0, regs[8] if route == 2 else 0,
                    regs[10] if data[state + 0x38] == 255 else 0,
                    regs[9] if data[state + 0x38] == 255 else 0)
        actual = tuple(map(int, row.split()))
        assert actual[:9] == expected, (expected, actual[:9])
        assert actual[9:] == tuple(n for read in reads for n in read), (reads, actual[9:])
        assert writes == (((SP + 0x28, 8, regs[25]),) if route else ())
        if route == 2:
            replaced = original(image, state, slots, data, actual[:9])
            assert replaced[:3] == (route, regs, flags) and replaced[5] == stack
        counts[route] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    counts = compare(nso.load(args.module).image, args.candidate, list(fixtures()))
    print(f"{sum(counts)} render prefix cases matched two originals "
          f"({counts[0]} empty, {counts[1]} changed, {counts[2]} matching). "
          "Ordered reads agree; matching cases preserve all GPRs, NZCV and the original stack spill.")


if __name__ == "__main__":
    main()
