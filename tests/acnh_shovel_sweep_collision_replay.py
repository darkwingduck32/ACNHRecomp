"""Compare the 16-sector shovel sweep cast with its original body."""
import argparse
from collections import Counter, defaultdict
from itertools import product
from pathlib import Path
import struct
import subprocess
import re

from unicorn import arm64_const as reg

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import RETURN, words
from tests.acnh_shovel_simple_cast_fixtures import NAME, ROOT, ITEM, bits, cast_state, put, trig_table
from tests.acnh_shovel_simple_cast_replay import (
    BUILD, ENTRY, SIZE, HEIGHT_PROBES, NAME_GETTERS, STAGE_RULES, FIELD_LOOKUPS, REGS, Capture,
)
from tests.function_replay import run
from tools.acnh_version import build_id


HASHES = tuple(site + 0xc for site in NAME_GETTERS)
XYZ = struct.Struct("<3I")
ROOT_WORDS = struct.Struct("<2I")
INPUTS = (reg.UC_ARM64_REG_X0, reg.UC_ARM64_REG_X1, reg.UC_ARM64_REG_X2, reg.UC_ARM64_REG_X3)
SWEEP_RESUME = 0x15f2784
TRACE = re.compile(r"acnh shovel sweep collision (original|cpp) entry=15ed290 (call=15f2780 .* out=([0-9a-f]+))")


def fixture(trig, radius, scene, touches, root_offset, mutate, odd_heading):
    capture = cast_state(Capture, trig, 4, radius, 1., scene, (-0.125, -0.0, 693.25), False)
    point = capture.regs[0]
    flag, output = point + 0x50, point + 0x60
    capture.regs = (point, output, flag, 0, 0, bits(693.25))
    capture.memory.update({point + 24 + i: value for i, value in enumerate(struct.pack("<I", 0x10001))})
    put(capture, capture.base + 0x5140234, 0x1001213d if odd_heading else 0x10000000, 4)
    capture.add(flag, b"\xa5")
    capture.add(output, XYZ.pack(0x11111111, 0x22222222, 0x33333333))

    for index in range(1 if abs(radius) < 2**-23 else 17):
        center = index == 0
        hit = index in touches
        effect = (int(mutate and (center or hit)), bits(426.25), bits(13.5), bits(-16.25))
        capture.add_call(HEIGHT_PROBES[not center], 0x15e11a0, 1 if hit else 2, effect)
        if not hit:
            continue
        if scene >= 2:
            getter, hash_site = NAME_GETTERS[not center], HASHES[not center]
            capture.add_call(getter, 0x5cb0a0, NAME)
            capture.add_call(hash_site, 0x5bd550, 0x758f7eaf if scene == 3 else 0x12)
        capture.add_call(STAGE_RULES[not center], 0x16baf44, scene & 1)
        x, z = root_offset
        if index == 16 and len(touches) > 1:
            x += 1
        capture.add_call(FIELD_LOOKUPS[not center], 0x195190c, ITEM, (1, x & 0xffffffff, z & 0xffffffff, 0))
    return capture


def synthetic_cases():
    trig = trig_table()
    for scene, touches, root, mutate in product(
        (0, 2, 3), ((), (0,), (1,), (1, 16), (0, 5, 16)), ((0, 0), (7, 0), (81, 139)), (False, True)
    ):
        yield fixture(trig, 4.75, scene, touches, root, mutate, mutate)
    for radius, touches in product((0., 2**-24, 2**-23), ((0,), (1, 16))):
        yield fixture(trig, radius, 0, touches, (7, 0), False, True)


