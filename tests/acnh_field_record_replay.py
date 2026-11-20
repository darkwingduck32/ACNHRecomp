"""Compare the field record lookup with independently replayed AArch64."""
import argparse
import hashlib
import itertools
import random
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1

from recomp import ehframe, nso
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ENTRY, INDEXED_ENTRY, SIZE = 0x8807CC, 0x240F090, 0xA0
DB, REGISTRY, HEADER, FLAGS, PREPARED = (0x20000, 0x21000, 0x22000, 0x23000, 0x24000)


def fixture(index, *, prepared=0, lookup_count=0, registry=True, header=True, flags=b"",
            entries=2, stride=48, columns=4, variant=0):
    db = bytearray(0x1C4)
    struct.pack_into("<i", db, 0x1C0, prepared)
    struct.pack_into("<I", db, 0x1B0, lookup_count)
    struct.pack_into("<Q", db, 0x1B8, PREPARED)
    struct.pack_into("<Q", db, 0xB0, REGISTRY if registry else 0)
    struct.pack_into("<I", db, 0xC8, len(flags))
    struct.pack_into("<Q", db, 0xD0, FLAGS)
    memory = {DB: bytes(db), REGISTRY: struct.pack("<QQ", 0, HEADER if header else 0),
              HEADER: struct.pack("<IIHB", entries, stride, columns, variant),
              PREPARED: struct.pack("<3Q", 0x30010, 0, 0x30040)}
    if flags:
        memory[FLAGS] = flags
    return DB, index, memory


def inputs():
    for index in (0, 1, 2, 3, 0xffffffff):
        for prepared, count in itertools.product((1, 2, 3), (0, 1, 3)):
            yield fixture(index, prepared=prepared, lookup_count=count)
        for registry, header in itertools.product((False, True), repeat=2):
            yield fixture(index, registry=registry, header=header)
        yield fixture(index, flags=b"\x01\x00\x02", entries=4)
        yield fixture(index, entries=4, stride=0)
        yield fixture(index, entries=4, columns=0)
    rng = random.Random(303)
    for _ in range(256):
        yield fixture(rng.choice((0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xffffffff)),
                      entries=rng.choice((0, 1, 4, 0xffffffff)),
                      stride=rng.getrandbits(32), columns=rng.randrange(0x10000),
                      variant=rng.randrange(0x100), flags=rng.randbytes(rng.randrange(4)))


TRACE = re.compile(
    r"acnh (field row|indexed record) (original|cpp) caller=([0-9a-f]+) parent=([0-9a-f]+) "
    r"db=([0-9a-f]+) index=([0-9a-f]+) out=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def decode_reads(fields):
    data = {}
    reads = []
    for field in fields.rstrip(",").split(","):
        address_s, width_s, value_s = field.split(":")
        address, width, value = int(address_s, 16), int(width_s), int(value_s, 16)
        if width not in (1, 2, 4, 8):
            raise ValueError(f"invalid snapshot width {width}")
        reads.append((address, width, value))
        for i, byte in enumerate(value.to_bytes(width, "little")):
            if data.setdefault(address + i, byte) != byte:
                raise ValueError(f"snapshot changed during capture at {address + i:#x}")

    blocks = {}
    start = None
    for address, byte in sorted(data.items()):
        if start is None or start + len(blocks[start]) != address:
            start = address
            blocks[start] = bytearray()
        blocks[start].append(byte)
    return reads, {address: bytes(block) for address, block in blocks.items()}


def shape(db, index, output, caller, parent, reads):
    # The scenarios allocate at different addresses. Compare pointer identities
    # and returned record offsets, then replay each exact address map below.
    pointers = {0: 0, db: 1}
    def pointer_id(address):
        return pointers.setdefault(address, len(pointers))

    fields = tuple((width, pointer_id(value) if width == 8 else value)
                   for _, width, value in reads)
    if not output:
        result = ("null", 0)
    elif 0 < reads[0][2] < 0x80000000:
        result = ("prepared", pointer_id(output))
    else:
        result = ("packed", output - reads[2][2])
    return caller, parent, index, fields, result


def live_cases(path, expected_route, label="field row"):
    raw = path.read_bytes()
    signatures = set()
    captures = []
    for match in TRACE.finditer(raw.decode("utf-8", errors="replace")):
        kind, route, caller, parent, db, index, output, fields = match.groups()
        if kind != label:
            continue
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, saw {route}")
        db, index, output = int(db, 16), int(index, 16), int(output, 16)
        reads, memory = decode_reads(fields)
        signatures.add(shape(db, index, output, int(caller, 16), int(parent, 16), reads))
        captures.append((db, index, memory, output))
    if not captures:
        raise ValueError(f"{path}: no {expected_route} field record calls")
    print(f"{path}: {len(captures)} calls, {len(signatures)} normalized states; "
          f"SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return signatures, captures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live", nargs=2, metavar=("ORIGINAL_LOG", "CPP_LOG"), type=Path)
    parser.add_argument("--indexed", action="store_true", help="replay the second indexed record entry")
    args = parser.parse_args()
    entry = INDEXED_ENTRY if args.indexed else ENTRY
    label = "indexed record" if args.indexed else "field row"
    if build_id(args.module) != BUILD:
        parser.error(f"expected module build {BUILD}")
    module = nso.load(args.module)
    if not any(f.start == entry and f.size == SIZE for f in ehframe.parse(module)):
        parser.error("function boundary is absent from the module's unwind table")
    code = bytes(module.image[entry:entry + SIZE])
    cases = [(*case, None) for case in inputs()]
    if args.live:
        original_shapes, original = live_cases(args.live[0], "original", label)
        replacement_shapes, replacement = live_cases(args.live[1], "cpp", label)
        if original_shapes != replacement_shapes:
            raise SystemExit("original and C++ traces have different normalized states or outcomes")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for db, index, memory, _ in cases:
        values = [(address + offset, byte) for address, block in memory.items()
                  for offset, byte in enumerate(block)]
        lines.append(f"{db} {index} {len(values)} " + " ".join(f"{a} {b}" for a, b in values))
    candidate = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines) + "\n", text=True,
                               capture_output=True, check=True, timeout=30)
    actual = candidate.stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit(f"candidate returned {len(actual)} results for {len(cases)} cases")
    for i, (db, index, memory, observed) in enumerate(cases):
        registers = {UC_ARM64_REG_X0: db, UC_ARM64_REG_X1: index}
        original = run(BASE + entry, code, registers, (UC_ARM64_REG_X0,), memory)
        repeat = run(BASE + entry, code, registers, (UC_ARM64_REG_X0,), memory)
        if original != repeat or original.memory != memory or original.written_bytes:
            raise SystemExit(f"unexpected reference effects in case {i}, index {index}")
        expected, got = original.registers[UC_ARM64_REG_X0], int(actual[i])
        if observed is not None and observed != expected:
            raise SystemExit(f"live case {i}: observed {observed:#x}, expected {expected:#x}")
        if got != expected:
            raise SystemExit(f"case {i}, index {index}: expected {expected:#x}, got {got:#x}")
    print(f"{len(cases)} comparisons passed twice")


if __name__ == "__main__":
    main()
