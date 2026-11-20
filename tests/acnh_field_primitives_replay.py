"""Replay action bits, half-tile lookup, vector multiply and tile category masks."""
import argparse
import collections
import hashlib
import itertools
import random
import re
import struct
import subprocess
from pathlib import Path

from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2

from recomp import ehframe, nso
from tests.acnh_field_record_replay import decode_reads
from tests.function_replay import run
from tools.acnh_version import build_id

BUILD = "FF1D1C05670DB6021C85B624A710B963"
BASE = 0x4000000000
ROUTES = ((0x24642A8, 0x68), (0x1281C54, 0x15C), (0x3DB4ED0, 0x4C), (0x849898, 0x118), (0x1285FE0, 0x1C4), (0x880870, 0x19C), (0x880A10, 0x19C), (0x127C6EC, 0x278), (0x1A03610, 0xEC), (0x267D6C4, 0xF0))


def inputs():
    for index, flag, bits in itertools.product((0, 1, 30, 31, 32, 53, 54, 55, 0xffffffff),
                                               (0, 1, 2, 3, 0xffffffff), (0, (1 << 64) - 1)):
        yield 0, 0x22000, index, flag, {0x22000: struct.pack("<Q", bits)}

    grid = bytearray(0xa8)
    struct.pack_into("<II", grid, 0x18, 4, 2)
    struct.pack_into("<iiI", grid, 0x28, -2, -1, 12)
    struct.pack_into("<Q", grid, 0x38, 0x24000)
    struct.pack_into("<B", grid, 0x88, 1)
    struct.pack_into("<I", grid, 0x8c, 8)
    struct.pack_into("<iiII", grid, 0x98, 2, 0, 2, 2)
    for x, z, slot in itertools.product(range(-5, 8), range(-3, 4), range(5)):
        records = bytearray(12 * 0x68)
        fallback = bytearray(0x68)
        if slot < 4:
            for offset in range(12):
                struct.pack_into("<hh", records, offset * 0x68 + 0x34 + slot * 16, x, z)
            struct.pack_into("<hh", fallback, 0x34 + slot * 16, x, z)
        yield 1, 0x23000, 0x25000, BASE, {
            0x23000: bytes(grid), 0x24000: bytes(records),
            0x25000: struct.pack("<Qii", 0, x, z), BASE + 0x50a50f0: bytes(fallback),
        }

    owner, node = 0x27000, 0x28000
    for layer, x, z, flag in itertools.product((0, 1, 56, 57, 0xffffffff), (-3, 0, 5), (-1, 1), (0, 1, 2, 3)):
        owner_bytes = bytearray(0x200)
        layer_address = 0x1f8 if layer == 0 else 0x30 + 8 * (layer if layer < 57 else 0)
        struct.pack_into("<Q", owner_bytes, layer_address, node)
        node_bytes = bytearray(0xb8)
        struct.pack_into("<Q", node_bytes, 0xb0, 0x23000)
        records = bytearray(12 * 0x68)
        fallback = bytearray(0x68)
        for offset in range(12):
            struct.pack_into("<B3xhh", records, offset * 0x68 + 0x40, flag, x, z)
        struct.pack_into("<B3xhh", fallback, 0x40, flag, x, z)
        yield 4, 0x25000, layer, BASE, {
            BASE + 0x509d040: struct.pack("<Q", owner), owner: bytes(owner_bytes),
            node: bytes(node_bytes), 0x23000: bytes(grid), 0x24000: bytes(records),
            0x25000: struct.pack("<Qii", 0, x, z), BASE + 0x50a50f0: bytes(fallback),
        }
    yield 4, 0x25000, 0, BASE, {BASE + 0x509d040: struct.pack("<Q", owner), owner: bytes(0x200)}
    owner_with_node = bytearray(0x200)
    struct.pack_into("<Q", owner_with_node, 0x1f8, node)
    yield 4, 0x25000, 0, BASE, {
        BASE + 0x509d040: struct.pack("<Q", owner), owner: bytes(owner_with_node), node: bytes(0xb8),
    }

    table, row, header, pointers = 0x28000, 0x29200, 0x29000, 0x2a000
    for mode, hash_value in ((5, 0x472724ed), (6, 0x0254bd05)):
        for ready, cached, value, index, present in itertools.product(
            (0, 1), (-2, -1, 28), (0, 2), (0, 1, 2, 0xffffffff), (False, True),
        ):
            owner = bytearray(0x1cc)
            struct.pack_into("<IQi", owner, 0x1b8, 1, 0, 0)
            struct.pack_into("<Qi", owner, 0x1c0, pointers, 2)
            data = bytearray(64)
            struct.pack_into("<I", data, 0, row - header)
            data[28] = value
            fields = bytearray(64)
            struct.pack_into("<HB", fields, 8, 1, 0)
            struct.pack_into("<II", fields, 0xc, hash_value if present else hash_value ^ 1, 28)
            yield mode, table, index, BASE, {
                table: bytes(owner), header: bytes(fields), row: bytes(data),
                pointers: struct.pack("<Q", row),
                BASE + 0x4bfab80: struct.pack("<B3x14i", ready, *([cached] * 14)),
            }

    for kind, auxiliary_values in itertools.product(
            range(18), ((0, 10, 11, 12), (10, 10, 11, 10), (0, 8, 15, 1), (0, 0, 0, 0))):
        yield 8, kind, 0, BASE, {BASE + 0x522a758: struct.pack("<4I", *auxiliary_values)}
    for item, tag, auxiliary_values in itertools.product(
            (0, 0x16a1, 0x3100, 0x6098, 0xffff), range(64),
            ((0, 10, 11, 12), (5, 1, 2, 3), (10, 10, 10, 10)),
    ):
        yield 9, 0x24000, 0, BASE, {
            0x24000: struct.pack("<HBB", item, 0xa5, tag | 0x80),
            BASE + 0x522a758: struct.pack("<4I", *auxiliary_values),
            BASE + 0x4077938: struct.pack("<17I", *range(0x100, 0x111)),
            BASE + 0x4001d00: struct.pack("<4I", *range(0x200, 0x204)),
        }

    rng = random.Random(303)
    defaults = BASE + 0x5486008
    for obj in (0x27000, 0x27001, defaults - 0x18, defaults - 0x41, defaults - 0xf0):
        for _ in range(8):
            data = rng.randbytes(0x178)
            memory = {obj: data}
            if not obj <= defaults < obj + len(data):
                memory[defaults] = rng.randbytes(8)
            yield 7, obj, 0, BASE, memory

    for alias, case in itertools.product((0, 1, 2, 3), range(64)):
        values = struct.pack("<9f", *(rng.uniform(-200, 200) for _ in range(9)))
        vector = struct.pack("<3f", *(rng.uniform(-200, 200) for _ in range(3)))
        matrix = 0x26000
        point = 0x26100 if alias != 3 else matrix
        out = (0x26200, point, matrix + 4, matrix)[alias]
        memory = {matrix: values}
        if point != matrix:
            memory[point] = vector
        if out == 0x26200:
            memory[out] = b"\xa5" * 12
        yield 2, out, matrix, point, memory


    for x, z, count, records, categories, mask in itertools.product(
        (-1, 0, 1, 2), (-1, 0, 1, 2), (0, 1, 4), (0, 0x2a000),
        ((0, 31, 32, 72), (73, 255, 255, 255)), (0, 1, 1 << 31, 1 << 32, 1 << 72, 1 << 73),
    ):
        owner, query, mask_address, grid = 0x27000, 0x28000, 0x29000, 0x2b000
        fields = bytearray(0x58)
        struct.pack_into("<II", fields, 0x38, 2, 2)
        struct.pack_into("<I", fields, 0x48, count)
        struct.pack_into("<Q", fields, 0x50, records)
        data = {
            owner + 0x80: struct.pack("<Q", grid), query: struct.pack("<Qii", 0, x, z),
            mask_address: mask.to_bytes(12, "little"), grid: bytes(fields),
        }
        if records:
            rows = (categories, (32, 73, 255, 255), (0, 72, 255, 255), (31, 73, 255, 255))
            data[records] = b"".join(bytes(0x30) + bytes(row) for row in rows)
        yield 3, owner, query, mask_address, data


