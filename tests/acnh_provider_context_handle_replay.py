"""Replay release of a provider handle, including guest exclusive and shared paths."""
import argparse
from collections import Counter
import itertools
from pathlib import Path
import re
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.acnh_provider_cell_replay import address_words, words, RETURN
from tests.function_replay import run
from tools.acnh_version import build_id


BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x37568c0, 0xfc
HANDLE, SLOT, MANAGER, THREAD, TABLE = (i * 0x1000 for i in range(32, 37))


def fixture(match, count, exclusive, holders, waiters):
    handle = bytearray(0xfc)
    struct.pack_into("<I4xQ", handle, 8, 0 if match == "empty" else 2, TABLE)
    struct.pack_into("<I", handle, 0x58, holders)
    struct.pack_into("<I", handle, 0xf8, waiters)
    records = b"".join(struct.pack("<QB3xi", THREAD if match == index else 0x90000,
                                   exclusive, count) for index in range(2))
    memory = {
        BASE + 0x4ab3350: struct.pack("<Q", SLOT),
        SLOT: struct.pack("<Q", MANAGER),
        MANAGER: bytes(0x68) + struct.pack("<I", 0xabcdef12),
        HANDLE: bytes(handle),
        TABLE: records,
    }
    calls = {
        BASE + 0x37568e4: (BASE + 0x3f033a0, words(*address_words(0, THREAD), RETURN)),
        BASE + 0x375698c: (BASE + 0x3f05830, words(RETURN)),
        BASE + 0x3756994: (BASE + 0x37e3040, words(RETURN)),
    }
    return memory, calls


def acquire_fixture(keys, mode, replace, change_thread):
    memory, _ = fixture("miss", 0, 0, 0, 0)
    memory[TABLE] = b"".join(struct.pack("<Q8x", key) for key in keys) or bytes(16)
    handle = bytearray(memory[HANDLE])
    struct.pack_into("<I", handle, 8, len(keys))
    memory[HANDLE] = bytes(handle)
    next_table = TABLE + 0x100
    memory[next_table] = struct.pack("<Q8xQ8x", 0x90000, 0)
    update = (*address_words(9, HANDLE + 0x10), *address_words(10, next_table), 0xf900012a) if replace else ()
    next_manager, first_thread = 0x26000, 0x27000
    thread_lookup = words(*address_words(0, THREAD), RETURN)
    if change_thread:
        memory[MANAGER] = bytes(0x68) + struct.pack("<I", 1)
        memory[next_manager] = bytes(0x68) + struct.pack("<I", 2)
        first = (*address_words(9, SLOT), *address_words(10, next_manager), 0xf900012a,
                 *address_words(0, first_thread), RETURN)
        thread_lookup = words(0x7100041f, 0x54000001 | ((len(first) + 1) << 5),
                              *first, *address_words(0, THREAD), RETURN)
    calls = {
        BASE + 0x37567f8: (BASE + 0x3f033a0, thread_lookup),
        BASE + 0x3756808: (BASE + 0x3f033a0, thread_lookup),
        BASE + 0x3756848: (BASE + 0x3f03880, words(*update, RETURN)),
        BASE + 0x37568a8: (BASE + 0x3f038c0, words(RETURN)),
    }
    return memory, calls, mode, next_table if replace else 0, next_manager if change_thread else 0, first_thread


def prepare_fixture(primary, ticket, next_group):
    counters = bytearray(0xa4)
    struct.pack_into("<I", counters, 0, primary)
    struct.pack_into("<I", counters, 0x50, ticket)
    struct.pack_into("<I", counters, 0xa0, next_group)
    return {HANDLE: bytes(counters)}, {
        BASE + 0x37e2eac: (BASE + 0x3f05820, words(RETURN)),
        BASE + 0x37e2ee8: (BASE + 0x3f05820, words(RETURN)),
        BASE + 0x37e2f20: (BASE + 0x3f05830, words(RETURN)),
    }


def live(path, entry, expected):
    pattern = (re.compile(r"acnh shovel handle prepare (original|cpp) (caller=[^\r\n]+)")
               if entry == 0x37e2e80 else
               re.compile(rf"acnh shovel handle (original|cpp) entry={entry:x} (state=[^\r\n]+)"))
    states = Counter()
    for route, state in pattern.findall(path.read_text(errors="replace")):
        if route != expected:
            raise ValueError(f"{path}: expected {expected}, found {route}")
        states[state] += 1
    if not states:
        raise ValueError(f"{path}: missing provider handle call {entry:#x}")
    print(f"{path}: {states.total()} calls to {entry:#x}, {dict(states)}")
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    route = parser.add_mutually_exclusive_group()
    route.add_argument("--acquire", action="store_true")
    route.add_argument("--prepare", action="store_true")
    parser.add_argument("--live", nargs=2, type=Path)
    args = parser.parse_args()
    module = nso.load(args.module)
    operation = 2 if args.prepare else int(args.acquire)
    entry, size = ((ENTRY, SIZE), (0x37567d0, 0xf0), (0x37e2e80, 0xa4))[operation]
    if build_id(args.module) != BUILD or not any(f.start == entry and f.size == size for f in ehframe.parse(module)):
        parser.error("unexpected build or function boundary")

    if args.live and live(args.live[0], entry, "original") != live(args.live[1], entry, "cpp"):
        raise SystemExit("live provider handle states or caller counts differ")

    cases, lines = [], []
    if args.prepare:
        cases.extend((*prepare_fixture(*values), 0, 0, 0, 0) for values in itertools.product(
            (0, 1, 2, 0xffffffff), repeat=3))
    elif args.acquire:
        keys = [(first, second) for first, second in itertools.product((0, THREAD, 0x90000), repeat=2)]
        keys.append(())
        cases.extend(acquire_fixture(row, mode, replace, change_thread)
                     for row, mode, replace, change_thread in itertools.product(
                         keys, (0, 1, 2, 0xffffffff), (False, True), (False, True)))
    else:
        for match, count, exclusive, holders, waiters in itertools.product(
                (0, 1, "miss", "empty"), (-0x80000000, -1, 0, 1, 2), (False, True), (0, 1, 2), (0, 1, 2)):
            cases.append((*fixture(match, count, exclusive, holders, waiters), 0, 0, 0, 0))
    for memory, _, mode, replacement, manager, first_thread in cases:
        pairs = [(address + i, value) for address, data in memory.items() for i, value in enumerate(data)]
        lines.append(f"{BASE} {HANDLE} {THREAD} {operation} {mode} {replacement} {SLOT} {manager} {first_thread} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    native = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n",
                            text=True, capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(native) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[entry:entry + size])
    for index, ((memory, calls, mode, _, _, _), row) in enumerate(zip(cases, native)):
        regs = {UC_ARM64_REG_X0: HANDLE, UC_ARM64_REG_X1: mode}
        outputs = (UC_ARM64_REG_X0,) if args.acquire else ()
        original = run(BASE + entry, code, regs, outputs, memory, callees=calls)
        if original != run(BASE + entry, code, regs, outputs, memory, callees=calls):
            raise SystemExit(f"unstable reference case {index}")
        expected = [original.registers[UC_ARM64_REG_X0]] if args.acquire else []
        expected.append(len(original.calls))
        for site, target, receiver in original.calls:
            expected.extend((site - BASE, target - BASE, receiver))
        expected.extend(field for write in original.writes for field in write)
        if [int(field) for field in row.split()] != expected:
            raise SystemExit(f"context handle case {index}: expected {expected}, got {row}")
    print(f"{len(cases)} cases matched both AArch64 runs")


if __name__ == "__main__":
    main()
