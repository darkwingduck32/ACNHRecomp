"""Replay alternate terrain rectangles against the original AArch64 body."""
import argparse
from collections import defaultdict
from pathlib import Path
import re
import subprocess

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import RETURN, words
from tests.acnh_shovel_marker_replay import Capture, write_word
from tests.acnh_shovel_point_classifier_replay import immed
from tests.acnh_shovel_alternate_terrain_fixtures import synthetic_cases
from tests.acnh_shovel_target_replay import compare_live
from tests.function_replay import run
from tools.acnh_version import build_id
from unicorn.arm64_const import UC_ARM64_REG_X0

BUILD = "FF1D1C05670DB6021C85B624A710B963"
ENTRY, SIZE, FRAME = 0x87bf30, 0x300, 0x90
REJECTION = (0x87c0bc, 0x87c0fc, 0x87c13c, 0x87c17c)
REQUIREMENT = (0x87c0ec, 0x87c12c, 0x87c16c, 0x87c1ac)
SHAPES = {
    0x87bf88: ((0, "low"), (0, "high"), (1, "low"), (1, "high"), (2, "low")),
    0x87c048: ((0, 0), (0, 4), (0, 8), (0, 12), (1, "low")),
    **{site: ((0, "low"), (0, "high"), (1, "low")) for site in (*REJECTION, *REQUIREMENT)},
}
TAPE, STRIDE = 0x80000000, 24
TRACE = re.compile(r"acnh shovel alternate terrain (original|cpp) entry=([0-9a-f]+) (call=.* out=[0-9a-f]+)")
LIVE_ENTRIES = (ENTRY, 0x871f80, 0x1285fe0, 0x862530)



def captures(path):
    records = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, raw_id, *data = line.split()
        key = int(raw_id, 16)
        if key >> 32 != ENTRY:
            continue
        if tag == "HEIGHT_BEGIN":
            base, _, stack, *regs = (int(raw, 16) for raw in data)
            records[key] = Capture(ENTRY, base, stack, tuple(regs))
            continue
        case = records[key]
        if tag == "HEIGHT_BYTES" and data[0] == "arg":
            case.add(int(data[2], 16), bytes.fromhex(data[3]))
        elif tag == "HEIGHT_LOAD":
            address, width, value = int(data[0], 16), int(data[1]), int(data[2], 16)
            if not case.stack - FRAME <= address < case.stack:
                case.add(address, (value % (1 << (8 * width))).to_bytes(width, "little"))
        elif tag == "HEIGHT_CALL":
            site, target = (int(raw, 16) for raw in data[:2])
            case.add_call(site, target)
        elif tag == "HEIGHT_RETURN":
            site, value, _ = (int(raw, 16) for raw in data)
            if case.calls[-1].site != site:
                raise ValueError("alternate terrain callback order inconsistent")
            case.calls[-1].result = value
        elif tag == "HEIGHT_END":
            case.result = int(data[0], 16)
    if not records or any(case.result is None for case in records.values()):
        raise ValueError("gameplay capture absent or incomplete")
    return records


