"""Prepare independent sweep fixtures and compare the corresponding gameplay traces."""
from collections import defaultdict
import re
import struct

from unicorn.arm64_const import (UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_X0,
    UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4)

from tests.acnh_provider_cell_replay import words, RETURN
from tests.function_replay import STACK_END, run

SWEEP = 0x15f23b0
REGISTERS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2,
             UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0)


def read_captures(path):
    cases = defaultdict(lambda: {"loads": [], "bytes": {}, "calls": []})
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        op, identity, *args = line.split()
        if int(identity, 16) >> 32 != SWEEP:
            continue
        case = cases[identity]
        if op == "HEIGHT_BYTES":
            tag, site, pointer, hex_bytes = args
            counter = case["calls"][-1]["bytes"] if tag not in {"arg", "exit"} else case["bytes"]
            counter[tag, int(site, 16)] = int(pointer, 16), bytes.fromhex(hex_bytes)
            continue
        values = tuple(int(value, 16) for value in args)
        if op == "HEIGHT_LOAD":
            case["loads"].append(values)
        elif op == "HEIGHT_CALL":
            case["calls"].append({"args": values, "bytes": {}})
        elif op == "HEIGHT_RETURN":
            case["calls"][-1]["returned"] = values
        elif op in ("HEIGHT_BEGIN", "HEIGHT_END"):
            case[op] = values
    if not cases:
        raise ValueError(f"{path}: no sweep captures")
    return tuple(cases.values())


def regions(initial):
    blocks = {}
    last = -2
    for address, byte in sorted(initial.items()):
        if address != last + 1:
            start = address
            blocks[start] = bytearray()
        blocks[start].append(byte)
        last = address
    return {address: bytes(data) for address, data in blocks.items()}


def replay(case, code):
    base, _, old_sp, *values = case["HEIGHT_BEGIN"]
    def local(addr):
        return old_sp - 0x3000 <= addr < old_sp
    def address(word):
        return word + (STACK_END - old_sp) if local(word) else word

    initial = {}
    for pointer, width, word in case["loads"]:
        if local(pointer):
            continue
        for i, byte in enumerate((word & ((1 << (width * 8)) - 1)).to_bytes(width, "little")):
            initial.setdefault(pointer + i, byte)
    for pointer, data in (case["bytes"]["arg", i] for i in (0, 1) if ("arg", i) in case["bytes"]):
        for i, byte in enumerate(data):
            initial[pointer + i] = byte
    for call in case["calls"]:
        if entry := call["bytes"].get(("record", 0x15f2670)):
            pointer, data = entry
            for i, byte in enumerate(data):
                initial.setdefault(pointer + i, byte)

    at = 0
    trace = []
    def intercept(uc, site, target):
        nonlocal at
        call = case["calls"][at]
        at += 1
        offset, expected, *args = call["args"]
        if (base + offset, base + expected) != (site, target):
            raise ValueError(f"unexpected sweep callback at {site-base:#x} -> {target-base:#x}")
        x0, x1, x2, x3 = (uc.reg_read(reg) for reg in REGISTERS[:4])
        if "callin" in {tag for tag, _ in call["bytes"]}:
            captured, data = call["bytes"]["callin", offset]
            point = x2 if offset == 0x15f2670 else x0
            if point != address(captured):
                raise ValueError(f"{offset:#x}: unexpected pointed-to argument")
            # The cast frame's final word is padding and carries prior stack bytes.
            size = 28 if offset == 0x15f2780 else len(data)
            seen = bytes(uc.mem_read(point, size))
            if seen != data[:size]:
                raise ValueError(f"{offset:#x}: expected {data[:size].hex()}, got {seen.hex()}")
            if offset == 0x15f2670:
                trace.extend((3, *struct.unpack_from("<2I", seen, 8)))
            elif offset == 0x15f2780:
                trace.extend((4, *struct.unpack_from("<3I", seen), int(bool(x3))))
                if x3:
                    trace.extend(struct.unpack("<2I", uc.mem_read(x3 + 8, 8)))
            else:
                trace.extend((1, *struct.unpack("<3I", seen)))
        if offset == 0x15f2548:
            trace.extend((2, uc.reg_read(UC_ARM64_REG_S0), uc.reg_read(UC_ARM64_REG_S1)))

        if captured := call["bytes"].get(("child", offset)):
            pointer, data = captured
            relevant = x3 if offset == 0x15f2670 else x1 if offset == 0x15f2780 else x0
            if relevant != address(pointer):
                raise ValueError(f"{offset:#x}: unexpected output pointer")
            uc.mem_write(relevant, data)
        if captured := call["bytes"].get(("flag", offset)):
            pointer, data = captured
            if x2 != address(pointer):
                raise ValueError("unexpected cast flag pointer")
            uc.mem_write(x2, data)
        _, result, scalar = call["returned"]
        uc.reg_write(UC_ARM64_REG_X0, address(result))
        uc.reg_write(UC_ARM64_REG_S0, scalar)
        return True

    regs = {reg: address(value) for reg, value in zip(REGISTERS, values)}
    callees = {base + call["args"][0]: (base + call["args"][1], words(RETURN)) for call in case["calls"]}
    result = run(base + SWEEP, code, regs, (UC_ARM64_REG_X0,), regions(initial),
                 callees=callees, call_model=intercept)
    if len(result.calls) != len(case["calls"]):
        raise ValueError(f"replay stopped before all captured calls")
    pointer = values[1]
    if pointer:
        _, output = case["bytes"]["exit", 1]
        start, data = next((start, data) for start, data in result.memory.items()
                           if start <= pointer and pointer + len(output) <= start + len(data))
        if data[pointer-start:pointer-start + len(output)] != output:
            raise ValueError("sweep output differs")
    if result.registers[UC_ARM64_REG_X0] != case["HEIGHT_END"][0]:
        raise ValueError("sweep return differs")
    writes = [(address - pointer + 32, width, value) for address, width, value in result.writes]
    return (result.registers[UC_ARM64_REG_X0], len(writes), *(item for write in writes for item in write), *trace)


