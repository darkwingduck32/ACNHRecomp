"""Original AArch64 controls for the two read-only culler descendants."""
import argparse
from collections import Counter, defaultdict
import itertools
from pathlib import Path
import random
import re
import struct
import subprocess

from unicorn.arm64_const import (UC_ARM64_REG_S0, UC_ARM64_REG_X0, UC_ARM64_REG_X1,
                                 UC_ARM64_REG_X2)

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id
from unicorn import arm64_const as reg


BUILD = "FF1D1C05670DB6021C85B624A710B963"
CONFIG, VECTOR = 0x21000, 0x25000
FRUSTUM, PLANE = 0x63e274, 0x14e9d34
PZ, NZ, QNAN = 0, 0x80000000, 0x7fc12345
BASE = 0x4000000000
CTX, POINT, META = 0x20000, 0x22000, 0x24000
ENTRIES = {0: (0x63e274, 0x120), 1: (0x14e9d34, 0x6c)}
GAMEPLAY_SITES = {0x146a8ac, 0x146ab38}
TRACE = re.compile(
    r"acnh worker cull (original|cpp) same_input=(\w+) call=([0-9a-f]+) "
    r"input=([0-9a-f]+) w0=([01]) s0=([0-9a-f]+)"
)


def read_gameplay(path, route, comparison):
    outcomes = defaultdict(lambda: defaultdict(set))
    counts = Counter()
    for row in path.read_text(errors="replace").splitlines():
        if not row.startswith("acnh worker cull "):
            continue
        match = TRACE.fullmatch(row)
        if not match or match.groups()[:2] != (route, comparison):
            raise ValueError(f"{path}: unexpected cull trace {row}")
        site, key, w0, s0 = (int(value, 16) for value in match.groups()[2:])
        outcomes[site][key].add((w0, s0))
        counts[site] += 1
    if counts.keys() != GAMEPLAY_SITES:
        raise ValueError(f"{path}: missing or extra cull sites {counts.keys() ^ GAMEPLAY_SITES}")
    return outcomes, counts


def compare_gameplay(original_path, cpp_path):
    original, original_counts = read_gameplay(original_path, "original", "stock")
    cpp, cpp_counts = read_gameplay(cpp_path, "cpp", "match")
    for site in sorted(GAMEPLAY_SITES):
        shared = original[site].keys() & cpp[site].keys()
        if any(original[site][key] != cpp[site][key] for key in shared):
            raise ValueError(f"{site:#x}: different results on a shared input")
        original_booleans = {w0 for results in original[site].values() for w0, _ in results}
        cpp_booleans = {w0 for results in cpp[site].values() for w0, _ in results}
        if original_booleans != cpp_booleans:
            raise ValueError(f"{site:#x}: original W0 outcomes {original_booleans}, C++ {cpp_booleans}")
        print(f"{site:#x}: {len(shared)} shared inputs, {len(original[site]) - len(shared)} original-only, "
              f"{len(cpp[site]) - len(shared)} C++-only; {original_counts[site]} original / {cpp_counts[site]} C++ samples")
    print(f"All {sum(cpp_counts.values())} sampled C++ calls matched the original on the same guest input")


