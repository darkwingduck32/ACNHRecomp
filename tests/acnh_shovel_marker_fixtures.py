"""Portable boundary inputs for the original shovel marker replays."""
import itertools
import struct

from tests.acnh_shovel_intermediate_fixtures import bits, trig_table

BASE, SP = 0x4000000000, 0x6500001000
POINT, DESCRIPTOR, TYPE, MASK_BITS = 0x21000, 0x22000, 0x23000, 0x24000
OWNER_SLOT, OWNER, GRID, TRIG = 0x25000, 0x26000, 0x28000, 0x29000
OUTPUTS = tuple(0x2a000 + 4*i for i in range(4))

EXTENT, GLOBAL, MASK, CORNERS = 0x15ea0c8, 0x876320, 0x849ac8, 0x8493f8
DIRECT_SITES = (0x15ea1b0, 0x15ea290, 0x15ea398, 0x15ea490, 0x15ea58c)
GLOBAL_SITES = (0x15ea624, 0x15ea6bc, 0x15ea75c, 0x15ea824)
FALLBACK_SITES = (0x8494bc, 0x8494f4, 0x849528, 0x84955c)


def put(cap, address, value, width=8):
    cap.add(address, (value % (1 << (width * 8))).to_bytes(width, "little"))


def extent(capture, trig, radius, position, owner, active, stop, angle):
    cap = capture(EXTENT, BASE, SP, (POINT, 0, 0, 0, 0, 0, bits(radius)))
    cap.add(POINT, struct.pack("<3f", position[0], 15.0, position[1]))
    put(cap, BASE + 0x4ab2010, TYPE)
    put(cap, BASE + 0x4ab27c8, OWNER_SLOT)
    put(cap, BASE + 0x4ab2d00, TRIG)
    put(cap, BASE + 0x5140230, angle, 4)
    put(cap, OWNER_SLOT, OWNER if owner else 0)
    put(cap, OWNER + 0xa8, GRID if active else 0)
    cap.add(TRIG, trig)
    sites = []
    if owner and active:
        sites.append((DIRECT_SITES[0], MASK))
    if abs(radius) > 2**-23:
        if owner and active:
            sites.extend((site, MASK) for site in DIRECT_SITES[1:])
        sites.extend((site, GLOBAL) for site in GLOBAL_SITES)
    cap.result = 0
    for index, (site, target) in enumerate(sites):
        hit = int(index == stop)
        cap.add_call(site, target, hit)
        if hit:
            cap.result = 1
            break
    return cap


def global_marker(capture, owner, active, hit, tile):
    cap = capture(GLOBAL, BASE, SP, (DESCRIPTOR, 0, 0, 0, 0, 0, 0))
    cap.add(DESCRIPTOR, struct.pack("<Qii", TYPE + 0x10, *tile))
    put(cap, BASE + 0x4bfae30, OWNER if owner else 0)
    put(cap, OWNER + 0xa8, GRID if active else 0)
    cap.result = int(owner and active and hit)
    if owner and active:
        cap.add_call(0x876398, MASK, int(hit))
    return cap


def mask(capture, sampled, kinds, enabled):
    cap = capture(MASK, BASE, SP, (OWNER, DESCRIPTOR, MASK_BITS, 0, 0, 0, 0))
    cap.add(DESCRIPTOR, struct.pack("<Qii", TYPE + 0x10, -1, 42))
    mask = sum(1 << value for value in enabled)
    cap.add(MASK_BITS, mask.to_bytes(12, "little"))
    cap.add_call(0x849af8, CORNERS, int(sampled), kinds)
    cap.result = int(sampled and any(kind < 73 and mask & (1 << kind) for kind in kinds))
    return cap


def quadrant(capture, tile, values, fallbacks, move, alias):
    outputs = (*OUTPUTS[:3], OUTPUTS[0] if alias else OUTPUTS[3])
    cap = capture(CORNERS, BASE, SP, (OWNER, DESCRIPTOR, *outputs, 0))
    cap.add(DESCRIPTOR, struct.pack("<Qii", TYPE + 0x10, *tile))
    put(cap, OWNER + 0x80, GRID)
    put(cap, GRID + 0x38, 90, 4)
    put(cap, GRID + 0x3c, 140, 4)
    for i, address in enumerate(OUTPUTS):
        put(cap, address, 11 + i, 4)
    cap.result = int(0 <= tile[0] < 90 and 0 <= tile[1] < 140)
    if cap.result:
        packed = int.from_bytes(bytes(values), "little")
        cap.add_call(0x849458, 0x862530, outputs=(packed, 0, 0, 0))
    x, z = (coordinate * 5. + 2.5 for coordinate in tile)
    for i, (dx, dz) in enumerate(((0, -1.25), (-1.25, 1.25), (1.25, 1.25), (1.25, -1.25))):
        x += dx
        z += dz
        if i == 1 and move:
            x += 0.5
            z -= 1.
        cap.add_call(FALLBACK_SITES[i], 0x87a980, fallbacks[i], (bits(x), bits(z), 0, 0))
    return cap


def synthetic_cases(capture):
    trig = trig_table()
    cases = []
    for i, stop in enumerate(range(-1, 9)):
        for radius in (0., -2**-23, 4.25, -7.):
            cases.append(extent(capture, trig, radius, (400.5, 680.5) if i % 2 else (-20.5, 0.), True, True,
                                stop, 0x20000000 if i % 2 else 0x7032831a))
    for owner, active in itertools.product((False, True), repeat=2):
        cases.append(extent(capture, trig, 3.125, (-20.5, -30.5), owner, active, -1, 0x20000000))
        for hit in (False, True):
            for tile in ((0, 0), (-1, -1), (89, 139)):
                cases.append(global_marker(capture, owner, active, hit, tile))

    for sampled in (False, True):
        for kind in (0, 8, 31, 32, 63, 64, 72, 73, 255, 0xffffffff):
            for corner in range(4):
                kinds = [255] * 4
                kinds[corner] = kind
                cases.append(mask(capture, sampled, kinds, (0, 8, 31, 32, 63, 64, 72)))
    for tile in ((0, 0), (89, 139), (-1, 2), (8, -7), (90, 25), (8, 140)):
        for fallbacks, move, alias in itertools.product(((7, 7, 7, 7), (8, 7, 32, 1)), (False, True), (False, True)):
            cases.append(quadrant(capture, tile, (0, 8, 3, 2), fallbacks, move, alias))
    return {f"synthetic-{i}": cap for i, cap in enumerate(cases)}
