"""Compare the two simple 3.0.3 shovel step casts with the original body."""
import argparse
from collections import Counter, defaultdict
from pathlib import Path
import re
import struct
import subprocess

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import RETURN, words
from tests.acnh_shovel_collision_probes_replay import ADDRESS, POINT, REGS
from tests.acnh_shovel_marker_replay import Capture, write_word
from tests.acnh_shovel_point_classifier_replay import immed
from tests.acnh_shovel_target_replay import live_cases
from tests.function_replay import run
from tools.acnh_version import build_id
from unicorn.arm64_const import UC_ARM64_REG_X0

BUILD = "FF1D1C05670DB6021C85B624A710B963"
ENTRY, SIZE, FRAME = 0x15ed290, 0x283c, 0x140
TRACE = re.compile(r"acnh shovel simple cast (original|cpp) entry=(15ed290) (call=.* out=[0-9a-f]+)")
SITES = {0x15f0f14: 16, 0x15f0f4c: 4}
HEIGHT_PROBES = (0x15ede4c, 0x15ef5ac)
ITEM_PROBES = (0x15edfd4, 0x15ef890)
NAME_GETTERS = (0x15ef99c, 0x15ef674)
STAGE_RULES = (0x15ef9e0, 0x15ef6e8)
FIELD_LOOKUPS = (0x15ef9fc, 0x15ef704)
STAGE_CALL = (*ADDRESS, (1, "low", 4))
FIELD_CALL = (*((0, i, 4) for i in range(0, 16, 4)), (0, 16, 1), (1, "low", 4),
              *((2, i, 4) for i in range(0, 16, 4)), *((3, i, 4) for i in range(0, 16, 4)))
SHAPES = {
    **{site: (*POINT, (1, "low", 4), (2, "low", 4)) for site in HEIGHT_PROBES},
    **{site: (*POINT, (1, "low", 4)) for site in ITEM_PROBES},
    **{site: STAGE_CALL for site in (*NAME_GETTERS, *(site + 0xc for site in NAME_GETTERS))},
    **{site: ((0, "low", 4),) for site in STAGE_RULES},
    **{site: FIELD_CALL for site in FIELD_LOOKUPS},
}
TAPE, STRIDE = 0x80000000, 64


def captures(path):
    result = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, raw_key, *data = line.split()
        key = int(raw_key, 16)
        if key >> 32 != ENTRY:
            continue
        if tag == "HEIGHT_BEGIN":
            base, resume, stack, *regs = (int(raw, 16) for raw in data)
            if resume in SITES:
                result[key] = Capture(ENTRY, base, stack, tuple(regs))
            continue
        if key not in result:
            continue
        cap = result[key]
        if tag == "HEIGHT_BYTES":
            kind, slot, address, raw = data
            if kind == "arg":
                cap.add(int(address, 16), bytes.fromhex(raw))
            elif kind == "child" and int(slot, 16) in (*HEIGHT_PROBES, *ITEM_PROBES):
                cap.calls[-1].outputs = (1, *struct.unpack("<3I", bytes.fromhex(raw)))
        elif tag == "HEIGHT_LOAD":
            address, size, value = int(data[0], 16), int(data[1]), int(data[2], 16)
            if not cap.stack - FRAME <= address < cap.stack:
                cap.add(address, (value % (1 << (8 * size))).to_bytes(size, "little"))
        elif tag == "HEIGHT_CALL":
            site, target = (int(raw, 16) for raw in data[:2])
            cap.add_call(site, target)
        elif tag == "HEIGHT_RETURN":
            site, value, _ = (int(raw, 16) for raw in data)
            if cap.calls[-1].site != site:
                raise ValueError(f"out-of-order child return in {key:#x}")
            cap.calls[-1].result = value
        elif tag == "HEIGHT_END":
            cap.result = int(data[0], 16)
    for key, cap in result.items():
        if cap.result is None or not cap.calls:
            raise ValueError(f"incomplete cast capture {key:#x}")
        descriptor = bytes(cap.memory[cap.regs[0] + offset] for offset in range(28))
        radius, scale, mask, mode = struct.unpack_from("<2f2I", descriptor, 12)
        if any(cap.regs[1:4]) or mask not in (4, 16) or mode != 1 or radius < 0:
            raise ValueError(f"capture {key:#x} falls outside the simple cast scope")
    return result


