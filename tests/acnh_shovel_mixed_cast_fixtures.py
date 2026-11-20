"""Portable mixed-material cases for the shovel step cast."""
import itertools

from tests.acnh_shovel_intermediate_fixtures import bits, trig_table
from tests.acnh_shovel_simple_cast_fixtures import (
    BASE, CENTER_PROBES, ITEM, NAME, RIM_PROBES, cast_state, put,
)

TERRAIN_SLOT, TERRAIN = 0x42000, 0x46000
DB_SLOT, DB, BASE_CACHE_SLOT, OVERLAY_CACHE_SLOT = 0x47000, 0x48000, 0x49000, 0x4a000
BASE_CACHE, OVERLAY_CACHE, ROW, SET, CLEAR = 0x4b000, 0x4c000, 0x4d000, 0x4e000, 0x4f000
BASE_SAMPLE = (0x15ed35c, 0x15eed74)
OVERLAY_SAMPLE = (0x15ed370, 0x15eed88)
BASE_ROW = (0x15ed390, 0x15eeda8)
BASE_COL = (0x15ed3b0, 0x15eedc8)
TILE_FLAG = (0x15ed698, 0x15ef098)
OVERLAY_CODE = (0x15ed80c, 0x15ef1e0)
OVERLAY_PROPERTY = (0x15ed870, 0x15ef240)
OVERLAY_ROW = (0x15ed890, 0x15ef260)
OVERLAY_COL = (0x15ed8b0, 0x15ef280)


def fixture(capture, trig, radius, scale, stop, hit, scene, mutate, variant):
    position = (-0.125, 15., -10.25) if variant % 2 else (405.125, -0.0, 688.)
    cap = cast_state(capture, trig, 0x67, radius, scale, scene, position, variant % 2 == 0)
    put(cap, TERRAIN_SLOT, TERRAIN)
    put(cap, BASE + 0x4ab46c8, DB_SLOT)
    put(cap, DB_SLOT, DB)
    put(cap, BASE + 0x4ab5d10, BASE_CACHE_SLOT)
    put(cap, BASE + 0x4ab5d28, OVERLAY_CACHE_SLOT)
    put(cap, BASE_CACHE_SLOT, BASE_CACHE)
    put(cap, OVERLAY_CACHE_SLOT, OVERLAY_CACHE)
    put(cap, SET, 1, 1)
    put(cap, CLEAR, 0, 1)

    def sample(sites, target, kind, moved=False):
        effect = (1, bits(-6.25), bits(13.125), bits(691.5), kind) if moved else (0, 0, 0, 0, kind)
        cap.add_call(sites[phase], target, 0, effect)

    def material(row_sites, col_sites, enabled):
        row = ROW if variant % 3 or enabled else 0
        cap.add_call(row_sites[phase], 0x8807cc, row)
        if row:
            column = SET if enabled else (CLEAR if variant % 3 == 1 else 0)
            cap.add_call(col_sites[phase], 0x8823a0, column)

    def contact():
        if scene >= 2:
            site = 0x15ef99c if phase == 0 else 0x15ef674
            cap.add_call(site, 0x5cb0a0, NAME)
            cap.add_call(site + 0xc, 0x5bd550, 0x758f7eaf if scene == 3 else 0x12)
        cap.add_call(0x15ef9e0 if phase == 0 else 0x15ef6e8, 0x16baf44, scene % 2)
        cap.add_call(0x15ef9fc if phase == 0 else 0x15ef704, 0x195190c, ITEM)

    cap.result = 0
    count = 1 if abs(radius * scale) < 2**-23 else 9
    for point_index in range(count):
        phase = int(point_index != 0)
        result = hit if point_index == stop else 0
        sample(BASE_SAMPLE, 0x83b024, 11, mutate and point_index == 0)
        sample(OVERLAY_SAMPLE, 0x83a950, 11 if result == 1 else 9)
        material(BASE_ROW, BASE_COL, result == 1 or variant == 3)
        if result == 1:
            cap.result = 1
            break
        cap.add_call(TILE_FLAG[phase], 0x1218220, int(result == 2))
        if result == 2:
            cap.result = 2
            break
        sample(OVERLAY_CODE, 0x83a950, 0x2e if result == 0x20 else 7)
        if result == 0x20:
            cap.result = 0x20
            break
        sample(OVERLAY_PROPERTY, 0x83a950, 31)
        material(OVERLAY_ROW, OVERLAY_COL, result == 0x40)
        if result == 0x40:
            cap.result = 0x40
            break
        site, target = (CENTER_PROBES if phase == 0 else RIM_PROBES)[4]
        cap.add_call(site, target, int(result == 4))
        if result == 4:
            contact()
            cap.result = 4
            break
    return cap


def synthetic_cases(capture):
    trig = trig_table()
    cases = []
    for stop, hit, mutate, variant in itertools.product((0, 1, 8), (0, 1, 2, 4, 0x20, 0x40),
                                                        (False, True), range(4)):
        cases.append(fixture(capture, trig, 4.25, 0.8, stop, hit, variant, mutate, variant))
    for radius, scale, hit in itertools.product((0., 2**-24, 2**-23), (1., -1.), (0, 4, 0x40)):
        cases.append(fixture(capture, trig, radius, scale, 0, hit, 3, False, 0))
    return {f"synthetic-{i}": case for i, case in enumerate(cases)}