def captures(path):
    captured = {}
    children = defaultdict(list)
    upcoming = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        tag, key, *data = line.split()
        identity = int(key, 16)
        if identity >> 32 != ENTRY:
            continue
        if tag == "HEIGHT_BEGIN":
            base, resume, stack, *regs = map(lambda word: int(word, 16), data)
            if resume == SWEEP_RESUME:
                captured[identity] = Capture(ENTRY, base, stack, tuple(regs))
        if identity not in captured or tag == "HEIGHT_BEGIN":
            continue
        capture = captured[identity]
        if tag == "HEIGHT_LOAD":
            address, width, value = map(lambda word: int(word, 16), data)
            if not capture.stack - 0x140 <= address < capture.stack:
                capture.add(address, (value % (1 << (8 * width))).to_bytes(width, "little"))
        elif tag == "HEIGHT_CALL":
            site, target, *regs = map(lambda word: int(word, 16), data)
            capture.add_call(site, target)
            observed = {"regs": regs}
            if identity in upcoming:
                recorded_site, observed["before"] = upcoming.pop(identity)
                if recorded_site != site:
                    raise ValueError(f"out-of-order callback input in capture {identity:x}")
            children[identity].append(observed)
        elif tag == "HEIGHT_RETURN":
            site, result, scalar = map(lambda word: int(word, 16), data)
            if capture.calls[-1].site != site:
                raise ValueError(f"out-of-order callback in capture {identity:x}")
            capture.calls[-1].result = result
            children[identity][-1]["scalar"] = scalar
        elif tag == "HEIGHT_BYTES":
            kind, raw_slot, raw_address, raw = data
            slot, address = int(raw_slot, 16), int(raw_address, 16)
            blob = bytes.fromhex(raw)
            if kind == "arg":
                capture.add(address, blob)
            elif kind == "exit":
                capture.final[address] = blob
            elif kind == "before":
                upcoming[identity] = slot, blob
            elif kind == "effect":
                if capture.calls[-1].site != slot:
                    raise ValueError(f"out-of-order callback bytes in capture {identity:x}")
                children[identity][-1][kind] = blob
        elif tag == "HEIGHT_END":
            capture.result = int(data[0], 16) & 0xffffffff

    for identity, capture in captured.items():
        request, output, flag, destination, *_ = capture.regs
        mode = bytes(capture.memory[request + offset] for offset in range(12, 28))
        if not output or not flag or destination or struct.unpack("<ffII", mode)[2:] != (4, 0x10001):
            raise ValueError(f"capture {identity:x} falls outside the sweep cast")
        if capture.result is None or capture.final.keys() != {output, flag} or not capture.calls:
            raise ValueError(f"incomplete sweep capture {identity:x}")

        for call, observed in zip(capture.calls, children[identity], strict=True):
            if "scalar" not in observed:
                raise ValueError(f"unterminated callback in capture {identity:x}")
            if call.site in HEIGHT_PROBES:
                call.outputs = (1, *XYZ.unpack(observed["effect"]))
            elif call.site in FIELD_LOOKUPS:
                call.outputs = (1, *ROOT_WORDS.unpack_from(observed["effect"], 8), 0)
        yield capture, children[identity]


def expected(body, capture, recorded):
    memory = capture.regions()
    declarations = {capture.base + call.site: (capture.base + call.target, words(RETURN)) for call in capture.calls}

    def replay():
        arguments = []
        position = 0

        def intercept(uc, site, target):
            nonlocal position
            if position >= len(capture.calls):
                raise ValueError(f"extra callback {site - capture.base:#x}")
            call = capture.calls[position]
            position += 1
            site -= capture.base
            if site != call.site or target != capture.base + call.target:
                raise ValueError(f"unexpected callback {site:#x} to {target - capture.base:#x}")
            x0, x1, x2, x3 = (uc.reg_read(register) for register in INPUTS)
            arguments.append(site)
            if site in HEIGHT_PROBES:
                point = bytes(uc.mem_read(x0, 12))
                shape = (*XYZ.unpack(point), x1 & 0xffffffff, x2 & 0xffffffff, uc.reg_read(reg.UC_ARM64_REG_S0))
                if recorded:
                    _, mode, index, _, _, scalar = recorded[position - 1]["regs"]
                    observed = (*XYZ.unpack(recorded[position - 1]["before"]), mode & 0xffffffff, index & 0xffffffff, scalar)
                    if shape != observed:
                        raise ValueError(f"{site:#x}: recorded probe inputs differ from replay")
                arguments.extend(shape)
                if call.outputs[0]:
                    uc.mem_write(x0, XYZ.pack(*call.outputs[1:]))
            elif site in (*NAME_GETTERS, *HASHES):
                arguments.extend((x0, x1 & 0xffffffff))
            elif site in STAGE_RULES:
                arguments.append(x0 & 0xffffffff)
            elif site in FIELD_LOOKUPS:
                record_type, table, key, skip = struct.unpack("<QIIB", uc.mem_read(x0, 17))
                point = struct.unpack("<QII", uc.mem_read(x2, 16))
                root = struct.unpack("<QII", uc.mem_read(x3, 16))
                if recorded and root != struct.unpack("<QII", recorded[position - 1]["before"]):
                    raise ValueError(f"{site:#x}: recorded root input differs from replay")
                arguments.extend((record_type, table, key, skip, x1, *point, *root))
                if call.outputs[0]:
                    uc.mem_write(x3 + 8, ROOT_WORDS.pack(*call.outputs[1:3]))
            uc.reg_write(reg.UC_ARM64_REG_X0, call.result)
            if recorded:
                uc.reg_write(reg.UC_ARM64_REG_S0, recorded[position - 1]["scalar"])
            return True

        regs = dict(zip(REGS, capture.regs, strict=True))
        result = run(capture.base + ENTRY, body, regs, (reg.UC_ARM64_REG_X0,), memory,
                     callees=declarations, call_model=intercept)
        if position != len(capture.calls):
            raise ValueError(f"completed only {position} of {len(capture.calls)} calls")
        if capture.result is not None and capture.result != result.registers[reg.UC_ARM64_REG_X0]:
            raise ValueError("original replay changed the captured return")
        flat = [result.registers[reg.UC_ARM64_REG_X0], position, len(result.writes)]
        flat.extend(value for write in result.writes for value in write)
        flat.extend(arguments)
        return result, flat

    first, values = replay()
    if (first, values) != replay():
        raise ValueError("unstable original")
    for output, effect in capture.final.items():
        matching = [data[output - pointer:output - pointer + len(effect)] for pointer, data in first.memory.items()
                    if pointer <= output and output + len(effect) <= pointer + len(data)]
        if matching != [effect]:
            raise ValueError(f"original replay changed the captured output at {output:x}")
    return values