def callback_code(call, index):
    code = immed(9, TAPE + 16 + STRIDE * index)
    for position, (reg, offset, width) in enumerate(SHAPES[call.site]):
        if offset == "low":
            code.append(0x2a0003ea | (reg << 16))  # mov w10, w<reg>
        elif offset == "high":
            code.append(0xd360fc0a | (reg << 5))  # lsr x10, x<reg>, #32
        else:
            load = 0x3940000a if width == 1 else 0xb940000a
            code.append(load | ((offset // width) << 10) | (reg << 5))
        code.append(write_word(9, position * 4))
    if call.outputs[0]:
        for offset, value in enumerate(call.outputs[1:]):
            code.extend((*immed(10, value), write_word(0, 4 * offset)))
    code.extend((*immed(9, TAPE), *immed(10, index + 1), write_word(9, 0),
                 *immed(0, call.result), RETURN))
    return code


def dependencies(cap):
    targets = defaultdict(list)
    for index, call in enumerate(cap.calls):
        targets[call.target].append((call.site, index, callback_code(call, index)))
    result = {}
    for target, calls in targets.items():
        code = [*immed(9, TAPE), 0xb9400128]  # ldr w8, [x9]
        branches = []
        for _, index, _ in calls:
            code.extend((0x7100011f | (index << 10), 0))  # cmp w8, #index
            branches.append(len(code) - 1)
        code.append(0xd4200000)
        for branch, (_, _, body) in zip(branches, calls, strict=True):
            code[branch] = 0x54000000 | ((len(code) - branch) << 5)
            code.extend(body)
        binary = words(*code)
        result.update({cap.base + site: (cap.base + target, binary) for site, _, _ in calls})
    return result


def payload(cap):
    data = [cap.base, cap.regs[0], len(cap.memory), len(cap.calls)]
    data.extend(value for pair in cap.memory.items() for value in pair)
    for call in cap.calls:
        data.extend((call.site, call.result, *call.outputs))
    return " ".join(map(str, data))


def expected(module, cap):
    memory = cap.regions()
    memory[TAPE] = bytes(16 + STRIDE * len(cap.calls))
    body = bytes(module.image[ENTRY:ENTRY + SIZE])
    regs = dict(zip(REGS, cap.regs, strict=True))
    callees = dependencies(cap)
    reference = run(cap.base + ENTRY, body, regs, (UC_ARM64_REG_X0,), memory, callees=callees)
    if reference != run(cap.base + ENTRY, body, regs, (UC_ARM64_REG_X0,), memory, callees=callees):
        raise ValueError("unstable original")
    returned = reference.registers[UC_ARM64_REG_X0]
    if cap.result is not None and cap.result != returned:
        raise ValueError(f"captured return {cap.result} differs from reference {returned}")
    order = [(site - cap.base, target - cap.base) for site, target, _ in reference.calls]
    if order != [(call.site, call.target) for call in cap.calls]:
        raise ValueError("original child order differs")
    tape = reference.memory[TAPE]
    if int.from_bytes(tape[:4], "little") != len(cap.calls):
        raise ValueError("missing child shape")
    if any(not TAPE <= address < TAPE + len(tape) for address, _, _ in reference.writes):
        raise ValueError("unexpected write to guest-visible memory")
    output = [returned, len(cap.calls)]
    for idx, call in enumerate(cap.calls):
        start = 16 + idx * STRIDE
        stop = start + 4 * len(SHAPES[call.site])
        output.extend(int.from_bytes(tape[offset:offset + 4], "little") for offset in range(start, stop, 4))
    return output


def compare_cast_live(paths, tile):
    if not paths:
        return
    original, native = (live_cases(path, route, tile, (ENTRY,), TRACE)[ENTRY]
                        for path, route in zip(paths, ("original", "cpp"), strict=True))
    for resume, mask in SITES.items():
        prefix = f"call={resume-4:x} mask={mask:x} "
        before = Counter({state: n for state, n in original.items() if state.startswith(prefix)})
        after = Counter({state: n for state, n in native.items() if state.startswith(prefix)})
        if not before or before != after:
            raise ValueError(f"{prefix} differed: original {before}, C++ {after}")
        print(f"mask {mask:#x} at {resume-4:#x}: {before.total()} calls matched")


def main():
    from tests.acnh_shovel_simple_cast_fixtures import synthetic_cases

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--no-op", action="store_true")
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((fn.start, fn.size) == (ENTRY, SIZE) for fn in ehframe.parse(module)):
        parser.error("unexpected cast function boundary")
    compare_cast_live(args.live, tuple(args.survey_tile) if args.survey_tile else None)
    rows = synthetic_cases(Capture)
    if args.capture:
        observed = captures(args.capture)
        if not observed:
            parser.error("no simple cast captures")
        rows.update(observed)
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
        native = list(map(int, raw.split()))
        if reference != native:
            raise SystemExit(f"case {key}: original {reference}; C++ {native}")
    print(f"{ENTRY:#x}: {len(rows)} cases match both original AArch64 runs and callback shapes")


if __name__ == "__main__":
    main()
