"""Compare the typed traversal coordinator with its original AArch64 body."""
import argparse
from dataclasses import dataclass, field
from itertools import product
from pathlib import Path
import struct
import subprocess
import re

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import arm64_const as reg

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import RETURN, words
from tests.acnh_shovel_marker_replay import Capture
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x15f10f0, 0x12b8
REQUEST, OUTPUT = 0x30000, 0x31000
HEADING, SURFACE, PEAK, RECTANGLE, STAGE_HEIGHT, SWEEP = 0x5f0400, 0x15e026c, 0x15f27e0, 0x8721a0, 0x15e06d0, 0x15f23b0
NAME, HASH = 0x5cb0a0, 0x5bd550
XYZ = struct.Struct("<3I")
ORIGIN = (400., -0., 680.)
TRACE = re.compile(r"acnh shovel traversal parent (original|cpp) entry=15f10f0 (call=15f10bc .* out=[01])")


def bits(real):
    return struct.unpack("<I", struct.pack("<f", real))[0]


@dataclass
class Fixtures:
    goal: tuple = (401., 17., 682.)
    options: tuple = (1, 1, 1)
    terrain: int = 1
    output: int = OUTPUT
    returns: dict = field(default_factory=dict)
    heights: dict = field(default_factory=dict)
    vectors: dict = field(default_factory=dict)
    scalar_returns: dict = field(default_factory=dict)
    observed_order: list = field(default_factory=list)
    expected_output: bytes | None = None
    expected_return: int | None = None

    def capture(self):
        cap = Capture(ENTRY, BASE, 0, (REQUEST, self.output))
        split, alternate, stage = self.options
        flags = bytes([split, 0x41, 0, alternate, stage, 0x42, 0x43, 0x44])
        cap.add(REQUEST, struct.pack("<6f", *ORIGIN, *self.goal) + flags + bytes(16))
        if self.output == OUTPUT:
            cap.add(OUTPUT, struct.pack("<3f", -1., 18., -7.))
        cap.add(BASE + 0x4abc1a0, struct.pack("<Q", 0x37000))
        cap.add(BASE + 0x4ab2010, struct.pack("<Q", 0x37100))
        cap.add(BASE + 0x4ab27c8, struct.pack("<Q", 0x32000))
        cap.add(0x32000, struct.pack("<Q", 0x32100 if self.terrain else 0))
        cap.add(0x321a8, struct.pack("<Q", 0x33000 if self.terrain == 1 else 0))
        cap.add(BASE + 0x4ab2720, struct.pack("<Q", 0x35000))
        cap.add(0x35000, bytes(8))
        cap.add(BASE + 0x54c1b60, struct.pack("<Q", BASE + 0x54c1b58))
        cap.add(BASE + 0x4ab6298, struct.pack("<Q", 0x36000))
        cap.add(0x36000, b"\xff\xff")
        return cap


def synthetic():
    for dx, dz in product((-20., -10., 0., 10., 20.), repeat=2):
        yield Fixtures(goal=(400. + dx, 17., 680. + dz), options=(0, 0, 0))
    for site in (0x15f12f8, 0x15f13fc, 0x15f1424, 0x15f1654, 0x15f182c, 0x15f1a08):
        position = {0x15f12f8: (401., 682.), 0x15f13fc: (400., 670.), 0x15f1424: (400., 670.),
                    0x15f1654: (410., 680.), 0x15f182c: (400., 690.), 0x15f1a08: (390., 680.)}[site]
        for output in (OUTPUT, 0, REQUEST + 0x10):
            yield Fixtures(goal=(position[0], 17., position[1]), options=(0, 0, 0),
                           output=output, returns={site: 1})
    for terrain, position in product((0, 2), ((401., 17., 682.), (400., 17., 670.))):
        yield Fixtures(goal=position, options=(0, 0, 0), terrain=terrain)
    for stage_height, origin_height, output in product((14., 15.), (25., 30., float("nan")), (OUTPUT, 0, REQUEST + 0x10)):
        yield Fixtures(options=(0, 0, 1), output=output, heights={0x15f1cb8: stage_height, 0x15f1d1c: origin_height})
    for split, legs, sweep in product((0, 1), product((0, 1), repeat=4), (0, 1)):
        replies = dict(zip((0x15f1dd8, 0x15f1e24, 0x15f1fe4, 0x15f202c), legs, strict=True))
        replies.update({0x15f2140: sweep, 0x15f215c: 1 - sweep})
        yield Fixtures(options=(split, 1, 1), returns=replies)
    for height, output, corner_stage in product((25., 30.), (OUTPUT, 0, REQUEST + 0x10), (14., 15.)):
        yields = {0x15f20d0: height, 0x15f2338: height, 0x15f1f28: corner_stage}
        yield Fixtures(returns={0x15f1dd8: 1, 0x15f1e24: 1, 0x15f1fe4: 1, 0x15f202c: 1},
                       heights=yields, output=output)
    for first, second in product(((400.,15.,681.), (401.,15.,680.), (402.,17.,682.)),
                                 ((400.,15.,681.), (401.,15.,680.), (402.,17.,682.))):
        yield Fixtures(vectors={0x15f2250: first, 0x15f2260: second})
    yield Fixtures(returns={0x15f1dd8: 1, 0x15f1e24: 1, 0x15f2140: 0},
                   vectors={0x15f1dd8: (401.,18.,685.), 0x15f2140: (407.,19.,680.)})
    yield Fixtures(output=0, returns={0x15f1fe4: 1, 0x15f202c: 1, 0x15f2140: 1, 0x15f215c: 1},
                   vectors={0x15f1fe4: (404., 18., 682.), 0x15f202c: (405., 19., 681.)})


