"""Portable inputs for both simple masks of the shovel step cast."""
import itertools
import struct

from tests.acnh_shovel_intermediate_fixtures import bits, trig_table

BASE, STACK, ENTRY = 0x4000000000, 0x6500001000, 0x15ed290
POINT, TRIG, OWNER_SLOT, OWNER = 0x21000, 0x22000, 0x24000, 0x25000
SCENE_SLOT, SCENE, STAGE, VTABLE = 0x26000, 0x27000, 0x28000, 0x29000
NAME, TEXT, ROOT, ITEM = 0x2a000, 0x2b000, 0x2c000, 0x2d000
CENTER_PROBES = {4: (0x15ede4c, 0x15e11a0), 16: (0x15edfd4, 0x15ec764)}
RIM_PROBES = {4: (0x15ef5ac, 0x15e11a0), 16: (0x15ef890, 0x15ec764)}


def put(cap, address, value, width=8):
    cap.add(address, (value % (1 << (width * 8))).to_bytes(width, "little"))


def cast_state(capture, trig, mask, radius, scale, scene, coordinate, odd_heading):
    cap = capture(ENTRY, BASE, STACK, (POINT, 0, 0, 0, 0, 0))
    cap.add(POINT, struct.pack("<5fII", *coordinate, radius, scale, mask, 1))
    cap.add(TRIG, trig)
    put(cap, BASE + 0x4ab2d00, TRIG)
    put(cap, BASE + 0x5140230, 0x712381a7 if odd_heading else 0x20000000, 4)
    put(cap, BASE + 0x4ab2010, 0x40000)
    put(cap, BASE + 0x4ab2018, 0x41000)
    put(cap, BASE + 0x4ab27c8, 0x42000)
    put(cap, BASE + 0x4abfe68, 0x43000)
    put(cap, BASE + 0x4abc1a0, 0x44000)
    put(cap, BASE + 0x4ac0510, 0x45000)
    put(cap, BASE + 0x4ab5d40, ROOT)
    put(cap, ROOT + 8, 0 if scene % 2 else 7, 4)
    put(cap, ROOT + 12, 0, 4)
    put(cap, BASE + 0x4ab2720, SCENE_SLOT)
    put(cap, SCENE_SLOT, SCENE if scene else 0)
    put(cap, SCENE + 0x330, 1 if scene >= 2 else 0)
    put(cap, SCENE + 0x338, 13, 4)
    put(cap, BASE + 0x4ab2640, OWNER_SLOT)
    put(cap, OWNER_SLOT, OWNER)
    put(cap, OWNER, VTABLE)
    put(cap, VTABLE + 0x18, BASE + 0x5cb0a0)
    put(cap, NAME, TEXT)
    put(cap, BASE + 0x54c04a0 + 0x38, 0x81f6, 4)
    put(cap, BASE + 0x4ac3a80, STAGE)
    put(cap, STAGE + 0x38, 0x72b5, 4)
    return cap


def fixture(capture, trig, mask, radius, scale, stop, scene, mutate, coordinate, odd_heading):
    cap = cast_state(capture, trig, mask, radius, scale, scene, coordinate, odd_heading)

    def probe(site, target, index):
        hit = index == stop
        effect = (0, 0, 0, 0)
        if (mutate and index == 0) or (mutate and hit):
            effect = (1, bits(426.25), bits(13.5), bits(-16.25))
        cap.add_call(site, target, 1 if hit else 2, effect)
        return hit

    def contact(center):
        if mask != 4:
            return
        if scene >= 2:
            getter = 0x15ef99c if center else 0x15ef674
            cap.add_call(getter, 0x5cb0a0, NAME)
            cap.add_call(getter + 0xc, 0x5bd550, 0x758f7eaf if scene == 3 else 0x12)
        cap.add_call(0x15ef9e0 if center else 0x15ef6e8, 0x16baf44, scene % 2)
        cap.add_call(0x15ef9fc if center else 0x15ef704, 0x195190c, ITEM)

    cap.result = 0
    if probe(*CENTER_PROBES[mask], 0):
        contact(True)
        cap.result = mask
        return cap
    if abs(radius * scale) < 2**-23:
        return cap
    for i in range(1, 9):
        if probe(*RIM_PROBES[mask], i):
            contact(False)
            cap.result = mask
            break
    return cap


def synthetic_cases(capture):
    trig = trig_table()
    cases = []
    for mask, stop, mutate in itertools.product((4, 16), range(-1, 9), (False, True)):
        for radius, scale in ((0., 1.), (2**-24, 1.), (2**-23, 1.), (3.5, 0.8), (4.5, -1.)):
            cases.append(fixture(capture, trig, mask, radius, scale, stop, 0, mutate,
                                 (-0.125, -0.0, 693.25), stop % 2 == 0))
    for stop, scene, mutate in itertools.product((0, 1, 8), (1, 2, 3), (False, True)):
        cases.append(fixture(capture, trig, 4, 4.25, 0.8, stop, scene, mutate, (405., 15., 688.), True))
    return {f"synthetic-{i}": case for i, case in enumerate(cases)}
