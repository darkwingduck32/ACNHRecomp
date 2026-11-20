"""Replay eight observed field action state helpers with explicit AArch64 fixtures."""
import argparse
from collections import Counter
from itertools import product
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_S0

from recomp import ehframe, nso
from tests.acnh_field_action_state_live import by_input, read
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ACTOR, TIMER, SETTINGS, GROUP, ENABLED, SELECTED, CYCLE = (
    0x4000000000, 0x21000, 0x23000, 0x25000, 0x27000, 0x29000, 0x2b000, 0x2d000,
)
ENTRIES = (
    (0x21e094, 0x64), (0x21e0f8, 0x84), (0x21e17c, 0x84), (0x21e208, 0x5c),
    (0x21e264, 0x78), (0x21e2dc, 0xdc), (0x21e3b8, 0x30), (0x21e3f4, 0x3c),
)


def advance():
    for event, flags, current, size in product((0, 1, 2, 3), (0, 1, 0x8000),
                                               (-2147483648, -2, -1, 0, 1, 2147483647), (-1, 0, 1, 3)):
        data = bytearray(0x4d8)
        struct.pack_into("<H", data, 9, flags)
        struct.pack_into("<i", data, 0x30, current)
        struct.pack_into("<Q", data, 0x4d0, SELECTED)
        yield 0, event, {ACTOR: bytes(data), SELECTED: struct.pack("<Q", CYCLE), CYCLE: struct.pack("<i", size)}


def slots(mode):
    for pattern in product(range(4), repeat=3):
        group = bytearray(0x368)
        memory = {ACTOR + 0x240: struct.pack("<Q", GROUP)}
        for index, selector in enumerate(pattern):
            if not selector:
                continue
            record = 0x2f000 + 0x1000 * index
            struct.pack_into("<Q", group, 0x350 + 8 * index, record)
            data = bytearray(b"\xff" * 0x52)
            if selector > 1:
                data[(0x50 if selector == 2 else 0x13) + mode - 1] = 9
            memory[record] = bytes(data)
        memory[GROUP] = bytes(group)
        yield mode, 0, memory


def ready():
    for cursor, amount, active, force, mode in product(
        (-1.25, -0., 0., .25, 1., 25., float("inf"), float("nan")),
        (-1, 0, 2), (0, 1), (0, 1), (-1, 0, 1),
    ):
        actor = bytearray(0xa8)
        struct.pack_into("<i", actor, 0x2c, mode)
        struct.pack_into("<f", actor, 0x44, cursor)
        struct.pack_into("<f", actor, 0x64, .25)
        struct.pack_into("<Q", actor, 0x78, TIMER)
        struct.pack_into("<Q", actor, 0xa0, SETTINGS)
        timer = bytearray(0x27c)
        timer[0x1e] = active
        struct.pack_into("<ii", timer, 0x270, amount, 0)
        struct.pack_into("<i", timer, 0x278, -1)
        yield 3, 0, {ACTOR: bytes(actor), TIMER: bytes(timer), SETTINGS + 0x792: bytes([force])}


def curve(mode):
    coeffs = (-1., -0., 0., .25, .99, 1., 2., float("nan"))
    for left, right, gate, pattern in product(coeffs, coeffs, (0, 1, 2, 3), range(4)):
        actor = bytearray(0x248)
        struct.pack_into("<ff", actor, 0x6c, left, right)
        struct.pack_into("<Q", actor, 0x240, GROUP)
        struct.pack_into("<Q", actor, 0xa0, SETTINGS)
        settings = bytearray(0x820)
        struct.pack_into("<ff", settings, 0x818, .37, .62)
        settings[0x799], settings[0x79b] = pattern, 3 - pattern
        enable = bytearray(0x79d)
        for offset in (0x799, 0x79a):
            enable[offset] = gate & 1
        for offset in (0x79b, 0x79c):
            enable[offset] = gate >> 1
        yield mode, 0, {ACTOR: bytes(actor), GROUP + 0x10: struct.pack("<Q", ENABLED),
                        SETTINGS: bytes(settings), ENABLED: bytes(enable)}


def position():
    for mode, pointer, offset in product(range(8), (0, 0x20000, 0xfffffffe00002000), (0, 3, 0x4fffffff7)):
        yield 6, 0, {ACTOR + 0x240: struct.pack("<Q", GROUP), ACTOR + 0x788: struct.pack("<Q", pointer),
                     ACTOR + 0x4a8: struct.pack("<Q", ENABLED), GROUP + 0x340: struct.pack("<I", mode),
                     ENABLED + 0x18: struct.pack("<Q", offset)}


