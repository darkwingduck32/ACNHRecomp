"""Replay the captured original shovel point classifier with site-specific external effects."""
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
import argparse
import re
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.acnh_shovel_point_classifier_fixtures import synthetic_cases
from tests.acnh_provider_cell_replay import words, RETURN
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

ENTRY = 0x15f3ed0
SIZE = 0xa38
BUILD = "FF1D1C05670DB6021C85B624A710B963"
TRACE = re.compile(r"acnh shovel point (original|cpp) entry=(15f3ed0) (call=.* out=[0-9a-f]+)")
BUFFER_REGISTERS = {0x15f418c: 1, 0x15f41dc: 0, 0x15f43d0: 0, 0x15f45dc: 0, 0x15f483c: 0}

@dataclass
class Capture:
    base: int
    caller: int
    sp: int
    point: tuple = None
    entry: tuple = None
    exit: tuple = None
    return_value: int = None
    calls: list = field(default_factory=list)
    loads: list = field(default_factory=list)
    child_buffers: dict = field(default_factory=dict)
    outcomes: dict = field(default_factory=dict)

def read_captures(path):
    captures = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("POINT_"):
            continue
        tag, key, *args = line.split()
        key = int(key)
        if tag == "POINT_BEGIN":
            if key in captures:
                raise ValueError(f"duplicate capture {key}")
            captures[key] = Capture(*(int(value, 16) for value in args))
            continue
        cap = captures[key]
        if tag == "POINT_BYTES":
            kind, site, address, data = args
            buffer = (int(address, 16), bytes.fromhex(data))
            if kind == "child":
                cap.child_buffers[int(site, 16)] = buffer
            else:
                setattr(cap, kind, buffer)
        elif tag == "POINT_LOAD":
            address, size, data = args
            cap.loads.append((int(address, 16), int(size), int(data, 16)))
        elif tag == "POINT_CALL":
            cap.calls.append(tuple(int(value, 16) for value in args))
        elif tag == "POINT_RETURN":
            site, value, float_bits = (int(value, 16) for value in args)
            cap.outcomes[site] = (value, float_bits)
        elif tag == "POINT_END":
            cap.return_value = int(args[0], 16)
    return captures

def immed(reg, value):
    return [0xd2800000 | ((value & 0xffff) << 5) | reg,
            *(0xf2800000 | (i << 21) | (((value >> (16*i)) & 0xffff) << 5) | reg
              for i in range(1, 4) if ((value >> (16*i)) & 0xffff))]

