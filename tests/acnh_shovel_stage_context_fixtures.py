"""Synthetic fixtures for alternate stages, layers and provider records."""
import itertools
import struct

FRAMES = {0x15e80f0: 0x50, 0x15e7980: 0x70, 0x195190c: 0x40}
BASE = 0x4000000000
SP = 0x6500001000
POINT, OUT, CONTEXT = 0x20000, 0x21000, 0x22000
SCENE_SLOT, WORLD_SLOT, WORLD = 0x23000, 0x24000, 0x25000
NODE_A, NODE_B, RECORD = 0x26000, 0x27000, 0x28000
GRID_SLOT, GRID_OWNER, GRID, PROVIDER = 0x29000, 0x2a000, 0x2b000, 0x2c000
EMPTY = BASE + 0x5486008
DEFAULT_KEY, ALT_KEY = 0x2468abcd, 0x3689abcd


def bits(value):
    return int.from_bytes(struct.pack("<f", value), "little")


def put(cap, address, value, width=8):
    cap.loads.append((address, width, value))


def call(cap, site, target, *args, result=0, height=0., effect=None):
    cap.calls.append((site, target, *args, *([0] * (6 - len(args)))))
    cap.outcomes[site] = (int(result), bits(height))
    if effect:
        cap.effects[site] = effect


def context_fixture(capture_type, entry, mode, layer):
    height = entry == 0x15e80f0
    ordinal = 0 if mode in ("current", "current-flag") else 19 if mode == "missing" else 1
    alternate = 3 if mode != "current" else 0
    args = (POINT, OUT, CONTEXT, 0, 0, 0) if height else (POINT, OUT, layer, CONTEXT, 0, 0)
    cap = capture_type(entry, BASE, 0, SP, args)
    cap.operands = {
        0: (POINT, struct.pack("<3f", 404., 15., 689.) if height else struct.pack("<Qii", 0x45670, 80, 137)),
        1: (OUT, struct.pack("<I", 0xdeadbeef) if height else struct.pack("<Qii", 0x45670, 0, 0)),
        2 if height else 3: (CONTEXT, struct.pack("<HHB", ordinal, alternate, 1)),
    }
    put(cap, BASE + 0x4ab2720, SCENE_SLOT)
    put(cap, SCENE_SLOT, 0)
    put(cap, BASE + 0x4ab27c8, WORLD_SLOT)
    put(cap, WORLD_SLOT, WORLD)
    put(cap, BASE + 0x54c04d8, DEFAULT_KEY, 4)
    head = BASE + 0x54c1b58
    put(cap, head + 8, NODE_A + 0x40)
    put(cap, NODE_A + 0x38, DEFAULT_KEY, 4)
    put(cap, NODE_A + 0x48, NODE_B + 0x40)
    put(cap, NODE_B + 0x38, ALT_KEY, 4)
    put(cap, NODE_B + 0x48, head)
    put(cap, BASE + 0x4abfe68, 0x50000)
    table = {"stage-one": 1, "stage-fifty": 0x32, "stage-two": 2, "world-missing": 1}.get(mode, 0)
    for index in range(0x39):
        key = DEFAULT_KEY if index == 0 else ALT_KEY if index == table else index
        put(cap, BASE + 0x506b108 + 4*index, key, 4)
    if height:
        value = 0.
        result = cap.operands[1][1]
        if mode in ("current", "current-flag"):
            value = 15.25
            result = struct.pack("<I", 4)
            call(cap, 0x15e8224, 0x83a950, WORLD + 0x28, POINT, OUT, height=value, effect=(OUT, result))
        elif table in (1, 0x32):
            put(cap, BASE + 0x4bfae30, WORLD)
            put(cap, WORLD + 0x90, 0 if mode == "world-missing" else 1)
            if mode == "world-missing":
                value = -500.
            else:
                value = 9.75
                result = struct.pack("<I", 11)
                call(cap, 0x15e82ec, 0x84ba6c, WORLD + 0x28, POINT, OUT, height=value, effect=(OUT, result))
        elif table:
            value = 12.625
            result = struct.pack("<I", 8)
            call(cap, 0x15e831c, 0x15e87e0, POINT, OUT, CONTEXT, height=value, effect=(OUT, result))
        cap.returned = (0, bits(value))
    else:
        if table or mode in ("current", "current-flag"):
            result = struct.pack("<Qii", 0x45670, 80, 137)
            call(cap, 0x15e7bd4, 0x16baf44, ALT_KEY if table else DEFAULT_KEY, result=table != 0)
            call(cap, 0x15e7bf0, 0x195190c, SP - FRAMES[entry], layer & 0xffffffff, POINT, OUT,
                 result=RECORD, effect=(OUT, result))
            cap.returned = (RECORD, 0)
        else:
            result = cap.operands[1][1]
            cap.returned = (EMPTY, 0)
    cap.exit = {1: (OUT, result)}
    return cap


def cell_fixture(capture_type, allowed, table, layer, provider_present, selected):
    cap = capture_type(0x195190c, BASE, 0, SP, (CONTEXT, (1 << 40) | layer, POINT, OUT, 0, 0))
    cap.operands = {0: (CONTEXT, struct.pack("<QIII", 0x433333, table, DEFAULT_KEY, 0)),
                    2: (POINT, struct.pack("<Qii", 0x45670, 80, 137)),
                    3: (OUT, struct.pack("<Qii", 0x45670, 0, 0))}
    put(cap, BASE + 0x4ab27a8, GRID_SLOT)
    put(cap, GRID_SLOT, GRID_OWNER)
    offset = 0x1f8 if table == 0 else 0x30 + 8 * (table if table < 0x39 else 0)
    put(cap, GRID_OWNER + offset, GRID)
    put(cap, GRID + 0xd0 + 16 * (layer if layer < 8 else 0), PROVIDER if provider_present else 0)
    put(cap, PROVIDER + 0x198, selected, 4)
    call(cap, 0x1951934, 0x194f798, CONTEXT, layer, POINT, result=3 if allowed else 0)
    result = cap.operands[3][1]
    if allowed and provider_present:
        result = struct.pack("<Qii", 0x45670, 81, 139)
        view = PROVIDER + 0x130 + 0x30 * (selected if selected < 2 else 0)
        call(cap, 0x19519ec, 0x11eb21c, view, POINT, OUT, result=RECORD, effect=(OUT, result))
        cap.returned = (RECORD, 0)
    else:
        cap.returned = (EMPTY, 0)
    cap.exit = {3: (OUT, result)}
    return cap


def synthetic_cases(capture_type):
    rows = {}
    modes = ("current", "current-flag", "missing", "stage-one", "stage-fifty", "stage-two", "world-missing")
    for entry, mode, layer in itertools.product((0x15e80f0, 0x15e7980), modes, (0, 1, 0x800000003)):
        if entry == 0x15e80f0 and layer != 0:
            continue
        rows[entry, mode, layer] = context_fixture(capture_type, entry, mode, layer)
    for state in itertools.product((True, False), (0, 1, 56, 57, 0xffffffff), (0, 1, 7, 8), (True, False), (0, 1, 2)):
        rows[(0x195190c, *state)] = cell_fixture(capture_type, *state)
    return rows
