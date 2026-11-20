"""Replay the original shovel marker group with recorded child responses."""
import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
import re
import subprocess

from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_S0,
)

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import RETURN, words
from tests.acnh_shovel_point_classifier_replay import immed
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
EXTENT, GLOBAL, MASK, CORNERS = 0x15ea0c8, 0x876320, 0x849ac8, 0x8493f8
ENTRIES = {EXTENT: (0x764, 0x80), GLOBAL: (0x94, 0x20), MASK: (0x100, 0x30), CORNERS: (0x19c, 0x90)}
TRACE = re.compile(r"acnh shovel marker (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")
REGS = (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
        UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_S0)
DIRECT_SITES = (0x15ea1b0, 0x15ea290, 0x15ea398, 0x15ea490, 0x15ea58c)
GLOBAL_SITES = (0x15ea624, 0x15ea6bc, 0x15ea75c, 0x15ea824)
FALLBACK_SITES = (0x8494bc, 0x8494f4, 0x849528, 0x84955c)
MASK_SITE, SAMPLE_SITE = 0x849af8, 0x849458
ADDRESS = ((0, "low"), (0, "high"))
TILE0 = tuple((0, i) for i in range(0, 16, 4))
TILE1 = tuple((1, i) for i in range(0, 16, 4))
SHAPES = {
    **{site: ADDRESS + TILE1 + ((2, 0), (2, 4), (2, 8)) for site in (*DIRECT_SITES, 0x876398)},
    **{site: TILE0 for site in GLOBAL_SITES},
    MASK_SITE: ADDRESS + TILE1 + tuple((reg, 0) for reg in range(2, 6)),
    SAMPLE_SITE: ADDRESS + TILE1,
    **{site: ((0, 0), (0, 8)) for site in FALLBACK_SITES},
}
TAPE = 0x80000000
STRIDE = 48


@dataclass
class Call:
    site: int
    target: int
    result: int = 0
    outputs: tuple = (0, 0, 0, 0)


@dataclass
class Capture:
    entry: int
    base: int
    stack: int
    regs: tuple
    memory: dict = field(default_factory=dict)
    calls: list = field(default_factory=list)
    final: dict = field(default_factory=dict)
    result: int | None = None

    def add(self, address, data):
        for offset, value in enumerate(data):
            self.memory.setdefault(address + offset, value)

    def regions(self):
        blocks = {}
        previous = None
        for address, value in sorted(self.memory.items()):
            if previous is None or address != previous + 1:
                start = address
                blocks[start] = bytearray()
            blocks[start].append(value)
            previous = address
        return {start: bytes(data) for start, data in blocks.items()}

    def add_call(self, site, target, result=0, outputs=(0, 0, 0, 0)):
        call = Call(site, target, result, tuple(outputs))
        self.calls.append(call)
        return call


def captures(path):
    result = {}
    pending = {}
    last_mask = None
    mask_children = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, raw_id, *data = line.split()
        key = int(raw_id, 16)
        entry = key >> 32
        if entry not in ENTRIES:
            continue
        if tag == "HEIGHT_BEGIN":
            base, _, stack, *regs = map(lambda s: int(s, 16), data)
            result[key] = Capture(entry, base, stack, tuple(regs))
            if entry == CORNERS and MASK_SITE in pending.get(last_mask, {}):
                mask_children[last_mask] = key
            pending[key] = {}
            continue
        cap = result[key]
        if tag == "HEIGHT_BYTES":
            kind, raw_slot, raw_address, raw_bytes = data
            slot, address, block = int(raw_slot, 16), int(raw_address, 16), bytes.fromhex(raw_bytes)
            if kind == "arg":
                cap.add(address, block)
            elif kind == "exit":
                cap.final[slot] = int.from_bytes(block, "little")
            elif kind == "child":
                call = next(call for call in reversed(cap.calls) if call.site == slot)
                if slot == SAMPLE_SITE:
                    call.outputs = (int.from_bytes(block[0x30:0x34], "little"), 0, 0, 0)
                elif slot in FALLBACK_SITES:
                    call.outputs = (int.from_bytes(block[:4], "little"), int.from_bytes(block[8:12], "little"), 0, 0)
        elif tag == "HEIGHT_LOAD":
            address, size, value = int(data[0], 16), int(data[1]), int(data[2], 16)
            if not cap.stack - ENTRIES[entry][1] <= address < cap.stack:
                cap.add(address, (value % (1 << (8 * size))).to_bytes(size, "little"))
        elif tag == "HEIGHT_CALL":
            site, target = (int(value, 16) for value in data[:2])
            pending[key][site] = cap.add_call(site, target)
            if entry == MASK:
                last_mask = key
        elif tag == "HEIGHT_RETURN":
            site, value, _ = (int(raw, 16) for raw in data)
            pending[key].pop(site).result = value
        elif tag == "HEIGHT_END":
            cap.result = int(data[0], 16)
    for key, cap in result.items():
        if cap.result is None or pending[key]:
            raise ValueError(f"incomplete marker capture {key:#x}")
        if cap.entry == MASK:
            child = result[mask_children[key]]
            cap.calls[0].outputs = tuple(child.final[i] for i in range(4))
    return result


