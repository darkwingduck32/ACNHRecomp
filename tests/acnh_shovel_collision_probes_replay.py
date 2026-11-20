"""Replay the original height and stage-item probes used by shovel collision."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import subprocess

from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0,
)

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import RETURN, words
from tests.acnh_shovel_marker_replay import Capture, write_word
from tests.acnh_shovel_point_classifier_replay import immed
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
HEIGHT, ITEM = 0x15e11a0, 0x15ec764
ENTRIES = {HEIGHT: (0x288, 0x50), ITEM: (0x200, 0x90)}
REGS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_S0)
TRACE = re.compile(r"acnh shovel collision probe (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")
POINT = ((0, 0, 4), (0, 4, 4), (0, 8, 4))
ADDRESS = ((0, "low", 4), (0, "high", 4))
SURFACE = (*POINT, (0, "s0", 4), (1, "low", 4), (2, "low", 4))
STAGE = (*ADDRESS, (1, "low", 4))
SHAPES = {
    0x15e11d0: SURFACE,
    0x15ec7b4: SURFACE,
    0x15e1210: STAGE,
    0x15e121c: STAGE,
    0x15ec85c: STAGE,
    0x15ec868: STAGE,
    0x15e12bc: (*POINT, (1, "low", 4), (2, "low", 4), (4, "low", 4),
                (3, 0, 2), (3, 2, 2), (3, 4, 1), (0, "s0", 4)),
    0x15ec898: ((0, "low", 4),),
    0x15ec8b4: (*((0, i, 4) for i in range(0, 16, 4)), (0, 16, 1), (1, "low", 4),
                *((2, i, 4) for i in range(0, 16, 4)), *((3, i, 4) for i in range(0, 16, 4))),
    0x15ec8b8: ADDRESS,
}
TAPE = 0x80000000
STRIDE = 64


def captures(path):
    result = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, raw_id, *data = line.split()
        key = int(raw_id, 16)
        entry = key >> 32
        if entry not in ENTRIES:
            continue
        if tag == "HEIGHT_BEGIN":
            base, _, stack, *regs = (int(raw, 16) for raw in data)
            result[key] = Capture(entry, base, stack, tuple(regs))
            continue
        cap = result[key]
        if tag == "HEIGHT_BYTES" and data[0] == "arg":
            cap.add(int(data[2], 16), bytes.fromhex(data[3]))
        elif tag == "HEIGHT_LOAD":
            address, size, value = int(data[0], 16), int(data[1]), int(data[2], 16)
            if not cap.stack - ENTRIES[entry][1] <= address < cap.stack:
                cap.add(address, (value % (1 << (8 * size))).to_bytes(size, "little"))
        elif tag == "HEIGHT_CALL":
            site, target = (int(raw, 16) for raw in data[:2])
            cap.add_call(site, target)
        elif tag == "HEIGHT_RETURN":
            site, value, scalar = (int(raw, 16) for raw in data)
            call = cap.calls[-1]
            if call.site != site:
                raise ValueError(f"wrong callback order in capture {key:#x}")
            call.result = value
            call.outputs = (scalar,)
        elif tag == "HEIGHT_END":
            cap.result = int(data[0], 16)
    for key, cap in result.items():
        if cap.result is None or any(len(call.outputs) != 1 for call in cap.calls):
            raise ValueError(f"incomplete collision capture {key:#x}")
    return result


def record_shape(site, index):
    code = immed(9, TAPE + STRIDE * index)
    load = {1: 0x3940000a, 2: 0x7940000a, 4: 0xb940000a}
    for position, (reg, offset, size) in enumerate(SHAPES[site]):
        if offset == "s0":
            code.append(0x1e26000a)
        elif offset == "low":
            code.append(0x2a0003ea | (reg << 16))
        elif offset == "high":
            code.append(0xd360fc0a | (reg << 5))
        else:
            code.append(load[size] | ((offset // size) << 10) | (reg << 5))
        code.append(write_word(9, position * 4))
    return code


def dependencies(cap):
    targets = defaultdict(list)
    for index, call in enumerate(cap.calls):
        body = record_shape(call.site, index)
        body.extend((*immed(0, call.result), *immed(8, call.outputs[0]), 0x1e270100, RETURN))
        targets[call.target].append((call.site, body))
    result = {}
    for target, calls in targets.items():
        code = []
        branches = []
        for site, _ in calls:
            code.extend((*immed(9, cap.base + site + 4), 0xeb0903df, 0))
            branches.append(len(code) - 1)
        code.append(0xd4200000)
        for branch, (_, body) in zip(branches, calls):
            code[branch] = 0x54000000 | ((len(code) - branch) << 5)
            code.extend(body)
        binary = words(*code)
        result.update({cap.base + site: (cap.base + target, binary) for site, _ in calls})
    return result


def payload(cap):
    data = [cap.entry, cap.base, *cap.regs[:3], len(cap.memory), len(cap.calls)]
    data.extend(value for pair in cap.memory.items() for value in pair)
    for call in cap.calls:
        data.extend((call.site, call.result, *call.outputs))
    return " ".join(map(str, data))


def expected(module, cap):
    memory = cap.regions()
    memory[TAPE] = bytes(STRIDE * len(cap.calls))
    body = bytes(module.image[cap.entry:cap.entry + ENTRIES[cap.entry][0]])
    regs = dict(zip(REGS, cap.regs, strict=True))
    callees = dependencies(cap)
    result = run(cap.base + cap.entry, body, regs, (UC_ARM64_REG_X0,), memory, callees=callees)
    if result != run(cap.base + cap.entry, body, regs, (UC_ARM64_REG_X0,), memory, callees=callees):
        raise ValueError("unstable original")
    outcome = result.registers[UC_ARM64_REG_X0]
    if cap.result is not None and cap.result != outcome:
        raise ValueError(f"captured return {cap.result} differs from reference {outcome}")
    order = [(site - cap.base, target - cap.base) for site, target, _ in result.calls]
    if order != [(call.site, call.target) for call in cap.calls]:
        raise ValueError("original child order differs")
    tape = result.memory[TAPE]
    output = [outcome, len(cap.calls)]
    for index, call in enumerate(cap.calls):
        start = index * STRIDE
        stop = start + 4 * len(SHAPES[call.site])
        output.extend(int.from_bytes(tape[i:i+4], "little") for i in range(start, stop, 4))
    output.extend(value for write in result.writes if not TAPE <= write[0] < TAPE + len(tape) for value in write)
    return output


def main():
    from tests.acnh_shovel_collision_probes_fixtures import synthetic_cases

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--entry", type=lambda value: int(value, 16), choices=tuple(ENTRIES))
    parser.add_argument("--no-op", action="store_true")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    extents = {(fn.start, fn.size) for fn in ehframe.parse(module)}
    if any((entry, size) not in extents for entry, (size, _) in ENTRIES.items()):
        parser.error("unexpected collision probe boundaries")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None,
                 (args.entry,) if args.entry else tuple(ENTRIES), TRACE)
    rows = synthetic_cases(Capture)
    if args.capture:
        observed = captures(args.capture)
        if not observed:
            parser.error("no collision probe captures")
        rows.update(observed)
    rows = {key: cap for key, cap in rows.items() if args.entry is None or cap.entry == args.entry}
    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    result = subprocess.run(command, text=True, capture_output=True, timeout=30,
                            input="\n".join(map(payload, rows.values())) + "\n")
    lines = result.stdout.splitlines()
    if result.returncode or len(lines) != len(rows):
        key = list(rows)[len(lines)] if len(lines) < len(rows) else "after the final case"
        raise SystemExit(f"{key}: {result.stderr or 'candidate returned the wrong number of cases'}")
    for (key, cap), raw in zip(rows.items(), lines, strict=True):
        try:
            reference = expected(module, cap)
        except Exception as error:
            raise ValueError(f"case {key}: {error}") from error
        actual = list(map(int, raw.split()))
        if reference != actual:
            raise SystemExit(f"case {key}: original {reference}; C++ {actual}")
    for entry, count in Counter(cap.entry for cap in rows.values()).items():
        print(f"{entry:#x}: {count} cases match both original AArch64 runs and callback shapes")


if __name__ == "__main__":
    main()
