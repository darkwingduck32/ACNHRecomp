"""Synthetic surface, stage and radial obstruction cases."""
import itertools
import math
import struct

BASE = 0x4000000000
SP = 0x6500001000
POINT, SCENE_SLOT, SCENE, OWNER = 0x20000, 0x21000, 0x22000, 0x23000
VTABLE, STAGE, NODE, NAME, TEXT, TRIG = 0x24000, 0x25000, 0x26000, 0x27000, 0x28000, 0x29000


def bits(value):
    return int.from_bytes(struct.pack("<f", value), "little")


def trig_table():
    data = bytearray()
    for i in range(256):
        a, b = (math.tau * (i + j) / 256 for j in (0, 1))
        data.extend(struct.pack("<4f", math.sin(a), math.sin(b) - math.sin(a),
                                math.cos(a), math.cos(b) - math.cos(a)))
    return data


def fixture(capture_type, trig, radius, surface, casts, skip, plan, have_scene):
    cap = capture_type(BASE, SP, (POINT, int(not casts), int(surface), int(skip), bits(radius)))

    def put(address, value, width=8):
        cap.add(address, value.to_bytes(width, "little"))

    def call(site, target, out=0, height=0.):
        cap.calls.append((site, target))
        cap.answers.append((int(out), bits(height)))

    def stage_calls(center):
        if have_scene:
            call(0x15e9d2c if center else 0x15e9fe0, 0x5cb0a0, NAME)
            call(0x15e9d38 if center else 0x15e9fec, 0x5bd550, 0x758f7eaf)

    cap.add(POINT, struct.pack("<3f", 403.25, -0.0 if plan=="small" else 15., 688.25))
    put(BASE + 0x4ab2d00, TRIG)
    put(BASE + 0x5140230, 0x712381a7 if skip else 0x20000000, 4)
    cap.add(TRIG, trig)
    put(BASE + 0x4ab2720, SCENE_SLOT)
    put(SCENE_SLOT, SCENE if have_scene else 0)
    put(BASE + 0x59ed670, SCENE if have_scene else 0)
    put(SCENE + 0x330, 1)
    put(SCENE + 0x338, 12, 4)
    put(BASE + 0x59ed668, OWNER)
    put(OWNER, VTABLE)
    put(VTABLE + 0x18, BASE + 0x5cb0a0)
    put(NAME, TEXT)
    put(BASE + 0x4ac3a80, STAGE)
    put(STAGE + 0x38, 0x5a31, 4)
    put(BASE + 0x54c04d8, 0x1f23, 4)
    head = BASE + 0x54c1b58
    put(head + 8, NODE if have_scene else head)
    put(NODE - 8, 0x5a31, 4)
    put(NODE + 8, head)

    if surface:
        call(0x15e9ccc, 0x15ea0c8, plan == "center-marker")
        if plan == "center-marker":
            cap.result = 0
            return cap
    call(0x15e9d68 if surface else 0x15e9cec, 0x15e026c, height=15.)
    if not surface:
        stage_calls(True)
        call(0x15e9df8, 0x15e06d0, height=17. if plan == "center-height" else 15.75)
        if plan == "center-height":
            cap.result = 0
            return cap
    if abs(radius) <= 2**-23:
        cap.result = 1
        return cap
    if casts:
        call(0x15e9e68, 0x15ed290, plan == "center-cast")
        if plan == "center-cast":
            cap.result = 0
            return cap

    cap.result = 1
    for sector in range(8):
        if skip:
            ignored = plan == "all-skipped" or (plan == "skip-even" and sector % 2 == 0)
            call(0x15e9f80, 0x15ed290, ignored)
            if ignored:
                continue
        height = 16.125 if plan == "late-height" and sector == 6 else 14.
        if surface:
            call(0x15e9f9c, 0x15e026c, height=height)
        else:
            stage_calls(False)
            call(0x15ea088, 0x15e06d0, height=height)
        if casts:
            blocked = plan == "late-cast" and sector == 5
            call(0x15e9edc, 0x15ed290, blocked)
            if blocked:
                cap.result = 0
                break
        if abs(height - 15.) > 1.:
            cap.result = 0
            break
    return cap


def synthetic_cases(capture_type):
    trig = trig_table()
    modes = itertools.product((False, True), (False, True), (False, True))
    scenarios = []
    for surface, casts, skip in modes:
        for radius, plan in ((0., "small"), (-2**-23, "small"), (2.75, "all-pass"), (-0.5, "late-height")):
            scenarios.append((radius, surface, casts, skip, plan, not surface and casts))
        if surface:
            scenarios.append((1., surface, casts, skip, "center-marker", False))
        else:
            scenarios.append((1., surface, casts, skip, "center-height", True))
        if casts:
            scenarios.extend((1., surface, casts, skip, label, False) for label in ("center-cast", "late-cast"))
        if skip:
            scenarios.extend((-1., surface, casts, skip, label, True) for label in ("all-skipped", "skip-even"))
    return {f"synthetic-{i}": fixture(capture_type, trig, *row) for i, row in enumerate(scenarios)}
