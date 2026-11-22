"""Fruit origins matching the reviewed canopy layouts (game model units)."""
import numpy as np
import copy
import re

from tree_art_direction import OAK_BRANCHES, OAK_PIVOTS, yaw

# ACNH 3.0.3 main+0x40746a8 (palm) and +0x40746c0 (oak).
# Verified against live peach render-instance positions and all table readers.
STOCK_OAK = [(-6., 16.8, 6.2), (6.2, 16., 6.9), (0., 24.5, 3.)]
STOCK_PALM = [(-2.5, 15.5, 3.2), (2.6, 16.3, 2.3)]


def layouts():
    # The layered oak/blossom fronts now extend farther toward +Z. Keep the
    # original hanging heights and move the stems to the front leaf fringes.
    oak = [(-6., 16.8, 9.4), (6.2, 16., 9.6), (0., 24.5, 5.6)]
    snow = []
    for lobe, old in zip(('left', 'right', 'top'), STOCK_OAK):
        _, x, z, rise, turn = next(b for b in OAK_BRANCHES if b[0] == lobe)
        point = np.array((x, rise, z))+yaw(turn)@(np.array(old)-OAK_PIVOTS[lobe])
        snow.append(point.tolist())
    return dict(oak=oak, sakura=oak, snow=snow, palm=STOCK_PALM)


def write_profiles(path):
    data = layouts()
    lines = ['ACNH_TREE_FRUIT_V1']
    for name, points in data.items():
        lines.append(name+' '+' '.join(f'{v:.7g}' for point in points for v in point))
    path.write_text('\n'.join(lines)+'\n')


def attach_preview(asset, fruit_assets, revised=False):
    """Add actual fruit meshes at verified hanging origins; no invented motion."""
    for name, model in asset['models'].items():
        match=re.fullmatch(r'PltTree(Oak|Palm)4(Snow|Sakura)?(?:Node)?',name)
        if not match: continue
        family, season=match.groups()
        profile='palm' if family=='Palm' else 'snow' if season=='Snow' else 'sakura' if season=='Sakura' else 'oak'
        points=layouts()[profile] if revised else STOCK_PALM if family=='Palm' else STOCK_OAK
        for species, fruit in fruit_assets.items():
            if (species=='Coconut') != (family=='Palm'):continue
            asset['textures'].update(fruit['textures'])
            for slot, point in enumerate(points):
                for original in next(iter(fruit['models'].values()))['shapes']:
                    shape=copy.deepcopy(original)
                    shape['name']=f'Fruit{species}{slot}__'+shape['material']
                    shape['fruitSpecies']=species
                    shape['positions']=(np.array(shape['positions'])+point).tolist()
                    shape['bones']=[0]*len(shape['positions'])
                    model['shapes'].append(shape)