HALF_TILE_TRACE = re.compile(
    r"acnh half tile (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"grid=([0-9a-f]+) query=([0-9a-f]+) record=([0-9a-f]+) out=([0-9a-f]+) "
    r"g=([0-9a-f]{336}) q=([0-9a-f]{32}) r=([0-9a-f]{208})"
)


def live_half_tiles(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    for route, caller, base, grid, query, record, out, g, q, r in HALF_TILE_TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        base, grid, query, record, out = map(lambda value: int(value, 16), (base, grid, query, record, out))
        grid_bytes, query_bytes, record_bytes = map(bytes.fromhex, (g, q, r))
        memory = {grid: grid_bytes, query: query_bytes, record: record_bytes}
        coords = struct.unpack_from("<ii", query_bytes, 8)
        slots = tuple(struct.unpack_from("<hh", record_bytes, 0x34 + 16*i) for i in range(4))
        states[(caller, coords, slots, out - record if out else None)] += 1
        cases.append(((1, grid, query, base, memory), out))
    if not cases:
        raise ValueError(f"{path}: no half-tile calls")
    print(f"{path}: {len(cases)} half-tile calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


CATEGORY_MASK_TRACE = re.compile(
    r"acnh tile category mask (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"owner=([0-9a-f]+) query=([0-9a-f]+) mask=([0-9a-f]+) grid=([0-9a-f]+) "
    r"record=([0-9a-f]+) out=([0-9a-f]+) g=([0-9a-f-]+) q=([0-9a-f]{16}) "
    r"r=([0-9a-f-]+) m=([0-9a-f-]+)"
)


def live_category_masks(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    for route, caller, _, owner, query, mask, grid, record, out, g, q, r, m in CATEGORY_MASK_TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        owner, query, mask, grid, record, out = (
            int(value, 16) for value in (owner, query, mask, grid, record, out)
        )
        memory = {query + 8: bytes.fromhex(q)}
        if grid:
            memory[owner + 0x80] = struct.pack("<Q", grid)
            memory[grid + 0x38] = bytes.fromhex(g)
        if record:
            memory[record + 0x30] = bytes.fromhex(r)
            memory[mask] = bytes.fromhex(m)
        states[(caller, q, r, m, out)] += 1
        cases.append(((3, owner, query, mask, memory), out))
    if not cases:
        raise ValueError(f"{path}: no tile category mask calls")
    print(f"{path}: {len(cases)} tile category mask calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


LAYER_FLAG_TRACE = re.compile(
    r"acnh half tile layer (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"query=([0-9a-f]+) layer=([0-9a-f]+) out=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def live_layer_flags(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    for route, caller, base, query, layer, out, fields in LAYER_FLAG_TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        base, query, layer, out = (int(value, 16) for value in (base, query, layer, out))
        reads, memory = decode_reads(fields)
        pointers = {0: 0, query: 1}
        normalized = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                           for _, width, value in reads)
        states[(caller, layer, out, normalized)] += 1
        cases.append(((4, query, layer, base, memory), out))
    if not cases:
        raise ValueError(f"{path}: no half-tile layer flag calls")
    callers = sorted({key[0] for key in states})
    print(f"{path}: {len(cases)} layer calls from {', '.join(callers)}, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


TERRAIN_TRACE = re.compile(
    r"acnh shovel terrain (original|cpp) entry=([0-9a-f]+) caller=([0-9a-f]+) "
    r"parent=([0-9a-f]+) base=([0-9a-f]+) table=([0-9a-f]+) index=([0-9a-f]+) "
    r"out=([0-9a-f]+) before=([0-9a-f]{120}) after=([0-9a-f]{120}) reads=([0-9a-f:,]+)"
)


def live_terrain(path, expected_route, required_entries, required_parent):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    reached = collections.Counter()
    for route, entry, caller, parent, base, table, index, out, before, after, fields in TERRAIN_TRACE.findall(raw.decode(errors="replace")):
        entry, caller, parent, base, table, index, out = (
            int(value, 16) for value in (entry, caller, parent, base, table, index, out)
        )
        if entry not in required_entries or (required_parent is not None and parent != required_parent):
            continue
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        reads, memory = decode_reads(fields)
        cache = base + 0x4bfab80
        memory[cache] = bytes.fromhex(before)
        pointers = {0: 0, table: 1}
        normalized = tuple((width, pointers.setdefault(value, len(pointers)) if width == 8 else value)
                           for _, width, value in reads)
        mode = 5 if entry == 0x880870 else 6
        states[(entry, caller, parent, index, out, before, after, normalized)] += 1
        cases.append(((mode, table, index, base, memory), (out, cache, bytes.fromhex(after))))
        reached[entry] += 1
    missing = required_entries - reached.keys()
    if missing:
        raise ValueError(f"{path}: missing terrain entries {', '.join(map(hex, sorted(missing)))}")
    print(f"{path}: terrain calls {dict(reached)}, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


SCRATCH_TRACE = re.compile(
    r"acnh shovel scratch (original|cpp) caller=([0-9a-f]+) base=([0-9a-f]+) "
    r"object=([0-9a-f]+) empty=([0-9a-f]{16}) before=([0-9a-f]{752}) after=([0-9a-f]{752}) out=([0-9a-f]+)"
)


def live_scratch(path, expected_route):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    callers = set()
    for route, caller, base, obj, empty, before, after, out in SCRATCH_TRACE.findall(raw.decode(errors="replace")):
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        base, obj, out = (int(value, 16) for value in (base, obj, out))
        before, after = bytes.fromhex(before), bytes.fromhex(after)
        memory = {obj: before, base + 0x5486008: bytes.fromhex(empty)}
        cases.append(((7, obj, 0, base, memory), (out, obj, after)))
        written = after[0x18:0x1c] + after[0x30:0x150] + after[0x160:0x168] + after[0x170:0x178]
        states[(caller, empty, written, out == obj)] += 1
        callers.add(caller)
    if callers != {"21f75a0", "21f75a8"}:
        raise ValueError(f"{path}: expected both shovel initializer callers, got {sorted(callers)}")
    print(f"{path}: {len(cases)} shovel scratch calls, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


ITEM_TRACE = re.compile(
    r"acnh shovel item (original|cpp) entry=([0-9a-f]+) caller=([0-9a-f]+) "
    r"base=([0-9a-f]+) arg=([0-9a-f]+) out=([0-9a-f]+) reads=([0-9a-f:,]+)"
)


def live_items(path, expected_route, required_entries):
    raw = path.read_bytes()
    cases = []
    states = collections.Counter()
    reached = collections.Counter()
    for route, entry, caller, base, arg, out, fields in ITEM_TRACE.findall(raw.decode(errors="replace")):
        entry, caller, base, arg, out = (int(value, 16) for value in (entry, caller, base, arg, out))
        if entry not in required_entries:
            continue
        if route != expected_route:
            raise ValueError(f"{path}: expected {expected_route}, found {route}")
        reads, memory = decode_reads(fields)
        mode = 8 if entry == 0x1a03610 else 9
        state = tuple((width, value) for _, width, value in reads)
        states[(entry, caller, arg if mode == 8 else 0, out, state)] += 1
        cases.append(((mode, arg, 0, base, memory), out))
        reached[entry] += 1
    missing = required_entries - reached.keys()
    if missing:
        raise ValueError(f"{path}: missing item entries {', '.join(map(hex, sorted(missing)))}")
    print(f"{path}: item calls {dict(reached)}, SHA-256 {hashlib.sha256(raw).hexdigest()}")
    return states, cases


def survey_outcomes(path, tile=None, kind="survey"):
    prefix = "SHOVEL_FIELD_RESULT " if kind == "final" else f"acnh shovel {kind} "
    if tile is not None:
        x, z = tile
        prefix += f"x={x} z={z} "
    outcomes = collections.Counter(line for line in path.read_text(errors="replace").splitlines()
                                   if line.startswith(prefix))
    if not outcomes:
        raise ValueError(f"{path}: no shovel {kind} snapshots")
    print(f"{path}: {outcomes.total()} shovel {kind} outcomes")
    return outcomes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--live-half-tile", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--live-category-mask", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--live-layer-flag", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--match-layer-caller", help="require matching layer snapshots at this caller; replay all snapshots")
    parser.add_argument("--live-survey", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--live-terrain", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--terrain-entry", action="append", type=lambda s: int(s, 16), choices=(0x880870, 0x880a10))
    parser.add_argument("--terrain-parent", type=lambda s: int(s, 16))
    parser.add_argument("--live-scratch", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--survey-tile", nargs=2, type=int, metavar=("X", "Z"))
    parser.add_argument("--scratch-sampled", action="store_true",
                        help="allow extra repeats of identical initialized scratch states")
    parser.add_argument("--live-item", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--item-entry", action="append", type=lambda s: int(s, 16), choices=(0x1a03610, 0x267d6c4))
    parser.add_argument("--live-final", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    parser.add_argument("--live-neighborhood", nargs=2, type=Path, metavar=("ORIGINAL", "CPP"))
    args = parser.parse_args()
    if build_id(args.module) != BUILD:
        parser.error(f"expected {BUILD}")
    module = nso.load(args.module)
    ranges = {f.start: f.size for f in ehframe.parse(module)}
    if any(ranges.get(entry) != size for entry, size in ROUTES):
        parser.error("unexpected function boundary")
    cases = [(case, None) for case in inputs()]
    if args.live_half_tile:
        original_states, original = live_half_tiles(args.live_half_tile[0], "original")
        replacement_states, replacement = live_half_tiles(args.live_half_tile[1], "cpp")
        if original_states != replacement_states:
            raise SystemExit("live callers, query coordinates, record slots or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    if args.live_category_mask:
        original_states, original = live_category_masks(args.live_category_mask[0], "original")
        replacement_states, replacement = live_category_masks(args.live_category_mask[1], "cpp")
        if original_states != replacement_states:
            raise SystemExit("live tile category mask callers, inputs or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    if args.live_survey and survey_outcomes(args.live_survey[0], args.survey_tile) !=             survey_outcomes(args.live_survey[1], args.survey_tile):
        raise SystemExit("shovel survey outcomes differ")
    if args.live_neighborhood and survey_outcomes(args.live_neighborhood[0], args.survey_tile, "neighborhood") != \
            survey_outcomes(args.live_neighborhood[1], args.survey_tile, "neighborhood"):
        raise SystemExit("shovel neighborhood records differ")
    if args.live_final and survey_outcomes(args.live_final[0], args.survey_tile, "final") != \
            survey_outcomes(args.live_final[1], args.survey_tile, "final"):
        raise SystemExit("final shovel flags, action codes or return values differ")
    if args.live_layer_flag:
        original_states, original = live_layer_flags(args.live_layer_flag[0], "original")
        replacement_states, replacement = live_layer_flags(args.live_layer_flag[1], "cpp")
        if args.match_layer_caller:
            caller = args.match_layer_caller.lower().removeprefix("0x")
            original_states = {key: n for key, n in original_states.items() if key[0] == caller}
            replacement_states = {key: n for key, n in replacement_states.items() if key[0] == caller}
            if not original_states or not replacement_states:
                raise SystemExit(f"both layer routes must reach caller {caller}")
        if original_states != replacement_states:
            raise SystemExit("live half-tile layer flag callers, inputs or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    if args.live_terrain:
        required = set(args.terrain_entry or (0x880870, 0x880a10))
        original_states, original = live_terrain(args.live_terrain[0], "original", required, args.terrain_parent)
        replacement_states, replacement = live_terrain(args.live_terrain[1], "cpp", required, args.terrain_parent)
        if original_states != replacement_states:
            raise SystemExit("live terrain callers, states or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    if args.live_scratch:
        original_states, original = live_scratch(args.live_scratch[0], "original")
        replacement_states, replacement = live_scratch(args.live_scratch[1], "cpp")
        matched = original_states.keys() == replacement_states.keys() if args.scratch_sampled             else original_states == replacement_states
        if not matched:
            raise SystemExit("shovel scratch callers or initialized contents differ")
        cases.extend(original)
        cases.extend(replacement)
    if args.live_item:
        required = set(args.item_entry or (0x1a03610, 0x267d6c4))
        original_states, original = live_items(args.live_item[0], "original", required)
        replacement_states, replacement = live_items(args.live_item[1], "cpp", required)
        if original_states != replacement_states:
            raise SystemExit("live item callers, inputs or outcomes differ")
        cases.extend(original)
        cases.extend(replacement)
    lines = []
    for (mode, a, b, c, memory), _ in cases:
        pairs = [(address+i, byte) for address, data in memory.items() for i, byte in enumerate(data)]
        lines.append(f"{mode} {a} {b} {c} {len(pairs)} " + " ".join(f"{x} {y}" for x, y in pairs))
    actual = subprocess.run([str(args.candidate.resolve())], input="\n".join(lines)+"\n",
                            capture_output=True, text=True, check=True, timeout=30).stdout.splitlines()
    if len(actual) != len(cases):
        raise SystemExit("candidate returned the wrong number of cases")
    counts, failures = collections.Counter(), {}
    item_dependency_calls = 0
    for i, ((mode, a, b, c, memory), observed) in enumerate(cases):
        entry, size = ROUTES[mode]
        code = bytes(module.image[entry:entry + size])
        regs = {UC_ARM64_REG_X0: a, UC_ARM64_REG_X1: b, UC_ARM64_REG_X2: c}
        base = c if mode in (1, 4, 5, 6, 7, 8, 9) else BASE
        address = base + entry
        callees = {base + 0x267d768: (base + 0x1a03610, bytes(module.image[0x1a03610:0x1a036fc]))} if mode == 9 else None
        want = run(address, code, regs, (UC_ARM64_REG_X0,), memory, callees=callees)
        if want != run(address, code, regs, (UC_ARM64_REG_X0,), memory, callees=callees):
            raise SystemExit(f"reference changed on repeat in case {i}")
        item_dependency_calls += len(want.calls)
        expected = (want.registers[UC_ARM64_REG_X0], *(v for event in want.writes for v in event))
        if observed is not None:
            if mode in (5, 6, 7):
                out, cache, after = observed
                if expected[0] != out or want.memory.get(cache) != after:
                    raise SystemExit(f"live case {i} differs from Unicorn")
            elif expected[0] != observed:
                raise SystemExit(f"live case {i} differs from Unicorn")
        if mode == 9:
            expected += tuple(x0 for _, _, x0 in want.calls)
        got = tuple(map(int, actual[i].split()))
        if got != expected:
            failures.setdefault(mode, f"entry {entry:#x}, case {i}: expected {expected[:13]}, got {got[:13]}")
        counts[mode] += 1
    if failures:
        raise SystemExit("\n".join(failures.values()))
    print(f"Original replay executed {item_dependency_calls} declared item helper calls")
    for mode, count in counts.items():
        print(f"entry {ROUTES[mode][0]:#x}: {count} cases match two reference runs")


if __name__ == "__main__":
    main()
