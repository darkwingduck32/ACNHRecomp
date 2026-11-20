"""Replay the field-cell lookup with the stock bounds check and controlled provider bodies."""
import argparse
import collections
import hashlib
import itertools
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3, UC_ARM64_REG_X8,
)

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE, ENTRY, SIZE = 0x4000000000, 0x195033C, 0x10C
LAYER, POINT, ROOT, OUTPUT = 0x20000, 0x21000, 0x22000, 0x23000
OWNER_PTR, OWNER, NODE, PROVIDER = 0x24000, 0x25000, 0x26000, 0x27000

# mov w9,#0x5678; str w9,[x0,#4]; ret
SEED = struct.pack("<3I", 0x52800009 | (0x5678 << 5), 0xb9000409, 0xd65f03c0)

# Copy two provider words to X8 and the input X/Z into the resolved position.
PROVIDE = struct.pack(
    "<10I", 0xb9400009, 0xb9000109, 0xb9400409, 0xb9000509, 0xb40000a2,
    0xb9400829, 0xb9000849, 0xb9400c29, 0xb9000c49, 0xd65f03c0,
)


def fixtures():
    for layer, channel, node_present, channel_present, provider_present, resolved, (x, z) in itertools.product(
        (0, 1, 56, 57), (0, 3, 7, 8, 0xffffffff), (False, True), (False, True),
        (False, True), (False, True), ((-3, 6), (-2, -3), (4, 6), (0, 7)),
    ):
        owner = bytearray(0x200)
        layer_slot = 0x1f8 if layer == 0 else 0x30 + 8 * (layer if layer < 57 else 0)
        struct.pack_into("<Q", owner, layer_slot, NODE if node_present else 0)
        node = bytearray(0x160)
        struct.pack_into("<ii", node, 0x48, -2, -3)
        struct.pack_into("<ii", node, 0x58, 4, 6)
        slot = channel if channel < 8 else 0
        struct.pack_into("<Q", node, 0x60 + 8 * slot, 1 if channel_present else 0)
        struct.pack_into("<Q", node, 0xd0 + 16 * slot, PROVIDER if provider_present else 0)
        yield channel, ROOT if resolved else 0, {
            LAYER: struct.pack("<QI", 0, layer), POINT: struct.pack("<Qii", 0, x, z),
            ROOT: bytes(16), OUTPUT: bytes(8), OWNER: bytes(owner), NODE: bytes(node),
            BASE + 0x4ab27a8: struct.pack("<Q", OWNER_PTR),
            OWNER_PTR: struct.pack("<Q", OWNER),
            PROVIDER: struct.pack("<II", 0x13824679, 0xaabbccdd),
            BASE + 0x5486008: struct.pack("<II", 0x1010fffe, 0x11aa22bb),
        }


def live_cases(path, expected_route):
    raw = path.read_bytes()
    cases = collections.Counter()
    prefix = f"acnh shovel cell {expected_route} "
    for line in raw.decode(errors="replace").splitlines():
        if not line.startswith("acnh shovel cell "):
            continue
        if not line.startswith(prefix):
            raise ValueError(f"{path}: unexpected field-cell route")
        cases[line.removeprefix(prefix)] += 1
    if not cases:
        raise ValueError(f"{path}: no field-cell calls")
    print(f"{path}: {cases.total()} field-cell calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    ranges = {function.start: function.size for function in ehframe.parse(module)}
    if ranges.get(ENTRY) != SIZE or ranges.get(0x194f798) != 0x268:
        parser.error("unexpected function boundary")
    callees = {
        BASE + 0x195036c: (BASE + 0x194f798, bytes(module.image[0x194f798:0x194fa00])),
        BASE + 0x1950380: (BASE + 0x267d4ac, SEED),
        BASE + 0x1950410: (BASE + 0x11e4f58, PROVIDE),
    }
    if args.live and live_cases(args.live[0], "original") != live_cases(args.live[1], "cpp"):
        raise SystemExit("live field-cell inputs or outcomes differ")
    inputs = list(fixtures())
    lines = []
    for channel, root, memory in inputs:
        pairs = [(address + i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{BASE} {LAYER} {channel} {POINT} {root} {OUTPUT} {len(pairs)} " +
                     " ".join(f"{address} {value}" for address, value in pairs))
    actual = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                            capture_output=True, check=True, timeout=30).stdout.splitlines()
    if len(actual) != len(inputs):
        raise SystemExit("candidate returned the wrong number of cases")
    code = bytes(module.image[ENTRY:ENTRY+SIZE])
    provider_hits = 0
    for i, (channel, root, memory) in enumerate(inputs):
        regs = {
            UC_ARM64_REG_X0: LAYER, UC_ARM64_REG_X1: channel, UC_ARM64_REG_X2: POINT,
            UC_ARM64_REG_X3: root, UC_ARM64_REG_X8: OUTPUT,
        }
        first = run(BASE + ENTRY, code, regs, (), memory, callees=callees)
        if first != run(BASE + ENTRY, code, regs, (), memory, callees=callees):
            raise SystemExit(f"unstable original in case {i}")
        expected = [len(first.calls)]
        for _, target, x0 in first.calls:
            expected.extend((target - BASE, x0))
            provider_hits += target == BASE + 0x11e4f58
        expected.extend(value for write in first.writes for value in write)
        if list(map(int, actual[i].split())) != expected:
            raise SystemExit(f"case {i}: expected {expected[:16]}, got {actual[i]}")
    print(f"{len(inputs)} cases match two independent reference runs, including {provider_hits} provider calls")


if __name__ == "__main__":
    main()