def payload(capture):
    values = [capture.base, *capture.regs[:3], len(capture.memory), len(capture.calls)]
    values.extend(value for entry in capture.memory.items() for value in entry)
    for call in capture.calls:
        values.extend((call.site, call.result, *call.outputs))
    return " ".join(map(str, values))


def read_live(path, route):
    cases = []
    returns = Counter()
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("acnh shovel sweep collision "):
            continue
        match = TRACE.fullmatch(line)
        if not match or match[1] != route:
            raise ValueError(f"{path}: unexpected sweep collision trace: {line}")
        cases.append(match[2])
        returns[int(match[3], 16)] += 1
    if returns.keys() != {0, 4}:
        raise ValueError(f"{path}: expected both hit and miss, observed {dict(returns)}")
    print(f"{path}: {len(cases)} sweep casts, {returns[4]} hits and {returns[0]} misses")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path, help="Replay local call arguments and ordered child effects")
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if args.live and read_live(args.live[0], "original") != read_live(args.live[1], "cpp"):
        raise SystemExit("Ordered sweep collision inputs or outputs differ")
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((fn.start, fn.size) == (ENTRY, SIZE) for fn in ehframe.parse(module)):
        parser.error("unexpected cast function boundary")
    body = bytes(module.image[ENTRY:ENTRY + SIZE])
    cases = [(case, None) for case in synthetic_cases()]
    if args.capture:
        observed = list(captures(args.capture))
        if not observed or {case.result for case, _ in observed} != {0, 4}:
            parser.error("the capture must include a complete hit and miss")
        cases.extend(observed)
    result = subprocess.run([str(args.candidate.resolve())], text=True, capture_output=True,
                            input="\n".join(payload(case) for case, _ in cases) + "\n", timeout=30)
    lines = result.stdout.splitlines()
    if result.returncode or len(lines) != len(cases):
        raise SystemExit(f"case {len(lines)}: {result.stderr or 'wrong candidate case count'}")
    for index, ((capture, recorded), line) in enumerate(zip(cases, lines, strict=True)):
        try:
            original = expected(body, capture, recorded)
        except Exception as error:
            raise ValueError(f"case {index}") from error
        native = list(map(int, line.split()))
        if original != native:
            difference = next((i for i, (a, b) in enumerate(zip(original, native)) if a != b),
                              min(len(original), len(native)))
            raise SystemExit(f"case {index}, field {difference}: original={original[difference:difference + 7]}, "
                             f"C++={native[difference:difference + 7]}")
    print(f"{ENTRY:#x}: {len(cases)} sweep casts match both original AArch64 runs")


if __name__ == "__main__":
    main()