def candidate_input(case):
    base, _, _, _, output, mode, *_ = case["HEIGHT_BEGIN"]
    descriptor = struct.unpack("<8I", case["bytes"]["arg", 0][1])
    loads = {address: word for address, _, word in case["loads"]}
    by_site = defaultdict(list)
    for call in case["calls"]:
        by_site[call["args"][0]].append(call)
    angle = by_site[0x15f2548][0]["returned"][1]
    trig = loads[base + 0x4ab2d00] + 16 * (angle >> 24)
    samples = tuple(loads[trig + 4*i] for i in range(4))
    stage = by_site[0x15f2670][0]["bytes"]
    eligible = struct.unpack_from("<H", stage["record", 0x15f2670][1])[0] != 0xfffe
    root = struct.unpack_from("<2i", stage["child", 0x15f2670][1], 8)
    heights = []
    for site in (0x15f2488, 0x15f251c):
        if not by_site[site]:
            heights.extend((0, 0, 0, 0, 0))
        else:
            call = by_site[site][0]
            before = call["bytes"]["callin", site][1]
            after = call["bytes"]["child", site][1]
            heights.extend((call["returned"][2], int(before != after), *struct.unpack("<3I", after)))
    casts = []
    for call in by_site[0x15f2780]:
        _, destination, accepted, blocked, output_xyz = cast_signature(call)
        casts.extend((accepted, blocked, *output_xyz))
    casts.extend(0 for _ in range(95 - len(casts)))
    return mode & 1, int(bool(output)), int(eligible), *root, angle, *descriptor, *samples, *heights, *casts


def cast_signature(call):
    site = 0x15f2780
    before = struct.unpack_from("<3I", call["bytes"]["callin", site][1])
    after = struct.unpack("<3I", call["bytes"]["child", site][1])
    return (before, int(bool(call["args"][5])), int(bool(call["returned"][1] & 0xffffffff)),
            int(bool(call["bytes"]["flag", site][1][0])), after)


ROOT_TRACE = re.compile(
    r"acnh shovel traversal sweep (original|cpp) (entry=15f23b0 call=[0-9a-f]+ mode=[01]"
    r" initial=[0-9a-f,]+ final=[0-9a-f,]+ out=[01])"
)
CAST_TRACE = re.compile(
    r"acnh shovel sweep cast input=([0-9a-f,]+) destination=([01]) accepted=([01]) blocked=([01]) output=([0-9a-f,]+)"
)


def compare_live(captures, original, cpp):
    states = []
    native_casts = []
    for path, route in ((original, "original"), (cpp, "cpp")):
        cases = []
        for line in path.read_text(errors="replace").splitlines():
            if match := ROOT_TRACE.fullmatch(line):
                if match[1] != route:
                    raise ValueError(f"{path}: sweep selected the wrong route")
                cases.append(match[2])
            if route == "cpp" and (match := CAST_TRACE.fullmatch(line)):
                values = tuple(int(match[i]) for i in (2, 3, 4))
                point = tuple(int(value, 16) for value in match[1].split(","))
                output = tuple(int(value, 16) for value in match[5].split(","))
                native_casts.append((point, *values, output))
        if not cases:
            raise ValueError(f"{path}: no sweep route traces")
        states.append(cases)
    if states[0] != states[1] or len(captures) != len(states[0]):
        raise ValueError("original and C++ sweep route results differ")
    expected_casts = [cast_signature(call) for case in captures for call in case["calls"]
                      if call["args"][0] == 0x15f2780]
    if expected_casts != native_casts:
        raise ValueError("ordered sweep collision inputs or outputs differ")
    print(f"{len(captures)} sweep results and {len(expected_casts)} ordered collision calls match both gameplay routes")
