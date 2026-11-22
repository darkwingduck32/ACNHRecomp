"""Explicit branch arrangements built from the game's intact foliage/snow parts.

Coordinates are authored in mature oak units. Growth 3 uses the stock 0.8
scale. Each assembly moves its snow, leaf fringe and interior together; no
random scatter, analytic snow surfaces or independent crown closure is used.
"""
import math
import re

import numpy as np

from assemble_freecamera_oak import components, world_positions
from freecamera_trees import string, u64


def yaw(degrees):
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return np.array(((c, 0, s), (0, 1, 0), (-s, 0, c)))


# source lobe, destination X/Z, height adjustment, outward turn.
# Two shoulders and a slightly raised rear branch, topped by an offset leader.
OAK_BRANCHES = (
    ('left', -6.4, 3.0, 0.0, -35),
    ('right', 6.4, 3.0, .5, 35),
    ('left', -.5, -6.0, 1.0, 180),
    ('top', -.4, -.2, 0.0, -15),
)
OAK_PIVOTS = {'left': (-7.2, 0, 0), 'right': (7.2, 0, 0), 'top': (0, 0, -3.2)}


def snow_branch_plans(tree, model, name, shapes):
    """Return explicit replacement plans, or {} for models outside this edit."""
    match = re.fullmatch(r'PltTree(Oak|Bamboo)([34])Snow(?:Node)?', name)
    if not match:
        return {}
    family, stage = match.groups()
    scale = .8 if family == 'Oak' and stage == '3' else 1.
    result = {}
    for shape in shapes:
        material = string(tree.data, u64(tree.data, shape+8)).split('__')[-1]
        if 'Leaf' not in material and material != 'mWinterSnow':
            continue
        points = np.asarray(world_positions(tree, dict(model_offset=model, shape=shape, vertex=u64(tree.data, shape+16))))
        parts = []
        for triangles in components(tree.triangles(shape)):
            used = sorted({i for tri in triangles for i in tri})
            center = points[used].mean(0)
            parts.append(dict(vertices=used, triangles=triangles, center=center))
        plan = []
        if family == 'Oak':
            threshold = (24 if material == 'mWinterSnow' else 19 if 'LeafSph' in material else 21.8)*scale
            for part in parts:
                p = part['center']
                lobe = 'top' if p[1] > threshold else 'left' if p[0] < 0 else 'right'
                pivot = np.asarray(OAK_PIVOTS[lobe])*scale
                for source, x, z, rise, turn in OAK_BRANCHES:
                    if source != lobe:
                        continue
                    target = np.asarray((x, rise, z))*scale
                    # Snow retains the original sculpted overhang and scallops.
                    # Leaf fringes get a rear half around the SAME branch pivot.
                    turns = (turn,) if material == 'mWinterSnow' else (turn, turn+180)
                    for angle in turns:
                        r = yaw(angle)
                        plan.append(dict(source=part, center=target+r@(p-pivot), rotation=r))
        else:
            # Preserve bamboo's ascending spray and two offset snow layers.
            # Its stock front is a whole assembly; turn the foliage fringe about
            # the cap's center to provide a rear, without revolving a snow disk.
            pivot = np.asarray((0., 0., -3.4 if stage == '4' else -2.0))
            for part in parts:
                low_spray = part['center'][1] < (20.5 if stage == '4' else 12.)
                turns = (0,) if material == 'mWinterSnow' or low_spray else (0, 180)
                # A few side-facing sprays break the flat side silhouette.
                # Select the central front sprays of each authored leaf tier.
                if material.endswith('LeafSnow') and not low_spray and abs(part['center'][0]) < 3.5 and part['center'][2] > 0:
                    turns = (0, 90, 180, 270)
                for angle in turns:
                    r = yaw(angle)
                    plan.append(dict(source=part, center=pivot+r@(part['center']-pivot), rotation=r))
                if low_spray and material.endswith('LeafSnow'):
                    plan.append(dict(source=part, center=part['center'].copy(), rotation=np.eye(3), reverse=True))
        result[shape] = plan
    return result