def callbacks(case):
    by_target = defaultdict(list)
    for slot, call in enumerate(case.calls):
        code = immed(9, TAPE + 16 + STRIDE * slot)
        for index, (reg, offset) in enumerate(SHAPES[call.site]):
            if offset == "low":
                code.append(0x2a0003ea | (reg << 16))
            elif offset == "high":
                code.append(0xd360fc0a | (reg << 5))
            else:
                code.append(0xb940000a | ((offset // 4) << 10) | (reg << 5))
            code.append(write_word(9, index * 4))
        code.extend((*immed(9, TAPE), *immed(10, slot + 1), write_word(9, 0),
                     *immed(0, call.result), RETURN))
        by_target[call.target].append((slot, call.site, code))
    output = {}
    for idx, (target, calls) in enumerate(by_target.items()):
        context = [*immed(9, TAPE), 0xb9400128]
        branches = []
        for slot, _, _ in calls:
            context.extend((0x7100011f | (slot << 10), 0))
            branches.append(len(context) - 1)
        context.append(0xd4200000)
        for branch, (_, _, body) in zip(branches, calls, strict=True):
            context[branch] = 0x54000000 | ((len(context) - branch) << 5)
            context.extend(body)
        target += case.base
        handler = case.base + 0x6000000 + idx * 0x10000
        trampoline = immed(11, handler)
        output[target + 4 * len(trampoline)] = (handler, words(*context))
        trampoline.append(0xd61f0160)
        output.update({case.base + site: (target, words(*trampoline)) for _,site,_ in calls})
    return output


def payload(case):
    data = [case.base, case.regs[0], len(case.memory), len(case.calls)]
    data.extend(value for pair in case.memory.items() for value in pair)
    for call in case.calls:
        data.extend((call.site, call.result))
    return " ".join(map(str, data))


def reference(module, case):
    memory = case.regions()
    memory[TAPE] = bytes(16 + STRIDE * len(case.calls))
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    callee = callbacks(case)
    args = (case.base + ENTRY, code, {UC_ARM64_REG_X0: case.regs[0]}, (UC_ARM64_REG_X0,), memory)
    result = run(*args, callees=callee, instruction_limit=max(10000,400*len(case.calls)))
    if result != run(*args, callees=callee, instruction_limit=max(10000,400*len(case.calls))):
        raise ValueError("unstable original")
    if case.result is not None and case.result != result.registers[UC_ARM64_REG_X0]:
        raise ValueError("AArch64 output differs from gameplay capture")
    called = [(site-case.base,target-case.base) for site,target,_ in result.calls if ENTRY <= site-case.base < ENTRY+SIZE]
    if called != [(call.site,call.target) for call in case.calls]:
        raise ValueError("AArch64 callback order differs from capture")
    tape = result.memory[TAPE]
    words = [result.registers[UC_ARM64_REG_X0],len(called)]
    for n, call in enumerate(case.calls):
        addr = 16 + n * STRIDE
        words.extend(int.from_bytes(tape[i:i+4], "little") for i in range(addr, addr+4*len(SHAPES[call.site]),4))
    if any(not TAPE <= address < TAPE + len(tape) for address, _, _ in result.writes):
        raise ValueError("unexpected guest-visible write")
    return words


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--entry", type=lambda value: int(value, 16), choices=LIVE_ENTRIES, action="append")
    parser.add_argument("--no-op", action="store_true")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((f.start, f.size) == (ENTRY, SIZE) for f in ehframe.parse(module)):
        parser.error("unexpected alternate terrain boundary")
    compare_live(args.live, None, tuple(args.entry or LIVE_ENTRIES), TRACE)
    rows = synthetic_cases(Capture)
    if args.capture:
        rows.update(captures(args.capture))
    command = [str(args.candidate.resolve()), *(["no-op"] if args.no_op else [])]
    result = subprocess.run(command, text=True, capture_output=True, timeout=30,
                            input="\n".join(map(payload, rows.values())) + "\n")
    lines = result.stdout.splitlines()
    if result.returncode:
        key = list(rows)[len(lines)] if len(lines)<len(rows) else "after final case"
        raise SystemExit(f"{key}: {result.stderr}")
    for (key, case), line in zip(rows.items(), lines, strict=True):
        try:
            expected = reference(module, case)
        except Exception as error:
            raise ValueError(f"case {key}: {error}") from error
        actual = list(map(int, line.split()))
        if expected != actual:
            word = next((i for i, (ref, cpp) in enumerate(zip(expected, actual)) if ref != cpp),
                        min(len(expected), len(actual)))
            raise SystemExit(f"{key}: word {word}, original {expected[word:word + 1]}, C++ {actual[word:word + 1]}")
    print(f"{len(rows)} alternate terrain cases match both original AArch64 replays and ordered child arguments")


if __name__ == "__main__":
    main()