def f32(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def pack_words(size, words):
    data = bytearray(size)
    for offset, bits in words.items():
        struct.pack_into("<I", data, offset, bits)
    return bytes(data)


def frustum(label, expected, changes=None, radius=f32(1), x=PZ):
    config = {0xE0: f32(-10), 0xE4: f32(10)}
    config.update((off, f32(2)) for off in (0x134, 0x138, 0x13C, 0x140))
    config.update(changes or {})
    return (FRUSTUM, label, expected,
            {reg.UC_ARM64_REG_X0: CONFIG, reg.UC_ARM64_REG_X1: POINT, reg.UC_ARM64_REG_S0: radius},
            {CONFIG: pack_words(0x144, config), POINT: struct.pack("<III", x, NZ, PZ)})


def plane(label, expected, changes=None, margin=f32(1), vector_y=PZ, point_z=PZ):
    config = {0x3C: f32(1), 0x58: f32(10), 0x6C: f32(2)}
    config.update(changes or {})
    return (PLANE, label, expected,
            {reg.UC_ARM64_REG_X0: CONFIG, reg.UC_ARM64_REG_X1: POINT, reg.UC_ARM64_REG_X2: VECTOR},
            {CONFIG: pack_words(0x70, config), POINT: struct.pack("<IIII", PZ, NZ, point_z, margin),
             VECTOR: struct.pack("<II", 0, vector_y)})


def boundary_cases():
    yield frustum("inside", 1)
    yield frustum("near equality allowed", 1, {0xE0: f32(1)})
    yield frustum("near beyond equality rejected", 0, {0xE0: 0x3F800001})
    yield frustum("far equality allowed", 1, {0xE4: f32(-1)})
    yield frustum("far below equality rejected", 0, {0xE4: 0xBF800001})
    for field in (0x134, 0x138, 0x13C, 0x140):
        yield frustum(f"plane +{field:x} equality rejected", 0, {field: f32(-1)})
        yield frustum(f"plane +{field:x} one float inside", 1, {field: 0xBF7FFFFF})
        yield frustum(f"plane +{field:x} unordered rejected", 0, {field: QNAN})
    for zero in (PZ, NZ):
        yield frustum(f"radius bits {zero:08x}, inside", 1, radius=zero, x=zero)
        yield frustum(f"radius bits {zero:08x}, plane equality", 0, {0x134: zero}, radius=zero, x=zero)
    yield frustum("unordered near allowed", 1, {0xE0: QNAN})
    yield frustum("unordered far allowed", 1, {0xE4: QNAN})
    yield frustum("unordered radius rejected by plane", 0, radius=QNAN)
    yield frustum("unordered point rejected by plane", 0, x=QNAN)

    yield plane("margin one float below boundary", 0, margin=0x3FFFFFFF)
    yield plane("margin equality", 1, margin=f32(2))
    yield plane("margin one float above boundary", 1, margin=0x40000001)
    yield plane("z one float below limit", 0, point_z=0x411FFFFF)
    yield plane("z equals limit", 1, point_z=f32(10))
    yield plane("computed plane equals zero", 1, {0x6C: PZ})
    for zero in (PZ, NZ):
        yield plane(f"gate bits {zero:08x} takes early return", 1, {0x3C: zero})
        yield plane(f"vector y bits {zero:08x} passes sign gate", 0, vector_y=zero)
        yield plane(f"margin bits {zero:08x} below boundary", 0, margin=zero)
    yield plane("negative vector y takes early return", 1, vector_y=f32(-1))
    yield plane("unordered config gate returns 1", 1, {0x3C: QNAN})
    yield plane("unordered vector y takes LT early return", 1, vector_y=QNAN)
    yield plane("unordered z returns 1", 1, point_z=QNAN)
    yield plane("unordered plane returns 1", 1, {0x6C: QNAN})
    yield plane("unordered margin returns 1", 1, margin=QNAN)



def controls():
    state = bytearray(0x144)
    struct.pack_into("<12f", state, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
    struct.pack_into("<2f", state, 0xe0, -5, 5)
    struct.pack_into("<12f", state, 0x104, 1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1, 0)
    struct.pack_into("<4f", state, 0x134, 6, 6, 6, 6)
    for px, py, pz, radius in itertools.product(
            (-7, -6, 0, 6, 7), (-7, 0, 6), (-6, -5, 0, 5, 6), (-1.0, 0.0, 1.0)):
        yield 0, bytes(state), (px, py, pz, 0.0), 0.0, radius
    for gate, tag, z, radius in itertools.product(
            (-1, 0, 1), (-1, 0, 1), (-8.0, -3.0, 0.0, 1.0), (0.0, 1.0, 5.0, 20.0)):
        state = bytearray(0x144)
        struct.pack_into("<f", state, 0x3c, gate)
        struct.pack_into("<f", state, 0x58, 1.0)
        struct.pack_into("<4f", state, 0x60, 0.75, -0.5, 1.25, 3.0)
        yield 1, bytes(state), (2.5, 1.5, z, radius), tag, 0.0
    rng = random.Random(0x146a660)
    for which in range(2):
        for _ in range(300):
            state = bytearray(0x144)
            for off in range(0, len(state), 4):
                struct.pack_into("<f", state, off, rng.uniform(-16, 16))
            point = tuple(rng.uniform(-10, 10) for _ in range(4))
            yield which, bytes(state), point, rng.uniform(-2, 2), rng.uniform(-2, 4)
    for entry, _, _, regs, memory in boundary_cases():
        mode = int(entry == 0x14e9d34)
        config = memory[regs[UC_ARM64_REG_X0]].ljust(0x144, b"\0")
        point = struct.unpack("<4f", memory[regs[UC_ARM64_REG_X1]].ljust(16, b"\0"))
        meta = struct.unpack("<2f", memory[regs[UC_ARM64_REG_X2]])[1] if mode else 0.0
        radius = struct.unpack("<f", struct.pack("<I", regs.get(UC_ARM64_REG_S0, 0)))[0]
        yield mode, config, point, meta, radius


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs="+", type=Path)
    parser.add_argument("--gameplay", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    options = parser.parse_args()
    if options.gameplay:
        compare_gameplay(*options.gameplay)
    cases = [(fields, None) for fields in controls()]
    live_counts = Counter()
    for path in options.live or ():
        for row in path.read_text(errors="replace").splitlines():
            if not row.startswith("CULL_ORIGINAL "):
                continue
            mode, w0, s0, *bits = map(int, row.split()[1:])
            if mode not in ENTRIES or len(bits) != 87:
                raise ValueError("Malformed culler capture")
            *point, gate, radius = struct.unpack("<6f", struct.pack("<6I", *bits[:6]))
            state = struct.pack("<81I", *bits[6:])
            cases.append(((mode, state, point, gate, radius), (w0, s0)))
            live_counts[mode] += 1
    ex = options.candidate.resolve()
    payload = []
    for (mode, state, point, gate, radius), _ in cases:
        context = struct.unpack("<81I", state)
        raw_args = struct.unpack("<6I", struct.pack("<6f", *point, gate, radius))
        payload.append(" ".join(str(x) for x in (mode, *raw_args, *context)))
    output = subprocess.run([str(ex)], input="\n".join(payload)+"\n", check=True,
                            text=True, capture_output=True).stdout.splitlines()
    assert len(output) == len(cases)
    if build_id(options.module) != BUILD:
        parser.error(f"expected module {BUILD}")
    module = nso.load(options.module)
    bounds = {frame.start: frame.size for frame in ehframe.parse(module)}
    originals = {}
    outcomes = {mode: set() for mode in ENTRIES}
    for mode, (entry, size) in ENTRIES.items():
        assert bounds[entry] == size
        originals[mode] = (entry, bytes(module.image[entry:entry+size]))
    for number, ((fields, captured), line) in enumerate(zip(cases, output)):
        mode, state, point, gate, radius = fields
        entry, code = originals[mode]
        registers = {UC_ARM64_REG_X0: CTX, UC_ARM64_REG_X1: POINT, UC_ARM64_REG_X2: META,
                UC_ARM64_REG_S0: struct.unpack("<I", struct.pack("<f", radius))[0]}
        memory = {CTX: state, POINT: struct.pack("<4f", *point), META: struct.pack("<2f", 0.0, gate)}
        a, b = (run(BASE+entry, code, registers, (UC_ARM64_REG_X0, UC_ARM64_REG_S0), memory) for _ in range(2))
        assert a == b and not a.calls and not a.writes and a.memory == memory
        expected = (a.registers[UC_ARM64_REG_X0], a.registers[UC_ARM64_REG_S0])
        actual = tuple(int(word) for word in line.split())
        if captured is not None and expected != captured:
            raise ValueError(f"Captured case {number} at {entry:#x}: AArch64 {expected}, gameplay {captured}")
        if expected != actual:
            raise ValueError(f"Case {number} at {entry:#x}: original {expected}, C++ {actual}")
        outcomes[mode].add(expected[0])
    assert all(values == {0, 1} for values in outcomes.values())
    print(f"{len(cases)} cases on both original predicates twice: both booleans, exact S0, no external writes")
    if options.live:
        if not live_counts:
            raise ValueError("No completed culler captures")
        print("Gameplay cases:", ", ".join(f"{ENTRIES[mode][0]:#x}={n}" for mode, n in sorted(live_counts.items())))


if __name__ == "__main__":
    main()
