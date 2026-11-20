"""Generated scenarios for independent replay of the full point classifier."""
import struct


BASE = 0x4000000000
SP = 0x6500001000
FRAME = SP - 0xf0
POINT, REQUEST, ITEM = 0x20000, 0x21000, 0x22000
SCENE, CATALOG_SLOT, ALTERNATE_SLOT, OWNER_SLOT = 0x23000, 0x24000, 0x25000, 0x26000
CATALOG, CATALOG_DATA, ALTERNATE, METADATA = 0x27000, 0x28000, 0x29000, 0x2a000
TERRAIN, ALTERNATE_OWNER, METADATA_SLOT = 0x2b000, 0x1000000, 0x2c000
STAGE_KEY = 0x123abcd
ITEM_ID = 0x1234
RESULT_SENTINEL = 0x76543210


def bits(value):
    return int.from_bytes(struct.pack("<f", value), "little")


def fixture(capture_type, path, expected):
    raised = path.startswith("raised")
    points = struct.pack("<3f", 405.0, 16.0, 690.0)
    alternate = struct.pack("<3f", 410.0, 17.0, 695.0)
    request = bytearray(0x44)
    struct.pack_into("<3f", request, 0, 400.0, 15.0, 680.0)
    struct.pack_into("<f", request, 0x24, 2.0)
    struct.pack_into("<I", request, 0x38, RESULT_SENTINEL)
    request[0x40] = path.startswith("retry")
    cap = capture_type(BASE, 0x15f39c8, SP, (POINT, points), (REQUEST, bytes(request)))

    def put(address, value, width=8):
        cap.loads.append((address, width, value))

    def call(site, target, *args, result=0, height=0., buffer=None):
        cap.calls.append((site, target, *args, *([0] * (6 - len(args)))))
        cap.outcomes[site] = (int(result), bits(height))
        if buffer:
            cap.child_buffers[site] = buffer

    put(BASE + 0x4ab2720, SCENE)
    put(SCENE, 0)
    put(BASE + 0x54c1b60, BASE + 0x54c1b58)
    put(BASE + 0x54c04d8, STAGE_KEY, 4)
    put(BASE + 0x4ab2010, 0x30000)
    put(BASE + 0x4abfe68, 0x30100)
    put(BASE + 0x4ac0510, 0x30200)
    put(BASE + 0x4abbc68, CATALOG_SLOT)
    put(BASE + 0x4ab43c0, ALTERNATE_SLOT)
    put(BASE + 0x4ab27c8, OWNER_SLOT)
    put(BASE + 0x4ac0428, METADATA_SLOT)
    put(METADATA_SLOT, METADATA)
    put(TERRAIN + 0xa8, 1)
    put(OWNER_SLOT, TERRAIN if path == "raised-excluded" else 0)

    use_catalog = "category" in path or "fossil" in path
    has_alternate = "alternate" in path
    put(CATALOG_SLOT, CATALOG if use_catalog else 0)
    put(ALTERNATE_SLOT, ALTERNATE_OWNER if has_alternate else 0)
    put(ALTERNATE_OWNER + 0x57e58, ALTERNATE)
    for offset in (0xb0, 0x488, 0x2b0):
        put(CATALOG + offset, CATALOG_DATA)
    put(CATALOG_DATA + 8, 1)
    for addr, table in ((CATALOG, 0x31000), (ALTERNATE, 0x32000), (METADATA, 0x33000)):
        put(addr + 0x1f8, ITEM_ID, 2)
        put(addr + 0x1d8, 1, 4)
        put(addr + 0x1e0, table)
        put(table, 0x34000)
    packed = struct.pack("<HBBI", ITEM_ID, 4 if path == "level-alternate-excluded" else 0, 0, 3)
    put(ITEM, int.from_bytes(packed, "little"))

    surface = 30. if "step" in path else 5. if "drop" in path else 15.
    terrain = 20. if raised else 13. if path == "below" else 15.
    call(0x15f3ff8, 0x15e06d0, POINT, 0, 1, FRAME + 8, 0, height=terrain)
    call(0x15f4010, 0x15e026c, POINT, 0, 0, height=surface)
    call(0x15f4024, 0x15ea82c, POINT, result=path == "blocked")

    chosen = points
    early = path in {"blocked", "below", "retry-success"}
    if path.startswith("retry"):
        call(0x15f418c, 0x15f0c64, FRAME + 0x50, FRAME + 0x70, buffer=(FRAME + 0x70, alternate))
        call(0x15f41dc, 0x15f36e0, FRAME + 8, result=1 if early else 14,
             buffer=(FRAME + 8, bytes(0x44)))
        chosen = alternate

    if not early:
        call(0x15f42d4 if raised else 0x15f4610, 0x16baf44, STAGE_KEY)
        call(0x15f42f0 if raised else 0x15f462c, 0x195190c, FRAME + 8, 0,
             FRAME + (0x50 if raised else 0x90), FRAME + (0x90 if raised else 0x80), result=ITEM)
        if path == "raised-excluded":
            call(0x15f436c, 0x849ac8, TERRAIN + 0x28, FRAME + 0x80, FRAME + 8, result=1)
        elif "step" in path:
            call(0x15f43bc, 0x15ed290, FRAME + 8, 0, 0, 0, result=path == "raised-step-blocked")
            if path != "raised-step-blocked":
                call(0x15f43d0, 0x15f33f0, REQUEST, POINT, result=path == "raised-step-moved")
                if path != "raised-step-moved":
                    call(0x15f47e4, 0x876320, FRAME + 8, result=path == "raised-step-barrier")
        else:
            item_calls(call, path, raised, use_catalog, has_alternate, packed)

    # The ordinary 0 return and the height-gap 14 paths leave the request's code as it was.
    request[0x2c:0x38] = chosen
    if expected and path not in ("below", "level-category-drop"):
        struct.pack_into("<I", request, 0x38, expected)
    cap.exit = (REQUEST, bytes(request))
    cap.return_value = expected
    return cap