def captured_cases(path):
    cases = {}
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("HEIGHT_"):
            continue
        op, raw_id, *raw = line.split()
        identity = int(raw_id, 16)
        if identity >> 32 != ENTRY:
            continue
        if op == "HEIGHT_BEGIN":
            base, resume, stack, request, output, *_ = (int(item, 16) for item in raw)
            if base == BASE and resume == 0x15f10c0:
                cases[identity] = Capture(ENTRY, base, stack, (request, output)), Fixtures()
            continue
        if identity not in cases:
            continue
        cap, fixture = cases[identity]
        if op == "HEIGHT_BYTES":
            kind, slot, address, raw_bytes = raw
            data = bytes.fromhex(raw_bytes)
            if kind == "arg":
                cap.add(int(address, 16), data)
            elif kind == "child":
                fixture.vectors[int(slot, 16)] = struct.unpack("<3f", data)
            elif kind == "exit":
                fixture.expected_output = data
            continue
        data = tuple(int(item, 16) for item in raw)
        if op == "HEIGHT_LOAD":
            address, width, value = data
            if not cap.stack - 0x120 <= address < cap.stack:
                cap.add(address, (value % (1 << (8 * width))).to_bytes(width, "little"))
        elif op == "HEIGHT_CALL":
            fixture.observed_order.append(data[:2])
        elif op == "HEIGHT_RETURN":
            site, outcome, scalar = data
            fixture.returns[site] = outcome
            fixture.scalar_returns[site] = scalar
        elif op == "HEIGHT_VECTOR":
            site, *xyz = data
            fixture.vectors[site] = struct.unpack("<3f", XYZ.pack(*xyz))
        elif op == "HEIGHT_END":
            fixture.expected_return = data[0] & 1

    if not cases:
        raise ValueError("no direct traversal captures")
    for identity, (cap, fixture) in cases.items():
        if not fixture.observed_order or fixture.expected_return is None or (cap.regs[1] and fixture.expected_output is None):
            raise ValueError(f"incomplete direct traversal capture {identity:x}")
        yield cap, fixture


