"""Replay bounded AArch64 code with explicit memory and declared callees."""
from dataclasses import dataclass

from unicorn import (
    UC_ARCH_ARM64, UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE, UC_MEM_WRITE, UC_MODE_ARM, Uc, UcError,
)
from unicorn import arm64_const
from unicorn.arm64_const import UC_ARM64_REG_LR, UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_X0


PAGE = 0x1000
STACK_BASE = 0x6000000000
STACK_SIZE = 0x10000
STACK_END = STACK_BASE + STACK_SIZE
RETURN = STACK_END + PAGE
X_REGISTERS = tuple(getattr(arm64_const, f"UC_ARM64_REG_X{i}") for i in range(31))


@dataclass(frozen=True)
class Result:
    registers: dict[int, int]
    memory: dict[int, bytes]
    written_bytes: frozenset[int]
    writes: tuple[tuple[int, int, int | None], ...]
    calls: tuple[tuple[int, int, int], ...]


def run(entry, code, registers, outputs, memory, *, instruction_limit=10000, callees=None, call_model=None):
    """Execute a fixture and optionally model effects at declared linked calls."""
    if entry <= 0 or entry % 4 or not code or len(code) % 4 or instruction_limit < 1:
        raise ValueError("invalid function range or instruction limit")
    if {UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_LR} & registers.keys():
        raise ValueError("the runner supplies PC, SP, and LR")

    declared_calls = callees or {}
    blocks = {entry: code}
    for site, (target, body) in declared_calls.items():
        if site % 4 or target <= 0 or target % 4 or not body or len(body) % 4:
            raise ValueError("invalid declared call")
        if blocks.setdefault(target, body) != body:
            raise ValueError("conflicting callee bodies")
    regions = sorted([(STACK_BASE, STACK_END), (RETURN, RETURN + 4)]
                     + [(address, address + len(body)) for address, body in blocks.items()]
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
    for address, body in blocks.items():
        uc.mem_write(address, body)
    for address, data in memory.items():
        uc.mem_write(address, data)
    for reg, value in registers.items():
        uc.reg_write(reg, value)
    uc.reg_write(UC_ARM64_REG_SP, STACK_END)
    uc.reg_write(UC_ARM64_REG_LR, RETURN)

    accessible = [(address, address + len(data)) for address, data in memory.items()]
    written = set()
    writes = []
    calls = []
    frames = [(entry, RETURN, STACK_END)]
    failure = None
    last_instruction = None

    def stop(machine, error):
        nonlocal failure
        failure = error
        machine.emu_stop()

    def check_instruction(machine, address, size, _):
        nonlocal last_instruction
        start, resume, sp = frames[-1]
        body = blocks[start]
        if not start <= address <= start + len(body) - size:
            stop(machine, f"execution outside function at {address:#x}")
            return
        word = int.from_bytes(body[address - start:address - start + size], "little")
        last_instruction = word
        opcode = word & 0xfc000000
        indirect = word & 0xfffffc1f == 0xd63f0000
        indirect_tail = word & 0xfffffc1f == 0xd61f0000
        linked = opcode == 0x94000000 or indirect
        if linked or ((opcode == 0x14000000 or indirect_tail) and address in declared_calls):
            if indirect or indirect_tail:
                register = (word >> 5) & 31
                target = machine.reg_read(X_REGISTERS[register]) if register < 31 else 0
            else:
                displacement = ((word & 0x3ffffff) ^ 0x2000000) - 0x2000000
                target = address + 4 * displacement
            declared = declared_calls.get(address)
            if not declared or target != declared[0]:
                stop(machine, f"unexpected call at {address:#x}")
                return
            argument = machine.reg_read(UC_ARM64_REG_X0)
            if linked and call_model is not None and call_model(machine, address, target):
                calls.append((address, target, argument))
                machine.reg_write(UC_ARM64_REG_LR, address + size)
                machine.reg_write(UC_ARM64_REG_PC, address + size)
                return
            if linked:
                frames.append((target, address + size, machine.reg_read(UC_ARM64_REG_SP)))
            else:
                if machine.reg_read(UC_ARM64_REG_LR) != resume or machine.reg_read(UC_ARM64_REG_SP) != sp:
                    stop(machine, "tail call did not restore the expected link register or stack pointer")
                    return
                frames[-1] = (target, resume, sp)
            calls.append((address, target, argument))
        elif word == 0xd65f03c0:
            if machine.reg_read(UC_ARM64_REG_LR) != resume or machine.reg_read(UC_ARM64_REG_SP) != sp:
                stop(machine, "RET did not restore the expected link register or stack pointer")
                return
            frames.pop()

    def check_memory(machine, access, address, size, value, _):
        if STACK_BASE <= address and address + size <= STACK_END:
            return
        write = access == UC_MEM_WRITE
        if not any(start <= address and address + size <= stop for start, stop in accessible):
            stop(machine, f"unexpected {'write' if write else 'read'} at {address:#x} ({size} bytes)")
        elif write:
            written.update(range(address, address + size))
            # Unicorn's value argument is at most 64 bits.
            writes.append((address, size, value & ((1 << (8 * size)) - 1) if size <= 8 else None))

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
    if frames or last_instruction != 0xd65f03c0:  # RET X30
        raise ValueError("function reached the sentinel without RET X30")
    if uc.reg_read(UC_ARM64_REG_SP) != STACK_END:
        raise ValueError("function did not restore SP")
    return Result(
        {reg: uc.reg_read(reg) for reg in outputs},
        {address: bytes(uc.mem_read(address, len(data))) for address, data in memory.items()},
        frozenset(written),
        tuple(writes),
        tuple(calls),
    )
