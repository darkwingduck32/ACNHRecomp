"""Portable fixtures for the shovel step collision probes."""
import itertools
import struct

from tests.acnh_shovel_intermediate_fixtures import bits

BASE, STACK = 0x4000000000, 0x6500001000
POINT, SCENE_SLOT, SCENE, OWNER_SLOT = 0x21000, 0x22000, 0x23000, 0x24000
OWNER, VTABLE, NAME, TEXT = 0x25000, 0x26000, 0x27000, 0x28000
STAGE, FIRST, SECOND, FLAGS = 0x29000, 0x2a000, 0x2b000, 0x2c000
FIELD_ITEM, COORD_TYPE, QUERY_TYPE = 0x2d000, 0x2e000, 0x2f000
HEIGHT, ITEM = 0x15e11a0, 0x15ec764


def put(cap, address, value, width=8):
    cap.add(address, (value % (1 << (width * 8))).to_bytes(width, "little"))


def probe(capture, entry, point, first, second, scene, height, stage_height,
          scene_id, special, scene_flag, item, permit):
    cap = capture(entry, BASE, STACK, (POINT, first, second, 0, 0, 0))
    cap.add(POINT, struct.pack("<3f", point[0], 15., point[1]))
    put(cap, BASE + 0x4ab2720, SCENE_SLOT)
    put(cap, SCENE_SLOT, SCENE if scene else 0)
    put(cap, SCENE + 0x330, 1 if scene >= 2 else 0)
    put(cap, SCENE + 0x338, 82, 4)
    put(cap, BASE + 0x59ed668, OWNER)
    put(cap, BASE + 0x4ab2640, OWNER_SLOT)
    put(cap, OWNER_SLOT, OWNER)
    put(cap, OWNER, VTABLE)
    put(cap, VTABLE + 0x18, BASE + 0x5cb0a0)
    put(cap, NAME, TEXT)
    put(cap, BASE + 0x4ac3a80, STAGE)
    put(cap, BASE + 0x54c04a0 + 0x38, 0x11, 4)
    put(cap, STAGE + 0x38, 0x91045f65 if special else 0x1122, 4)
    head = BASE + 0x54c1b58
    put(cap, head + 8, FIRST)
    put(cap, FIRST - 8, 0x11, 4)
    put(cap, FIRST + 8, SECOND)
    put(cap, SECOND - 8, 0x91045f65 if special else 0x1122, 4)
    put(cap, SECOND + 8, head)
    put(cap, BASE + 0x4b487d4, scene_id, 2)
    put(cap, BASE + 0x4ac39f8, FLAGS)
    put(cap, FLAGS + (8 * scene_id if 0 <= scene_id < 256 else 0), 1 << 25 if scene_flag else 0, 4)
    put(cap, BASE + 0x4ab2010, COORD_TYPE)
    put(cap, BASE + 0x4abfe68, QUERY_TYPE)

    def call(site, target, result=0, scalar=0.):
        cap.add_call(site, target, result, (bits(scalar),))

    is_height = entry == HEIGHT
    call(0x15e11d0 if is_height else 0x15ec7b4, 0x15e026c, scalar=height)
    if scene >= 2:
        site = 0x15e1210 if is_height else 0x15ec85c
        call(site, 0x5cb0a0, NAME, height)
        call(site + 0xc, 0x5bd550, 0x758f7eaf if scene == 3 else 0, height - 4)
    if is_height:
        call(0x15e12bc, 0x15e06d0, scalar=stage_height)
    else:
        call(0x15ec898, 0x16baf44, permit)
        call(0x15ec8b4, 0x195190c, FIELD_ITEM)
        call(0x15ec8b8, 0x267d6c4, item)
    return cap


def synthetic_cases(capture):
    cases = []
    for first, second, scene in itertools.product((0, 3), (0, 3), range(4)):
        for surface, height in ((15., 15.), (1., 1.1), (15., 15.125), (float("nan"), 15.)):
            cases.append(probe(capture, HEIGHT, (401.25, 691.25), first, second, scene,
                               surface, height, -1, False, False, 0, 0))
    for flag, special, scene_id in itertools.product((False, True), (False, True), (7, 300)):
        for point in ((395., 650.), (415., 650.), (435., 665.), (440., 665.), (405., 663.)):
            cases.append(probe(capture, HEIGHT, point, 1, 1, 3,
                               15., 15., scene_id, special, flag, 0, 0))
    for first, scene, permit in itertools.product((0, 1), range(4), (0, 1)):
        for point, item in (((405., 695.), 1), ((405., 695.), 0xea79), ((400., 695.), 0xea79),
                            ((409., 699.), 0xea79), ((-5., -5.), 0xea79), ((-0.5, -10.), 0xea79)):
            cases.append(probe(capture, ITEM, point, first, 0, scene,
                               15., 15., -1, False, False, item, permit))
    return {f"synthetic-{i}": cap for i, cap in enumerate(cases)}
