"""Replay the field layer bounds predicate using the original 3.0.3 body."""
import argparse
import collections
import hashlib
import itertools
import re
from pathlib import Path
import struct
import subprocess

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.acnh_field_record_replay import decode_reads
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x194F798, 0x268
LAYER, POSITION, OWNER_PTR, OWNER, NODE = 0x20000, 0x21000, 0x22000, 0x23000, 0x24000


def fixtures():
    for layer, channel, present, channel_present, x, z in itertools.product(
            (0, 1, 56, 57, 0xffffffff), (0, 2, 7, 8, 0xffffffff), (False, True), (False, True),
            (-3, -2, 4, 5), (-4, -3, 6, 7)):
        owner = bytearray(0x200)
        offset = 0x1f8 if layer == 0 else 0x30 + 8 * (layer if layer < 57 else 0)
        struct.pack_into("<Q", owner, offset, NODE if present else 0)
        node = bytearray(0xa0)
        struct.pack_into("<ii", node, 0x48, -2, -3)
        struct.pack_into("<ii", node, 0x58, 4, 6)
        offset = 0x60 + 8 * (channel if channel < 8 else 0)
        struct.pack_into("<Q", node, offset, 0x25000 if channel_present else 0)
        yield channel, {
            BASE + 0x4ab27a8: struct.pack("<Q", OWNER_PTR),
            OWNER_PTR: struct.pack("<Q", OWNER),
            OWNER: bytes(owner), NODE: bytes(node),
            LAYER: struct.pack("<QI", 0, layer),
            POSITION: struct.pack("<Qii", 0, x, z),
        }


    # The stock predicate does not read Z after X fails.
    yield 0, {
        BASE + 0x4ab27a8: struct.pack("<Q", OWNER_PTR),
        OWNER_PTR: struct.pack("<Q", OWNER),
        OWNER + 0x1f8: struct.pack("<Q", NODE),
        NODE + 0x48: struct.pack("<i", 0),
        NODE + 0x60: struct.pack("<Q", 0x25000),
        LAYER + 8: struct.pack("<I", 0),
        POSITION + 8: struct.pack("<i", -1),
    }


TRACE = re.compile(
    r"acnh shovel bounds (original|cpp) caller=([0-9a-f]+) parent=([0-9a-f]+) "
    r"base=([0-9a-f]+) layer=([0-9a-f]+) channel=([0-9a-f]+) position=([0-9a-f]+) "
    r"out=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def live_cases(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    for route, caller, parent, base, layer, channel, position, out, fields in TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        base, layer, channel, position, out = (int(value, 16) for value in (base, layer, channel, position, out))
        reads, memory = decode_reads(fields)
        pointers = {0: 0, layer: 1, position: 2}
        coordinates = tuple(value for address, _, value in sorted(reads) if address in (position + 8, position + 12))
        normalized = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                           for address, width, value in reads if address not in (position + 8, position + 12))
        states[(caller, parent, channel, out, coordinates, normalized)] += 1
        cases.append((base, layer, channel, position, memory, out))
    if not cases:
        raise ValueError(f"{path}: no shovel bounds calls")
    print(f"{path}: {len(cases)} calls across {len(states)} states, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected module build {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == ENTRY and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("unexpected function boundary")
    cases = [(BASE, LAYER, channel, POSITION, memory, None) for channel, memory in fixtures()]
    if args.live:
        original_states, original = live_cases(args.live[0], "original")
        replacement_states, replacement = live_cases(args.live[1], "cpp")
        if original_states != replacement_states:
            raise SystemExit("live bounds callers, inputs or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for base, layer, channel, position, memory, _ in cases:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{base} {layer} {channel} {position} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    result = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                            capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(result) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    for i, (base, layer, channel, position, memory, observed) in enumerate(cases):
        regs = {UC_ARM64_REG_X0: layer, UC_ARM64_REG_X1: channel, UC_ARM64_REG_X2: position}
        first = run(base + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory)
        if first != run(base + ENTRY, code, regs, (UC_ARM64_REG_X0,), memory) or first.writes:
            raise SystemExit(f"unstable original effects in case {i}")
        expected = first.registers[UC_ARM64_REG_X0]
        if (observed is not None and observed != expected) or int(result[i]) != expected:
            raise SystemExit(f"case {i}: expected {expected}, got {result[i]}")
    print(f"{len(cases)} cases match two independent reference runs")


if __name__ == "__main__":
    main()