def stub_for_site(capture, site, args):
    ops = []
    if site in capture.child_buffers:
        address, data = capture.child_buffers[site]
        arg = BUFFER_REGISTERS[site]
        if address != args[arg] or len(data) % 4:
            raise ValueError(f"unexpected child output at {site:#x}")
        for offset in range(0, len(data), 4):
            ops.extend(immed(10, int.from_bytes(data[offset:offset+4], "little")))
            ops.append(0xb9000000 | (offset // 4) << 10 | arg << 5 | 10)
    returned, float_bits = capture.outcomes[site]
    if capture.sp - 0xf0 <= returned < capture.sp:
        ops.append(0x910003e0 | (returned - capture.sp + 0xf0) << 10)
    else:
        ops.extend(immed(0, returned))
    ops.extend(immed(8, float_bits))
    ops.extend((0x1e270100, RETURN))
    return ops

def declared_calls(capture):
    by_target = defaultdict(list)
    for site, target, *args in capture.calls:
        by_target[target].append((site, stub_for_site(capture, site, args)))
    output = {}
    for target, alternatives in by_target.items():
        dispatch = []
        branches = []
        for site, _ in alternatives:
            dispatch.extend(immed(9, capture.base+site+4))
            dispatch.append(0xeb0903df)  # cmp x30, x9
            branches.append(len(dispatch))
            dispatch.append(0)
        dispatch.append(0xd4200000)
        for (site, stub), branch in zip(alternatives, branches):
            dispatch[branch] = 0x54000000 | ((len(dispatch) - branch) << 5)
            dispatch.extend(stub)
        body = words(*dispatch)
        output.update({capture.base+site: (capture.base+target, body) for site, _ in alternatives})
    return output

def external_memory(capture):
    seed = {}
    def place(start, data):
        for address, byte in enumerate(data, start):
            seed.setdefault(address, byte)
    place(*capture.entry)
    place(*capture.point)
    for start, size, value in capture.loads:
        if not capture.sp - 0xf0 <= start < capture.sp:
            place(start, (value % (1 << (8*size))).to_bytes(size, "little"))
    blocks = {address: bytearray(data) for address, data in (capture.entry, capture.point)}
    claimed = {address for start, data in blocks.items() for address in range(start,start+len(data))}
    prev = 0
    for address, byte in sorted(seed.items()):
        if address in claimed:
            prev = 0
            continue
        if not prev or address != prev+1:
            anchor = address
            blocks[anchor] = bytearray()
        blocks[anchor].append(byte)
        prev = address
    return {address: bytes(data) for address, data in blocks.items()}

def replay(module, cap):
    if cap.return_value is None or cap.exit is None:
        raise ValueError("incomplete point classifier capture")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    mem = external_memory(cap)
    args = {UC_ARM64_REG_X0: cap.point[0], UC_ARM64_REG_X1: cap.entry[0]}
    callees = declared_calls(cap)
    first = run(cap.base+ENTRY, code, args, (UC_ARM64_REG_X0,), mem, callees=callees)
    if first != run(cap.base+ENTRY, code, args, (UC_ARM64_REG_X0,), mem, callees=callees):
        raise ValueError("original AArch64 runs disagree")
    visited = [(site-cap.base, target-cap.base) for site, target, _ in first.calls]
    allowed = [(site, target) for site, target, *_ in cap.calls]
    if visited != allowed:
        raise ValueError(f"original visited {visited}; expected {allowed}")
    returned = first.registers[UC_ARM64_REG_X0]
    if returned != cap.return_value:
        raise ValueError(f"original returned {returned}; expected {cap.return_value}")
    request = first.memory[cap.exit[0]]
    if request != cap.exit[1]:
        raise ValueError(f"original wrote {request.hex()}; expected {cap.exit[1].hex()}")
    return first

def payload(cap):
    mem = external_memory(cap)
    pairs = [(address + offset, byte) for address, data in mem.items() for offset, byte in enumerate(data)]
    fields = [cap.base, cap.sp - 0xf0, cap.point[0], cap.entry[0], len(pairs), len(cap.calls)]
    fields += [value for pair in pairs for value in pair]
    for site, target, *arguments in cap.calls:
        result, float_bits = cap.outcomes[site]
        address, data = cap.child_buffers.get(site, (0, b""))
        fields.extend([site, target, result, float_bits, address, len(data), *arguments[:5], *data])
    return " ".join(map(str, fields))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--no-op", action="store_true")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        raise ValueError(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(module)):
        raise ValueError("unexpected point classifier boundary")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None, (ENTRY,), TRACE)
    captures = synthetic_cases(Capture)
    if args.capture:
        live = read_captures(args.capture)
        if not live:
            raise SystemExit("no classifier captures")
        captures.update(live)
    command = [str(args.candidate.resolve())]
    if args.no_op:
        command.append("no-op")
    proc = subprocess.run(command, input="\n".join(map(payload, captures.values())) + "\n",
                          capture_output=True, text=True, timeout=30)
    if proc.returncode:
        raise SystemExit(proc.stderr)
    results = proc.stdout.splitlines()
    if len(results) != len(captures):
        raise SystemExit("candidate returned the wrong number of cases")
    for (key, cap), line in zip(captures.items(), results):
        try:
            ref = replay(module, cap)
            expected = [cap.return_value, len(cap.calls), *cap.exit[1],
                        *(value for write in ref.writes for value in write)]
            actual = list(map(int, line.split()))
            if actual != expected:
                first_diff = next((idx for idx, pair in enumerate(zip(expected, actual)) if pair[0] != pair[1]), None)
                raise ValueError(f"mismatch at output word {first_diff}: original {expected}; C++ {actual}")
        except Exception as error:
            raise RuntimeError(f"replay case {key} ({cap.caller:#x}): {error}") from error
    print(f"{len(captures)} cases at {ENTRY:#x} match both original AArch64 runs")


if __name__ == "__main__":
    main()
