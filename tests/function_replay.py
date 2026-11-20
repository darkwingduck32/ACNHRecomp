"""Replay a bounded AArch64 function with explicit memory and no external calls."""
from dataclasses import dataclass

from unicorn import (
    UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc, UcError,
)
from unicorn.arm64_const import UC_ARM64_REG_LR, UC_ARM64_REG_PC, UC_ARM64_REG_SP


PAGE = 0x1000
STACK_BASE = 0x6000000000
STACK_SIZE = 0x10000
STACK_END = STACK_BASE + STACK_SIZE
RETURN = STACK_END + PAGE


@dataclass(frozen=True)
class Result:
    registers: dict[int, int]
    memory: dict[int, bytes]
    written_bytes: frozenset[int]


def run(entry, code, registers, outputs, memory, *, instruction_limit=10000):
    """Execute one independent copy of a fixture and snapshot caller-visible state."""
    if entry <= 0 or entry % 4 or not code or len(code) % 4 or instruction_limit < 1:
        raise ValueError("invalid function range or instruction limit")
    if {UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_LR} & registers.keys():
        raise ValueError("the runner supplies PC, SP, and LR")

    end = entry + len(code)
    regions = sorted([(entry, end), (STACK_BASE, STACK_END), (RETURN, RETURN + 4)]
                     + [(address, address + len(data)) for address, data in memory.items()])
    for index, (start, stop) in enumerate(regions):
        if start <= 0 or stop <= start or (index and regions[index - 1][1] > start):
            raise ValueError("invalid or overlapping fixture regions")

    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    mapped = set()
    for start, stop in regions:
        for page in range(start & -PAGE, stop, PAGE):
            if page not in mapped:
                uc.mem_map(page, PAGE)
                mapped.add(page)
    uc.mem_write(entry, code)
    for address, data in memory.items():
        uc.mem_write(address, data)
    for reg, value in registers.items():
        uc.reg_write(reg, value)
    uc.reg_write(UC_ARM64_REG_SP, STACK_END)
    uc.reg_write(UC_ARM64_REG_LR, RETURN)

    accessible = [(address, address + len(data)) for address, data in memory.items()]
    written = set()
    failure = None
    last_instruction = None

    def stop(machine, error):
        nonlocal failure
        failure = error
        machine.emu_stop()

    def check_instruction(machine, address, size, _):
        nonlocal last_instruction
        if not entry <= address <= end - size:
            stop(machine, f"execution outside function at {address:#x}")
            return
        word = int.from_bytes(code[address - entry:address - entry + size], "little")
        last_instruction = word
        if word & 0xfc000000 == 0x94000000 or word & 0xfffffc1f == 0xd63f0000:
            stop(machine, f"unexpected call at {address:#x}")

    def check_memory(machine, access, address, size, _value, _):
        if STACK_BASE <= address and address + size <= STACK_END:
            return
        write = access == UC_MEM_WRITE
        if not any(start <= address and address + size <= stop for start, stop in accessible):
            stop(machine, f"unexpected {'write' if write else 'read'} at {address:#x} ({size} bytes)")
        elif write:
            written.update(range(address, address + size))

    def check_unmapped(machine, access, address, size, _value, _):
        stop(machine, f"invalid memory access {access} at {address:#x} ({size} bytes)")
        return False

    uc.hook_add(UC_HOOK_CODE, check_instruction)
    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, check_memory)
    uc.hook_add(UC_HOOK_MEM_INVALID, check_unmapped)
    try:
        uc.emu_start(entry, RETURN, count=instruction_limit)
    except UcError as error:
        raise ValueError(failure or f"emulator failed at {uc.reg_read(UC_ARM64_REG_PC):#x}: {error}") from error
    if failure:
        raise ValueError(failure)
    if uc.reg_read(UC_ARM64_REG_PC) != RETURN:
        raise ValueError("function did not reach the return sentinel within the instruction limit")
    if last_instruction != 0xd65f03c0:  # RET X30
        raise ValueError("function reached the sentinel without RET X30")
    if uc.reg_read(UC_ARM64_REG_SP) != STACK_END:
        raise ValueError("function did not restore SP")
    return Result(
        {reg: uc.reg_read(reg) for reg in outputs},
        {address: bytes(uc.mem_read(address, len(data))) for address, data in memory.items()},
        frozenset(written),
    )
