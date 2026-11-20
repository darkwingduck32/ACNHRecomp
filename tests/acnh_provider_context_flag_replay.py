"""Replay context-flag selection with controlled thread-resolution calls."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x2601110, 0x160
OWNER, SLOT, MANAGER, THREAD, NAME, VTABLE, NEXT_VTABLE = (i * 0x1000 for i in range(32, 39))
LABELS = (0x3fa5738, 0x3f9b10c)
VIRTUAL_CALLS = (0x2601170, 0x2601180, 0x26011f4, 0x2601204)
TRACE = re.compile(r"acnh shovel context flag (original|cpp) (caller=[^\r\n]+)")


def qword(value):
    return struct.pack("<Q", value)


def fixture(index, matches_manager, clear_slot, change_vtable):
    first, second = b"alpha queue\0", b"bravo queue\0"
    names = (
        (BASE + LABELS[0], first), (BASE + LABELS[1], second), (NAME, first), (NAME, second),
        (NAME, b"alpha\0"), (NAME, b"bravo queued\0"), (NAME, b"\0"),
        (NAME, b"z" * 512 + b"\0"),
    )
    address, name = names[index]
    virtual, next_virtual = 0x3fa6000, 0x3fa7000
    manager = bytearray(0x6c)
    struct.pack_into("<QI", manager, 0x60, THREAD if matches_manager else OWNER, 0xfedcba98)
    memory = {
        BASE + 0x4ab3350: qword(SLOT),
        SLOT: qword(MANAGER),
        MANAGER: bytes(manager),
        THREAD: bytes(0x20) + qword(VTABLE) + qword(address),
        VTABLE: bytes(0x18) + qword(BASE + virtual),
        NEXT_VTABLE: bytes(0x18) + qword(BASE + next_virtual),
        BASE + LABELS[0]: first,
        BASE + LABELS[1]: second,
    }
    if address == NAME:
        memory[NAME] = name
    thread_body = (*address_words(9, SLOT), 0xf900013f) if clear_slot else ()
    first_refresh = words(*address_words(9, NEXT_VTABLE), 0xf9000009, RETURN) if change_vtable else words(RETURN)
    calls = {
        BASE + 0x2601138: (BASE + 0x3f033a0, words(*thread_body, *address_words(0, THREAD), RETURN)),
        BASE + VIRTUAL_CALLS[0]: (BASE + virtual, first_refresh),
    }
    for site in VIRTUAL_CALLS[1:]:
        calls[BASE + site] = (BASE + next_virtual, words(RETURN)) if change_vtable else (BASE + virtual, words(RETURN))
    return memory, calls


def live(path, expected):
    states = Counter()
    for route, state in TRACE.findall(path.read_text(errors="replace")):
        if route != expected:
            raise ValueError(f"{path}: expected {expected}, found {route}")
        states[state] += 1
    if not states:
        raise ValueError(f"{path}: missing context-flag lookups")
    print(f"{path}: {states.total()} lookups, {dict(states)}")
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path)
    args = parser.parse_args()
    module = nso.load(args.module)
    if build_id(args.module) != BUILD or not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected build or function boundary")
    if args.live and live(args.live[0], "original") != live(args.live[1], "cpp"):
        raise SystemExit("live flag results or caller counts differ")

    cases = []
    lines = []
    for index, matched, clear_slot, change_vtable in itertools.product(range(8), (False, True), (False, True), (False, True)):
        memory, calls = fixture(index, matched, clear_slot, change_vtable)
        cases.append((memory, calls))
        pairs = [(address + i, value) for address, data in memory.items() for i, value in enumerate(data)]
        lines.append(f"{BASE} {OWNER} {THREAD} {SLOT} {NEXT_VTABLE} {int(clear_slot)} {int(change_vtable)} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY + SIZE])
    for index, ((memory, calls), row) in enumerate(zip(cases, native)):
        regs, outputs = {UC_ARM64_REG_X0: OWNER}, (UC_ARM64_REG_X0,)
        original = run(BASE + ENTRY, code, regs, outputs, memory, callees=calls)
        if original != run(BASE + ENTRY, code, regs, outputs, memory, callees=calls):
            raise SystemExit(f"unstable reference case {index}")
        expected = [original.registers[UC_ARM64_REG_X0], len(original.calls)]
        for site, target, receiver in original.calls:
            expected.extend((site - BASE, target - BASE, receiver))
        expected.extend(field for write in original.writes for field in write)
        if [int(field) for field in row.split()] != expected:
            raise SystemExit(f"context flag case {index}: expected {expected}, got {row}")
    print(f"{len(cases)} cases matched both AArch64 runs")


if __name__ == "__main__":
    main()