def reference(body, declarations, cap, fixture):
    arguments = []
    replies = []

    def intercept(uc, site, target):
        site, target = site - cap.base, target - cap.base
        if fixture.observed_order and fixture.observed_order[len(replies)] != (site, target):
            raise ValueError(f"captured callback order differs at {site:#x}/{target:#x}")
        x0, x1, x2, x3, x4 = (uc.reg_read(regnum) for regnum in
                               (reg.UC_ARM64_REG_X0, reg.UC_ARM64_REG_X1, reg.UC_ARM64_REG_X2,
                                reg.UC_ARM64_REG_X3, reg.UC_ARM64_REG_X4))
        scalar = uc.reg_read(reg.UC_ARM64_REG_S0)
        arguments.append(site)
        outcome = fixture.returns.get(site, 0)
        vector = tuple(map(bits, fixture.vectors[site])) if site in fixture.vectors else (0, 0, 0)
        writes = site in fixture.vectors
        if target == HEADING:
            arguments.extend((scalar, uc.reg_read(reg.UC_ARM64_REG_S1)))
        elif target in (NAME, HASH):
            arguments.extend((x0, x1 & 0xffffffff, scalar))
            vector = (fixture.scalar_returns.get(site, scalar), 0, 0)
            uc.reg_write(reg.UC_ARM64_REG_S0, vector[0])
        elif target == RECTANGLE:
            first = struct.unpack("<2I", uc.mem_read(x1 + 0x10, 8))
            last = struct.unpack("<2I", uc.mem_read(x1 + 0x20, 8))
            arguments.extend((x0, *first, *last, x2 & 0xffffffff))
        elif target in (SURFACE, STAGE_HEIGHT):
            arguments.extend(XYZ.unpack(uc.mem_read(x0, 12)))
            arguments.extend((x1, x2 & 0xffffffff))
            if target == STAGE_HEIGHT:
                arguments.extend((x4 & 0xffffffff, *struct.unpack("<HHB", uc.mem_read(x3, 5))))
            arguments.append(scalar)
            vector = (fixture.scalar_returns.get(site, bits(fixture.heights.get(site, 15.))), 0, 0)
            uc.reg_write(reg.UC_ARM64_REG_S0, vector[0])
        elif target == PEAK:
            current = XYZ.unpack(uc.mem_read(x0, 12))
            arguments.extend(current)
            for register, value in zip((reg.UC_ARM64_REG_S0, reg.UC_ARM64_REG_S1, reg.UC_ARM64_REG_S2),
                                       vector if writes else current, strict=True):
                uc.reg_write(register, value)
        elif target in (ENTRY, SWEEP):
            arguments.extend(uc.mem_read(x0, 31))
            arguments.extend(XYZ.unpack(uc.mem_read(x1, 12)))
            if target == SWEEP:
                arguments.append(x2 & 1)
            if writes:
                uc.mem_write(x1, XYZ.pack(*vector))
        else:
            raise ValueError(f"unmodeled original target {target:#x}")
        uc.reg_write(reg.UC_ARM64_REG_X0, outcome)
        replies.append((site, outcome, int(writes), *vector))
        return True

    result = run(cap.base + ENTRY, body, {reg.UC_ARM64_REG_X0: cap.regs[0], reg.UC_ARM64_REG_X1: cap.regs[1]},
                 (reg.UC_ARM64_REG_X0,), cap.regions(), callees=declarations, call_model=intercept)
    result_bit = result.registers[reg.UC_ARM64_REG_X0]
    if fixture.expected_return is not None and (fixture.expected_return != result_bit
                                               or len(fixture.observed_order) != len(replies)):
        raise ValueError("captured return or number of callbacks differs from the replay")
    flat = [result_bit, len(replies), len(result.writes)]
    flat.extend(word for entry in result.writes for word in entry)
    if output := cap.regs[1]:
        for address, data in result.memory.items():
            if address <= output and output + 12 <= address + len(data):
                actual = data[output - address:output - address + 12]
                if fixture.expected_output is not None and fixture.expected_output != actual:
                    raise ValueError("original replay changed the captured output")
                flat.extend(XYZ.unpack_from(data, output - address))
                break
        else:
            raise ValueError("missing original output")
    return replies, flat + arguments


def payload(cap, replies):
    values = [cap.base, *cap.regs, len(cap.memory), len(replies)]
    values.extend(word for entry in cap.memory.items() for word in entry)
    values.extend(word for reply in replies for word in reply)
    return " ".join(map(str, values))


def live_cases(path, route):
    calls = []
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("acnh shovel traversal parent "):
            continue
        match = TRACE.fullmatch(line)
        if not match or match[1] != route:
            raise ValueError(f"{path}: unexpected traversal trace: {line}")
        calls.append(match[2])
    if not calls:
        raise ValueError(f"{path}: no direct traversal calls")
    print(f"{path}: {len(calls)} direct traversal calls")
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", type=Path, help="Replay completed direct calls from local gameplay")
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("Ordered traversal descriptors, outputs, or returns differ")
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    if not any((replay.start, replay.size) == (ENTRY, SIZE) for replay in ehframe.parse(module)):
        parser.error("unexpected original traversal boundary")
    body = bytes(module.image[ENTRY:ENTRY + SIZE])
    decoder = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    declarations = {}
    for site, _, op, arg in decoder.disasm_lite(body, ENTRY):
        if op in ("bl", "blr"):
            target = int(arg.removeprefix("#"), 16) if op == "bl" else NAME
            declarations[BASE + site] = BASE + target, body if target == ENTRY else words(RETURN)

    lines, expected = [], []
    cases = [(fixture.capture(), fixture) for fixture in synthetic()]
    if args.capture:
        cases.extend(captured_cases(args.capture))
    for capture, fixture in cases:
        replies, values = reference(body, declarations, capture, fixture)
        if reference(body, declarations, capture, fixture) != (replies, values):
            raise ValueError(f"unstable original for {fixture}")
        lines.append(payload(capture, replies))
        expected.append(values)

    result = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, timeout=30)
    actual = result.stdout.splitlines()
    if result.returncode or len(actual) != len(lines):
        raise SystemExit(f"Case {len(actual)}: {result.stderr or 'wrong number of candidate results'}")
    for index, (original, line) in enumerate(zip(expected, actual, strict=True)):
        native = list(map(int, line.split()))
        if original != native:
            first = next((i for i, (a, b) in enumerate(zip(original, native)) if a != b), min(len(original), len(native)))
            raise SystemExit(f"Case {index}, field {first}: original {original[first:first + 6]}, C++ {native[first:first + 6]}")
    print(f"{ENTRY:#x}: {len(lines)} cases match two original AArch64 runs, callbacks and writes")


if __name__ == "__main__":
    main()
