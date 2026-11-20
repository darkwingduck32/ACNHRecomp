"""Portable alternate terrain rectangles and child responses."""
import itertools
import struct

BASE, ENTRY, STACK = 0x4000000000, 0x87bf30, 0x6500001000
QUERY, FIELD_SLOT, FIELD, TERRAIN_SLOT = 0x21000, 0x22000, 0x23000, 0x24000
TERRAIN, GRID, FLAGS, RECORD = 0x25000, 0x26000, 0x27000, 0x28000
TABLE_SLOT, TABLE, TYPE = 0x29000, 0x2a000, 0x2b000
REJECTION = (0x87c0bc, 0x87c0fc, 0x87c13c, 0x87c17c)
REQUIREMENT = (0x87c0ec, 0x87c12c, 0x87c16c, 0x87c1ac)


def fixture(capture, scenario, selector, rectangle, stop, corner):
    cap = capture(ENTRY, BASE, STACK, (QUERY, 0, 0, 0, 0, 0))

    def put(address, value, width=8):
        cap.add(address, (value % (1 << (width * 8))).to_bytes(width, "little"))

    cap.add(QUERY, struct.pack("<2QiiQii", 0, TYPE + 0x10, *rectangle[:2], TYPE + 0x10, *rectangle[2:]))
    put(BASE + 0x4ab27a8, FIELD_SLOT)
    put(FIELD_SLOT, FIELD)
    put(FIELD + 0x200, selector, 4)
    put(BASE + 0x4ab27c8, TERRAIN_SLOT)
    put(TERRAIN_SLOT, TERRAIN)
    put(TERRAIN + 0x90, GRID)
    put(BASE + 0x4ab2010, TYPE)
    put(BASE + 0x4ab46c8, TABLE_SLOT)
    put(TABLE_SLOT, TABLE)
    put(GRID + 0x38, 3, 4)
    put(GRID + 0x3c, 3, 4)
    put(GRID + 0x48, 0 if scenario == "no-record-count" else 9, 4)
    put(GRID + 0x50, 0 if scenario == "no-record-pointer" else RECORD)
    put(GRID + 0x68, 0 if scenario == "no-flag-count" else 9, 4)
    put(GRID + 0x70, 0 if scenario == "no-flag-pointer" else FLAGS)
    for index in range(9):
        put(FLAGS + 4 * index, 4 if index == stop and scenario == "blocked-bit" else 0, 4)
        kinds = [29, 51, 8, 11]
        if scenario == "blocked-kind" and index == stop:
            kinds[corner] = (7, 43, 46)[corner % 3]
        cap.add(RECORD + 0x34 * index + 0x30, bytes(kinds))

    cap.result = 0
    cap.add_call(0x87bf88, 0x871f80, int(scenario != "not-level"))
    if scenario == "not-level":
        return cap
    x0, z0, x1, z1 = rectangle
    if z0 == z1 + 1:
        cap.result = 1
        return cap
    for z in range(z0, z1 + 1):
        for x in range(x0, max(x0, x1) + 1):
            index = 3*z+x
            valid = 0 <= x < 3 and 0 <= z < 3
            if valid and index == stop and scenario == "blocked-bit":
                return cap
            layer = scenario == "blocked-layer" and index == stop
            cap.add_call(0x87c048, 0x1285fe0, int(layer))
            if layer or not valid or scenario in ("no-record-pointer", "no-record-count"):
                return cap
            for i, (first, second) in enumerate(zip(REJECTION, REQUIREMENT, strict=True)):
                active = index == stop and i == corner
                rejected = active and scenario == "rejected"
                cap.add_call(first, 0x880630, int(rejected))
                if rejected or (active and scenario == "blocked-kind"):
                    return cap
                required = not (active and scenario == "required-missing")
                cap.add_call(second, 0x881090, int(required))
                if not required:
                    return cap
    cap.result = 1
    return cap


def synthetic_cases(capture):
    rows = []
    for selector, scenario in itertools.product((0, 1, 2), (
        "level", "not-level", "no-record-count", "no-record-pointer", "no-flag-count", "no-flag-pointer",
    )):
        rows.append(fixture(capture, scenario, selector, (0, 0, 1, 1), 0, 0))
    for selector, rectangle in itertools.product((0, 1), ((-1, 1, 2, 1), (2, 1, 2, 0), (2, 1, 1, 2), (1, 1, 3, 1))):
        rows.append(fixture(capture, "level", selector, rectangle, 0, 0))
    for scenario, stop, corner, selector in itertools.product(
        ("blocked-bit", "blocked-layer", "blocked-kind", "rejected", "required-missing"), (0, 1, 4), range(4), (0, 1)
    ):
        rows.append(fixture(capture, scenario, selector, (0, 0, 1, 1), stop, corner))
    return {f"synthetic-{i}": row for i, row in enumerate(rows)}
