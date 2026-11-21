"""Compare the typed descriptor transfer with the original AArch64 slice."""
import argparse
from collections import Counter
from pathlib import Path
import random
import struct
import subprocess

from recomp import nso
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


BUILD = "FF1D1C05670DB6021C85B624A710B963"
CODE, ENTRY, JOIN = 0x38ea000, 0x38eac68, 0x38ead40
STATE, DESCRIPTOR = 0x20000, 0x23000
OUTPUTS = (r.UC_ARM64_REG_X8, r.UC_ARM64_REG_X9, r.UC_ARM64_REG_X12,
           r.UC_ARM64_REG_X13, r.UC_ARM64_REG_X14, r.UC_ARM64_REG_NZCV)


def cases():
    rng = random.Random(ENTRY)
    for source in (DESCRIPTOR, STATE + 0x173, STATE + 0x180):
        for flags in (0x2, 0x12, 0x132, 0x322, 0xc232):
            for kind in (2, 5):
                for count in (0, 1, 0x80000000, 0xffffffff):
                    memory = {**dict(enumerate(rng.randbytes(0x248), STATE)),
                              **dict(enumerate(rng.randbytes(0xe1), source))}
                    memory.update(enumerate(struct.pack("<I", count), source + 0x80))
                    memory[source + 0x71] = (memory[source + 0x71] & 0xf8) | kind
                    yield STATE, source, flags, kind, rng.getrandbits(64), rng.getrandbits(64), memory


def original(image, state, descriptor, flags, kind, x13, x14, initial):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    machine.mem_map(CODE, 0x1000)
    machine.mem_write(CODE, bytes(image[CODE:CODE + 0x1000]))
    pages = {page: bytearray(0x1000)
             for page in range(state & ~0xfff, ((state + 0x244) & ~0xfff) + 0x1000, 0x1000)}
    for address, value in initial.items():
        page = address & ~0xfff
        if page not in pages:
            pages[page] = bytearray(0x1000)
        pages[page][address - page] = value
    for page, data in pages.items():
        machine.mem_map(page, 0x1000)
        machine.mem_write(page, bytes(data))
    registers = {r.UC_ARM64_REG_X19: state, r.UC_ARM64_REG_X11: descriptor,
                 r.UC_ARM64_REG_X10: descriptor + 0x68, r.UC_ARM64_REG_X8: flags & ~0x200,
                 r.UC_ARM64_REG_X9: flags, r.UC_ARM64_REG_X12: kind,
                 r.UC_ARM64_REG_X13: x13, r.UC_ARM64_REG_X14: x14, r.UC_ARM64_REG_NZCV: 0x60000000}
    for register, value in registers.items():
        machine.reg_write(register, value)
    effects = []
    readable = set(initial)
    visits = []

    def code(uc, pc, size, user):
        if not ENTRY <= pc <= JOIN:
            raise ValueError(f"Unexpected PC {pc:#x}")
        if pc == JOIN:
            visits.append(pc)
            uc.emu_stop()

    def read(uc, access, address, size, value, user):
        if not all(byte in readable for byte in range(address, address + size)):
            raise ValueError(f"Uncaptured read {address:#x}+{size}")
        effects.append((0, address, size, int.from_bytes(uc.mem_read(address, size), "little")))

    def write(uc, access, address, size, value, user):
        readable.update(range(address, address + size))
        effects.append((1, address, size, value & ((1 << (8 * size)) - 1)))

    machine.hook_add(UC_HOOK_CODE, code)
    machine.hook_add(UC_HOOK_MEM_READ, read)
    machine.hook_add(UC_HOOK_MEM_WRITE, write)
    machine.emu_start(ENTRY, CODE + 0x1000, count=192)
    assert visits == [JOIN]
    return tuple(machine.reg_read(register) for register in OUTPUTS), tuple(effects)


def compare(image, candidate, samples):
    payload = []
    for state, descriptor, flags, _, x13, x14, memory in samples:
        fields = (state, descriptor, flags, x13, x14, len(memory))
        payload.append(" ".join(map(str, (*fields, *(v for pair in memory.items() for v in pair)))))
    output = subprocess.run([str(candidate.resolve())], input="\n".join(payload) + "\n",
                            check=True, text=True, capture_output=True).stdout.splitlines()
    counts = Counter()
    for case, line in zip(samples, output, strict=True):
        stock = original(image, *case)
        assert stock == original(image, *case)
        actual = tuple(map(int, line.split()))
        values, effects = stock
        assert actual[:6] == values, (values, actual[:6])
        assert actual[6:] == tuple(v for event in effects for v in event)
        counts[sum(operation for operation, *_ in effects)] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    counts = compare(nso.load(args.module).image, args.candidate, list(cases()))
    print(f"{sum(counts.values())} descriptor cases matched two originals, with exact join registers, "
          f"NZCV and ordered loads and stores. Store counts: {dict(sorted(counts.items()))}.")


if __name__ == "__main__":
    main()