def item_calls(call, path, raised, use_catalog, has_alternate, packed):
    def alternate_calls():
        call(0x15f4440 if raised else 0x15f46c4, 0x267d6c4, ITEM, result=ITEM_ID)
        if not has_alternate:
            return
        call(0x15f44b8 if raised else 0x15f473c, 0x267d6c4, FRAME + 0x10, result=ITEM_ID)
        call(0x15f4504 if raised else 0x15f4788, 0x267d6c4, ITEM, result=ITEM_ID)
        if not raised:
            call(0x15f47b8, 0x19e59b0, FRAME + 8)
            if path == "level-alternate-excluded":
                return
        supported = path not in ("raised-alternate-unsupported", "level-alternate-unsupported")
        call(0x15f4534 if raised else 0x15f480c, 0x267e294, ITEM, result=supported)
        if supported:
            enabled = path in ("raised-alternate-success", "level-alternate-gate")
            call(0x15f4540 if raised else 0x15f4818, 0x19e6020, FRAME + 8, result=enabled)

    if raised:
        alternate_calls()
    if not use_catalog:
        if not raised:
            alternate_calls()
        return

    call(0x15f4594 if raised else 0x15f4678, 0x267d6c4, ITEM, result=ITEM_ID)
    call(0x15f45c0 if raised else 0x15f46a4, 0x267d6c4, ITEM, result=0xea79 if "fossil" in path else ITEM_ID)
    if "fossil" in path:
        if not raised:
            alternate_calls()
        return

    metadata = 0x35000
    descriptor = struct.pack("<Q", 0x35010) + packed + struct.pack("<Q", metadata)
    call(0x15f45dc if raised else 0x15f483c, 0x114c030, FRAME + 8, ITEM, buffer=(FRAME + 8, descriptor))

    def category(site, number):
        call(site, 0x113db50, FRAME + 0x10, metadata, result=number)

    if raised:
        category(0x15f45ec, 0x2e if path == "raised-category-special" else 0)
        if path == "raised-category-special":
            return
        special = path == "raised-category-override"
        call(0x15f488c, 0x114dc64, FRAME + 8, 0, result=special)
        if not special:
            low = path == "raised-category-low"
            category(0x15f48d4, 0xb if low else 0)
            if not low:
                category(0x15f48f4, 0x2d if path == "raised-category-other" else 0)
    else:
        category(0x15f484c, 0x12 if path == "level-category-first" else 0)
        if path != "level-category-first":
            category(0x15f486c, 0x23 if path == "level-category-second" else 0)
            if path != "level-category-second":
                category(0x15f48a4, 0 if path == "level-category-miss" else 0xb)


def synthetic_cases(capture_type):
    scenarios = (
        ("blocked", 14), ("below", 14), ("level-empty", 0), ("retry-success", 1), ("retry-reject", 0),
        ("level-fossil", 0), ("level-alternate-excluded", 10), ("level-alternate-success", 2),
        ("level-alternate-gate", 9), ("level-alternate-unsupported", 9), ("level-category-first", 7),
        ("level-category-second", 8), ("level-category-low", 4), ("level-category-miss", 9),
        ("level-category-drop", 14), ("raised-excluded", 11), ("raised-step-blocked", 14),
        ("raised-step-moved", 13), ("raised-step-height", 13), ("raised-step-barrier", 12),
        ("raised-alternate-success", 1), ("raised-alternate-unsupported", 9), ("raised-alternate-switch", 9),
        ("raised-category-special", 5), ("raised-category-override", 3), ("raised-category-low", 4),
        ("raised-category-other", 6), ("raised-category-miss", 9), ("raised-fossil", 0),
    )
    return {name: fixture(capture_type, name, result) for name, result in scenarios}