def write_word(reg, offset):
    return 0xb900000a | ((offset // 4) << 10) | (reg << 5)


def stub(call, index):
    code = immed(9, TAPE + STRIDE * index)
    for pos, (reg, offset) in enumerate(SHAPES[call.site]):
        if offset == "low":
            code.append(0x2a0003ea | (reg << 16))  # mov w10, w<reg>
        elif offset == "high":
            code.append(0xd360fc0a | (reg << 5))  # lsr x10, x<reg>, #32
        else:
            code.append(0xb940000a | ((offset // 4) << 10) | (reg << 5))
        code.append(write_word(9, pos * 4))
    if call.site == MASK_SITE:
        for reg, value in enumerate(call.outputs, start=2):
            code.extend((*immed(10, value), write_word(reg, 0)))
    elif call.site == SAMPLE_SITE:
        code.extend((*immed(10, call.outputs[0]), write_word(2, 0x30)))
    elif call.site in FALLBACK_SITES:
        for offset, value in zip((0, 8), call.outputs):
            code.extend((*immed(10, value), write_word(0, offset)))
    code.extend((*immed(0, call.result), RETURN))
    return code


def dependencies(cap):
    targets = defaultdict(list)
    for index, call in enumerate(cap.calls):
        targets[call.target].append((call.site, stub(call, index)))
    result = {}
    for target, calls in targets.items():
        dispatch, branches = [], []
        for site, _ in calls:
            dispatch.extend((*immed(9, cap.base + site + 4), 0xeb0903df, 0))
            branches.append(len(dispatch) - 1)
        dispatch.append(0xd4200000)
        for branch, (_, body) in zip(branches, calls):
            dispatch[branch] = 0x54000000 | ((len(dispatch) - branch) << 5)
            dispatch.extend(body)
        binary = words(*dispatch)
        result.update({cap.base + site: (cap.base + target, binary) for site, _ in calls})
    return result


def payload(cap):
    args = cap.regs
    data = [cap.entry, cap.base, args[-1], len(cap.memory), len(cap.calls), *args[:6]]
    data.extend(value for pair in cap.memory.items() for value in pair)
    for call in cap.calls:
        data.extend((call.site, call.result, *call.outputs))
    return " ".join(map(str, data))


def expected(module, cap):
    memory = cap.regions()
    memory[TAPE] = bytes(max(1, STRIDE * len(cap.calls)))
    code = bytes(module.image[cap.entry:cap.entry + ENTRIES[cap.entry][0]])
    args = dict(zip(REGS, cap.regs, strict=True))
    callees = dependencies(cap)
    result = run(cap.base + cap.entry, code, args, (UC_ARM64_REG_X0,), memory, callees=callees)
    if result != run(cap.base + cap.entry, code, args, (UC_ARM64_REG_X0,), memory, callees=callees):
        raise ValueError("unstable AArch64 original")
    outcome = result.registers[UC_ARM64_REG_X0]
    if cap.result is not None and outcome != cap.result:
        raise ValueError(f"original return {outcome} differs from capture {cap.result}")
    if [(site-cap.base, target-cap.base) for site, target, _ in result.calls] != [
        (call.site, call.target) for call in cap.calls
    ]:
        raise ValueError("original callback order differs from fixture")
    output = [outcome, len(cap.calls)]
    if cap.entry == CORNERS:
        for idx, address in enumerate(cap.regs[2:6]):
            start, raw = next((start, raw) for start, raw in result.memory.items() if start <= address < start + len(raw))
            value = int.from_bytes(raw[address-start:address-start+4], "little")
            if idx in cap.final and cap.final[idx] != value:
                raise ValueError(f"corner {idx} expected {cap.final[idx]} but original returned {value}")
            output.append(value)
    tape = result.memory[TAPE]
    for index, call in enumerate(cap.calls):
        start = index * STRIDE
        output.extend(int.from_bytes(tape[i:i+4], "little") for i in range(start, start + 4*len(SHAPES[call.site]), 4))
    output.extend(value for write in result.writes if not TAPE <= write[0] < TAPE + len(tape) for value in write)
    return output


def main():
    from tests.acnh_shovel_marker_fixtures import synthetic_cases

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
        parser.error("unexpected marker function boundaries")
    compare_live(args.live, tuple(args.survey_tile) if args.survey_tile else None,
                 (args.entry,) if args.entry else (*ENTRIES, 0x862530, 0x87a980), TRACE)
    rows = synthetic_cases(Capture)
    if args.capture:
        observed = captures(args.capture)
        if not observed:
            parser.error("no original marker captures")
        rows.update(observed)
    rows = {key: cap for key, cap in rows.items() if not args.entry or cap.entry == args.entry}
    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    result = subprocess.run(command, text=True, capture_output=True, timeout=30,
                            input="\n".join(map(payload, rows.values())) + "\n")
    if result.returncode:
        row = len(result.stdout.splitlines())
        key = list(rows)[row] if row < len(rows) else "after final case"
        raise SystemExit(f"{key}: {result.stderr.strip()}")
    lines = result.stdout.splitlines()
    if len(lines) != len(rows):
        raise ValueError("candidate returned the wrong number of cases")
    for (key, cap), raw in zip(rows.items(), lines, strict=True):
        try:
            original = expected(module, cap)
        except Exception as error:
            raise ValueError(f"case {key}: {error}") from error
        actual = list(map(int, raw.split()))
        if original != actual:
            raise SystemExit(f"case {key}: original {original}; C++ {actual}")
    for entry, count in Counter(cap.entry for cap in rows.values()).items():
        print(f"{entry:#x}: {count} cases match both original AArch64 runs and callback shapes")


if __name__ == "__main__":
    main()
