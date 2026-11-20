"""Synthetic contracts for retained callbacks under the stage-height routine."""
import struct

BASE = 0x4000000000
SP = 0x6500001000
FRAME = SP - 0xb0
POINT, OUT, STAGE = 0x20000, 0x21000, 0x22000
ITEM, SECOND, DESC_ENTRY, FLOAT_VALUE = 0x23000, 0x24000, 0x25000, 0x26000
CAT_SLOT, ALT_SLOT, METADATA_SLOT, PARAM_SLOT = 0x27000, 0x28000, 0x29000, 0x2a000
CATALOG, ALT_OWNER, ALT_CATALOG, METADATA = 0x2b000, 0x1000000, 0x2d000, 0x2e000
ID = 0x1234


def bits(value):
    return int.from_bytes(struct.pack("<f", value), "little")


def fixture(capture_type, name, expected, material):
    direct = name.startswith("direct")
    primary_enabled = name.startswith("primary-stage")
    secondary_enabled = name.startswith("secondary-stage")
    special = "special" in name
    stage_index = 1 if "mapped" in name else 0
    ctx_flag = int("same-flag" in name)
    far = "far" in name

    cap = capture_type(0x15e06d0, BASE, 0x15f3ffc, SP, (POINT, OUT, int("flags" in name), STAGE, int(special), 0))
    cap.operands = {
        0: (POINT, struct.pack("<3f", 400 if far else 405, 15, 680 if far else 685)),
        1: (OUT, struct.pack("<I", 0xdeadbeef)),
        3: (STAGE, struct.pack("<HHB", stage_index, ctx_flag, 1)),
    }
    cap.exit = {1: (OUT, struct.pack("<I", material))}
    cap.returned = (0, bits(expected))

    def put(address, value, width=8):
        cap.loads.append((address, width, value))

    def call(site, target, *args, out=0, height=0., effect=None):
        cap.calls.append((site, target, *args, *([0] * (6 - len(args)))))
        cap.outcomes[site] = (int(out), bits(height))
        if effect:
            cap.effects[site] = effect

    def alt_height(layer, enabled, offset):
        site = 0x15e0be0 if layer == 0 and enabled else 0x15e0b24 if layer == 0 else 0x15e0ddc
        call(site, 0x19e8c70, FRAME + 8, height=offset)

    def matching_stage(layer):
        if layer == 1:
            call(0x15e0d18, 0x28156e0, out=BASE + 0x54c04a0)
        put(BASE + 0x54c1b60, BASE + 0x54c1b58)
        if stage_index or ctx_flag:
            mapping = 0x12 if stage_index else 0
            call(0x15e0bb8 if layer == 0 else 0x15e0db4, 0x12837d0, stage_index, out=mapping)
            if mapping:
                call(0x15e0bcc if layer == 0 else 0x15e0dc8, 0x87bc40, mapping, FRAME + 0x20,
                     out="reject" not in name)
                return
        call(0x15e0ba8 if layer == 0 else 0x15e0da4, 0x87b830, FRAME + 0x20,
             out="reject" not in name)

    put(BASE + 0x4ab2010, 0x30000)
    put(BASE + 0x4abbc68, CAT_SLOT)
    put(BASE + 0x4ab43c0, ALT_SLOT)
    put(BASE + 0x4ac0428, METADATA_SLOT)
    put(BASE + 0x4abbca8, PARAM_SLOT)
    put(BASE + 0x59ed670, 0)
    put(PARAM_SLOT, 0x31000)
    put(CAT_SLOT, CATALOG if direct else 0)
    put(ALT_SLOT, ALT_OWNER if name != "missing" else 0)
    put(METADATA_SLOT, METADATA)
    put(ALT_OWNER + 0x57e58, ALT_CATALOG)
    for off in (0xb0, 0x488, 0x2b0):
        put(CATALOG + off, 0x2f000)
    put(0x2f008, 1)
    for address, table in ((CATALOG, 0x32000), (ALT_CATALOG, 0x33000), (METADATA, 0x34000)):
        put(address + 0x1f8, ID, 2)
        put(address + 0x1d8, 1, 4)
        put(address + 0x1e0, table)
        put(table, DESC_ENTRY)
    put(FLOAT_VALUE, bits(2.5), 4)
    put(ITEM, 0xfffe if name == "empty" else ID)
    put(SECOND, ID if secondary_enabled or "secondary-special" in name else 0xfffe)

    call(0x15e0720, 0x15e80f0, POINT, OUT, STAGE, height=10.0, effect=(OUT, struct.pack("<I", 3)))
    call(0x15e0794, 0x15e7980, FRAME + 8, FRAME + 0x20, 0, STAGE, out=ITEM)
    if name == "empty":
        return cap

    if direct:
        descriptor = struct.pack("<3Q", 0x30010, ID, DESC_ENTRY if "column" in name else 0)
        call(0x15e07ec, 0x267d6c4, ITEM, out=ID)
        call(0x15e081c, 0x114c030, FRAME + 8, ITEM, effect=(FRAME + 8, descriptor))
        call(0x15e0824, 0x267d6c4, ITEM, out=0xea79 if "fossil" in name else ID)
        variant = 2 if "two" in name else 1
        call(0x15e0830, 0x267ebd0, ITEM, out=variant)
        if variant == 2 and "flags" not in name:
            call(0x15e08c0, 0x15e026c, POINT, 0, 0, height=15.)
        if "column" in name:
            call(0x15e0920, 0x1146470, DESC_ENTRY, 0, 0x0c315945, 0x31000, out=FLOAT_VALUE)
        return cap

    call(0x15e0944, 0x267d6c4, ITEM, out=ID)
    if name == "missing":
        return cap
    blocked = name == "blocked"
    call(0x15e0984, 0x267e024, ITEM, out=blocked)
    if blocked:
        return cap
    has_primary = name != "unsupported"
    call(0x15e0990, 0x267e294, ITEM, out=has_primary)
    if has_primary:
        call(0x15e09d8, 0x267d6c4, FRAME + 0x10, out=ID)
        call(0x15e0a24, 0x267d6c4, ITEM, out=ID)
        call(0x15e0a54, 0x19e6020, FRAME + 8, out=primary_enabled)
        if primary_enabled:
            matching_stage(0)
            if "reject" not in name:
                alt_height(0, True, 0. if "zero" in name else 1.5)
        elif special:
            call(0x15e0b18, 0x15e8ee0, ITEM, out=name == "primary-special")
            if name == "primary-special":
                alt_height(0, False, -0.5)
    call(0x15e0c50, 0x15e7980, FRAME + 8, FRAME + 0x20, 1, STAGE, out=SECOND)
    if secondary_enabled or "secondary-special" in name:
        call(0x15e0c68, 0x267d6c4, SECOND, out=ID)
        call(0x15e0ca8, 0x267e294, SECOND, out=True)
        descriptor = struct.pack("<3Q", 0x30010, ID, DESC_ENTRY)
        call(0x15e0cb8, 0x19d5d04, FRAME + 8, SECOND, effect=(FRAME + 8, descriptor))
        call(0x15e0cc0, 0x19e6020, FRAME + 8, out=secondary_enabled)
        if secondary_enabled:
            matching_stage(1)
            if "reject" not in name:
                alt_height(1, True, 2.0)
        else:
            call(0x15e0d74, 0x15e8ee0, SECOND, out=True)
            alt_height(1, False, -0.25)
    return cap


def synthetic_cases(capture_type):
    scenarios = (
        ("empty", 10., 7), ("missing", 10., 7), ("blocked", 10., 7), ("unsupported", 10., 7),
        ("direct-fossil", 10., 7), ("direct-column", 12.5, 7), ("direct-null-row", 10., 7),
        ("direct-two-near", 10., 7), ("direct-two-far", 10., 3), ("direct-two-far-flags", 10., 7),
        ("primary-stage", 11.5, 7), ("primary-stage-zero", 15., 7), ("primary-stage-reject", 10., 7),
        ("primary-stage-mapped", 11.5, 7), ("primary-stage-same-flag", 11.5, 7), ("primary-special", 9.5, 7),
        ("secondary-stage", 12., 7), ("secondary-stage-reject", 10., 7), ("secondary-stage-mapped", 12., 7),
        ("secondary-special", 9.75, 7),
    )
    return {name: fixture(capture_type, name, result, material) for name, result, material in scenarios}