def start():
    for flag, counter in product((*range(32), 0x7f, 0xff), (0, 1, 0xffffffff)):
        yield 7, 0, {ACTOR + 0xa0: struct.pack("<Q", SETTINGS), SETTINGS + 0x880: bytes([flag]),
                     SETTINGS + 0x8bc: struct.pack("<I", counter)}


def cases():
    yield from advance()
    yield from slots(1)
    yield from slots(2)
    yield from ready()
    yield from curve(4)
    yield from curve(5)
    yield from position()
    yield from start()


def captured(captures):
    seen = {}
    for route, path in captures:
        if route not in {"original", "cpp"}:
            raise ValueError(f"unknown route {route}")
        for key, value in by_input(read(Path(path), route, True)).items():
            if seen.setdefault(key, value) != value:
                raise ValueError(f"captured effects differ for {key}")
    for (entry, _, event, inputs), (writes, result) in seen.items():
        if entry == ENTRIES[0][0]:
            flags, index, size = (int(word, 16) for word in inputs.split(":"))
            actor = bytearray(0x4d8)
            struct.pack_into("<H", actor, 9, flags)
            struct.pack_into("<I", actor, 0x30, index)
            struct.pack_into("<Q", actor, 0x4d0, SELECTED)
            memory = {ACTOR: bytes(actor), SELECTED: struct.pack("<Q", CYCLE), CYCLE: struct.pack("<I", size)}
            measured = [result]
            for field in filter(None, writes.split(",")):
                address, width, value = (int(word, 16) for word in field.split(":"))
                measured.extend((ACTOR + address, width, (ACTOR + value) % (1 << 64) if width == 8 else value))
            yield (0, event, memory), tuple(measured)
        else:
            group = bytearray(0x368)
            memory = {ACTOR + 0x240: struct.pack("<Q", GROUP)}
            for index, raw in enumerate(inputs.split(":")):
                if raw == "-":
                    continue
                record = 0x2f000 + index * 0x1000
                struct.pack_into("<Q", group, 0x350 + 8 * index, record)
                data = bytearray(0x52)
                data[0x51], data[0x14] = bytes.fromhex(raw)
                memory[record] = bytes(data)
            memory[GROUP] = bytes(group)
            if writes:
                raise ValueError("the slot predicate unexpectedly recorded writes")
            yield (2, event, memory), (result,)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--capture", nargs=2, action="append", default=[], metavar=("ROUTE", "LOG"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    sizes = {fn.start: fn.size for fn in ehframe.parse(module)}
    if any(sizes.get(entry) != size for entry, size in ENTRIES):
        parser.error("unexpected field action entry ranges")
    payload, expected, counts = [], [], Counter()
    fixtures = [(case, None) for case in cases()]
    measured = list(captured(args.capture))
    fixtures.extend(measured)
    for (mode, flags, memory), recorded in fixtures:
        entry, size = ENTRIES[mode]
        body = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: ACTOR, UC_ARM64_REG_X1: flags}
        result_reg = UC_ARM64_REG_S0 if mode in (4, 5) else UC_ARM64_REG_X0
        first = run(BASE + entry, body, regs, (result_reg,), memory)
        if first != run(BASE + entry, body, regs, (result_reg,), memory):
            raise ValueError(f"{entry:#x}: unstable original")
        result = (first.registers[result_reg], *(v for write in first.writes for v in write))
        if recorded is not None and recorded != result:
            raise ValueError(f"{entry:#x}: original replay differed from captured state")
        expected.append(result)
        values = [mode, ACTOR, flags, sum(map(len, memory.values()))]
        values.extend(v for address, data in memory.items() for i, b in enumerate(data) for v in (address + i, b))
        payload.append(" ".join(map(str, values)))
        counts[entry] += 1
    output = subprocess.run([str(args.candidate.resolve())], input="\n".join(payload) + "\n",
                            text=True, capture_output=True, check=True, timeout=45)
    actual = [tuple(map(int, row.split())) for row in output.stdout.splitlines()]
    if len(actual) != len(expected):
        raise SystemExit(f"Expected {len(expected)} cases, got {len(actual)}")
    for index, (before, after) in enumerate(zip(expected, actual, strict=True)):
        if before != after:
            raise SystemExit(f"Case {index} mode {payload[index].split(maxsplit=1)[0]}: original {before}, C++ {after}")
    print("Matched original returns and ordered writes twice: " +
          ", ".join(f"{entry:#x}={counts[entry]}" for entry, _ in ENTRIES))
    if measured:
        print(f"Replayed {len(measured)} distinct gameplay input states")


if __name__ == "__main__":
    main()
