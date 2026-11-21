"""Compare the scoped terrain node mask with the original draw decision."""
import argparse
from itertools import product
from pathlib import Path
import subprocess

from recomp import nso
from tests.acnh_render_pass_replay import BASE, BUILD, X, put
from tools.acnh_version import build_id
from unicorn import UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc
from unicorn import arm64_const as r


ENTRY, BRANCH, BYPASS = (BASE + offset for offset in (0x146b19c, 0x146b1c8, 0x146b1cc))
TABLE, STACK, NODE = BASE + 0x4075b7c, 0x700100, 0x31000
PASSES = (0, 2, 9)


def fixtures():
    for pass_id, gate, mask, flags in product(
            (*PASSES, 1, 10, 12, 255), (0, 1, 0x80000000),
            (0, 0x40, 0x80000000), (0, 0x40, 0x80000000, 0xffffffff)):
        indices = (pass_id, 1) if pass_id in PASSES else (pass_id,)
        for index in indices:
            data = {}
            for address, kind, value in (
                    (STACK + 0x58, "Q", pass_id), (STACK + 0x24, "I", gate),
                    (STACK + 8, "Q", index), (TABLE + 4 * index, "I", mask), (NODE + 0x18, "I", flags)):
                put(data, address, kind, value)
            yield pass_id, gate, index, data


def original(image, data):
    machine = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    for page in {ENTRY & -4096, *(address & -4096 for address in data)}:
        machine.mem_map(page, 4096)
    machine.mem_write(ENTRY, bytes(image[ENTRY - BASE:BYPASS - BASE]))
    for address, value in data.items():
        machine.mem_write(address, bytes([value]))
    for register, value in ((X[19], NODE), (X[29], STACK + 0x70),
                            (r.UC_ARM64_REG_SP, STACK), (r.UC_ARM64_REG_NZCV, 0x90000000)):
        machine.reg_write(register, value)
    reads = []

    def access(uc, kind, address, width, value, _):
        assert kind != UC_MEM_WRITE
        reads.append((address, width, int.from_bytes(uc.mem_read(address, width), "little")))

    def stop(uc, pc, size, data):
        if pc == BRANCH:
            uc.emu_stop()

    machine.hook_add(UC_HOOK_CODE, stop)
    machine.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access)
    machine.emu_start(ENTRY, BYPASS, count=12)
    end = machine.reg_read(r.UC_ARM64_REG_PC)
    assert end in (BRANCH, BYPASS)
    assert machine.reg_read(X[19]) == NODE
    return end, tuple(machine.reg_read(reg) for reg in (X[8], X[9], r.UC_ARM64_REG_NZCV)), reads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    assert build_id(args.module) == BUILD
    image = nso.load(args.module).image
    samples = list(fixtures())
    payload = [" ".join(map(str, (TABLE, NODE, STACK, len(data),
                                  *(word for pair in data.items() for word in pair))))
               for _, _, _, data in samples]
    result = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                            capture_output=True, check=True, text=True).stdout.splitlines()
    supported = 0
    for (pass_id, gate, index, data), line in zip(samples, result, strict=True):
        end, registers, reads = stock = original(image, data)
        assert stock == original(image, data)
        accepted, *values = map(int, line.split())
        assert accepted == (pass_id in PASSES and gate != 0 and index == pass_id)
        if accepted:
            assert end == BRANCH and tuple(values[:3]) == registers
            values = values[3:]
            expected = reads
            supported += 1
        else:
            consumed = 1 if pass_id not in PASSES else 2 if not gate else 3
            expected = reads[:consumed]
        assert list(zip(values[::3], values[1::3], values[2::3], strict=True)) == expected
    print(f"{len(samples)} twice-original node mask cases, {supported} exact joins, zero writes.")


if __name__ == "__main__":
    main()
