#!/usr/bin/env python3
"""Generate and audit 360-degree foliage across local tree assets."""
import argparse
import hashlib
import json
import math
import pathlib
import re
import struct

import numpy as np
import zstandard

from assemble_freecamera_oak import components, world_positions
from freecamera_trees import Tree, resource_sizes, sarc_members, string, u16, u32, u64
from generate_round_oak import ROOT, build_buffers, finish, make_plan, model_shapes
from tree_crown_surfaces import crown_closure, snow_surface
from tree_snow_deposits import support_mesh
from generate_round_oak import rotation_between
from tree_art_direction import snow_branch_plans


def coverage(tree, model, shape):
    points = np.array(world_positions(tree, dict(model_offset=model, shape=shape, vertex=u64(tree.data, shape+16))))
    triangles = np.array(tree.triangles(shape))
    if len(triangles) == 0:
        return 1
    normals = np.cross(points[triangles[:, 1]]-points[triangles[:, 0]], points[triangles[:, 2]]-points[triangles[:, 0]])
    areas = [np.maximum(0, normals@np.array((math.sin(a), 0, math.cos(a)))).sum()
             for a in np.arange(8)*math.pi/4]
    return float(min(areas)/max(.001, max(areas)))


def spin_plan(tree, model, shape, copies):
    points = np.array(world_positions(tree, dict(model_offset=model, shape=shape, vertex=u64(tree.data, shape+16))))
    plan = []
    for turn in range(copies):
        angle = turn*math.tau/copies
        c, s = math.cos(angle), math.sin(angle)
        rotation = np.array(((c, 0, s), (0, 1, 0), (-s, 0, c)))
        for triangles in components(tree.triangles(shape)):
            used = sorted({i for tri in triangles for i in tri})
            center = points[used].mean(axis=0)
            plan.append(dict(source=dict(vertices=used, triangles=triangles), center=rotation@center, rotation=rotation))
    return plan


def two_sided(tree, model, shape):
    """Retain the authored arrangement; add reversed faces with reversed normals."""
    d, v = tree.data, u64(tree.data, shape+16)
    count = u32(d, v+80)
    buffers = []
    for index, (start, size, stride) in enumerate(tree.buffers(v)):
        original = d[start:start+count*stride]
        reverse = bytearray(original)
        for name, fmt, offset, buffer in tree.attributes(v):
            if buffer != index:
                continue
            for i in range(count):
                at = i*stride+offset
                if name == '_n0':
                    if fmt != 0x20e:
                        raise ValueError(f'Unsupported foliage normal format: {fmt:x}')
                    value = u32(reverse, at)
                    packed = value & 0xc0000000
                    for shift in (0, 10, 20):
                        n = value >> shift & 1023
                        n = n-1024 if n & 512 else n
                        packed |= (max(-511, min(511, -n)) & 1023) << shift
                    struct.pack_into('<I', reverse, at, packed)
                elif name == '_t0' and fmt == 0x20b:
                    handedness = struct.unpack_from('<b', reverse, at+3)[0]
                    struct.pack_into('<b', reverse, at+3, max(-127, min(127, -handedness)))
        buffers.append((original+reverse, stride))
    triangles = tree.triangles(shape)
    triangles = triangles+[(c+count, b+count, a+count) for a, b, c in triangles]
    expected = world_positions(tree, dict(model_offset=model, shape=shape, vertex=v))
    return buffers, triangles, np.array(expected+expected)


def foliage_name(name):
    material = name.split('__')[-1]
    return bool(re.search('Leaf|Bloom|Pineneedle', material)) and 'Shadow' not in material


def generate(tree, asset_name):
    d = tree.data
    payload, changes, reports, plans, supports = bytearray(), [], [], {}, {}
    for model, name, shapes in model_shapes(tree):
        if 'Wreath' in asset_name:
            continue
        family = name.removesuffix('Node')
        authored = snow_branch_plans(tree, model, name, shapes) if 'Snow' in asset_name else {}
        broadleaf = ('Oak' in asset_name or 'TreeHuge' in asset_name)
        bamboo = 'Bamboo' in asset_name
        named_tree = bool(re.search('tree', asset_name, re.I)) and 'Wreath' not in asset_name
        leaves = [s for s in shapes if foliage_name(string(d, u64(d, s+8))) and
                  (named_tree or any(x in string(d, u64(d, s+8)) for x in ('Tree', 'Mangrove')))]
        backs = [s for s in shapes if any(x in string(d, u64(d, s+8)).split('__')[-1] for x in ('LeafBack', 'LeafSph', 'SakuraBack', 'LeafCover'))]
        for shape in shapes:
            sn = string(d, u64(d, shape+8))
            material = sn.split('__')[-1]
            if 'Shadow' in material or (shape in backs and shape not in authored):
                continue
            is_leaf = shape in leaves
            is_snow = material == 'mWinterSnow' and asset_name.startswith('PltTree')
            if not is_leaf and not is_snow:
                continue
            bias = coverage(tree, model, shape)
            mode = 'rear_faces'
            if shape in authored:
                mode = 'authored_snow_branch'
            elif is_snow:
                if bias >= .65:
                    reports.append(dict(model=name, shape=sn, action='retained', reason='Snow already surrounds crown', directional_area_ratio=bias))
                    continue
                mode = 'supported_snow' if broadleaf or bamboo else 'rotated_snow'
            elif 'TreeHuge' in asset_name:
                mode = 'native_vine_sectors'
            elif broadleaf and 'Stump' not in name and len(components(tree.triangles(shape))) > 5:
                mode = 'layered_crown'
            elif bamboo and bias < .5 and 'Stump' not in name:
                mode = 'layered_branches' if 'Snow' in asset_name and len(components(tree.triangles(shape)))>5 else 'radial_branches'
            elif bias < .12 and len(components(tree.triangles(shape))) <= 5:
                mode = 'crossed_sapling'
            if mode == 'authored_snow_branch':
                buffers, triangles, expected = build_buffers(tree, model, shape, authored[shape])
                plan_count = len(authored[shape])
            elif mode == 'supported_snow':
                buffers,triangles,expected=snow_surface(tree,model,shape,asset_name,supports.get(model))
                plan_count=0
            elif mode == 'rear_faces':
                buffers, triangles, expected = two_sided(tree, model, shape)
                plan_count = len(components(tree.triangles(shape)))
            else:
                key = (family, sn)
                if key not in plans:
                    if mode in ('layered_crown','layered_branches'):
                        density=3 if bamboo or 'Sakura' in asset_name or asset_name.startswith('Idr') else 4
                        plans[key] = make_plan(tree, model, shape, density, .95, 17)
                        if not bamboo:
                            # Spend part of the same cluster budget on branch
                            # interiors, rather than leaving every tier as a ring.
                            source_points=np.array(world_positions(tree,dict(vertex=u64(d,shape+16),shape=shape,model_offset=model)))
                            ceiling=float(source_points[:,1].max())
                            ys=np.array([item['source']['center'][1] for item in plans[key]])
                            split=float(ys.min()+(ys.max()-ys.min())*.60)
                            for index,item in enumerate(plans[key]):
                                if index%(2 if 'Snow' in asset_name else 4):continue
                                angle=math.atan2(item['center'][0],item['center'][2])
                                item['center'][[0,2]]*=.08+.65*math.sqrt(((index*7)%23)/22)
                                item['rotation']=rotation_between(item['source']['normal'],(math.sin(angle)*.5,.9,math.cos(angle)*.5))
                                relative=(source_points[item['source']['vertices']]-item['source']['center'])@item['rotation'].T
                                tier=ys[ys<split] if item['center'][1]<split else ys[ys>=split]
                                item['center'][1]=min(max(item['center'][1],float(np.quantile(tier,.85))),ceiling-float(relative[:,1].max())-.15)
                        if 'Sakura' in asset_name:
                            for item in plans[key]:
                                angle=math.atan2(item['center'][0],item['center'][2])
                                wobble=1+.07*math.sin(3*angle+.6)+.035*math.sin(5*angle+item['center'][1]*.3)
                                item['center'][[0,2]]*=wobble
                        plans[key] += crown_closure(tree,model,shape)
                    else:
                        plans[key] = spin_plan(tree, model, shape, 2 if is_snow else 3)
                buffers, triangles, expected = build_buffers(tree, model, shape, plans[key])
                plan_count = len(plans[key])
            if is_leaf:
                supports[model]=support_mesh(tree,u64(d,shape+16),buffers,expected,triangles,material)
            payload.extend(b'\0'*(-len(payload)%8))
            index_offset = len(payload)
            flat = [i for tri in triangles for i in tri]
            payload.extend(struct.pack(f'<{len(flat)}H', *flat))
            payload.extend(b'\0'*(-len(payload)%8))
            vertex_offset = len(payload)
            for raw, _ in buffers:
                payload.extend(b'\0'*(-len(payload)%8))
                payload.extend(raw)
            disable = backs if broadleaf and mode == 'layered_crown' else []
            changes.append(dict(model=model, shape=shape, v=u64(d, shape+16), buffers=buffers,
                triangles=triangles, expected=expected, index_offset=index_offset, vertex_offset=vertex_offset, backs=disable))
            reports.append(dict(model=name, shape=sn, action=mode, clusters=plan_count,
                vertices_before=u32(d, u64(d, shape+16)+80), vertices_after=len(expected), triangles_after=len(triangles),
                disabled_shapes=[string(d, u64(d, s+8)) for s in disable], directional_area_ratio=bias))
    if not changes:
        return d, reports
    return finish(tree, changes, payload), reports


def discover(root, force=False):
    cache = ROOT/'private/freecamera/tree-content-candidates.json'
    if cache.exists() and not force:
        return json.loads(cache.read_text())
    names, scanned = [], 0
    for path in root.glob('*.zs'):
        if not path.name.startswith(('Idr','Plt','Ftr','Fld','Obj')) or '_Tex_' in path.name or 'Anim' in path.name:
            continue
        data = zstandard.ZstdDecompressor().decompress(path.read_bytes())
        scanned += 1
        if b'FSHP' in data and re.search(rb'(?:Tree|tree|Cedar|Palm|Bamboo|Mangrove)[A-Za-z0-9_]{0,40}(?:Leaf|Bloom)', data):
            names.append(path.name)
        if scanned % 1000 == 0:
            print(f'Discovery: scanned {scanned} resources', flush=True)
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(names, indent=2)+'\n')
    return names


def candidate_paths(root, force_scan=False):
    names = set(discover(root, force_scan))
    for p in root.glob('*.zs'):
        if p.name.startswith('PltTree') and 'Anim' not in p.name:
            names.add(p.name)
        if p.name.startswith(('Idr','Ftr')) and re.search('tree', p.name, re.I) and not re.search('street|_Tex_|Anim|ReBody', p.name, re.I):
            names.add(p.name)
    return [root/n for n in sorted(names)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, default=ROOT/'private/freecamera/forest-v1')
    parser.add_argument('--scan', action='store_true', help='Refresh the content scan rather than using its local cache')
    args = parser.parse_args()
    if not args.out.resolve().is_relative_to(ROOT/'private') or args.out.resolve().is_relative_to(ROOT/'private/game'):
        parser.error('Choose an output directory under private/ outside the game folder')
    root = ROOT/'private/game/romfs/Model'
    rows, growth = [], {}
    for path in candidate_paths(root, args.scan):
        archive = zstandard.ZstdDecompressor().decompress(path.read_bytes())
        if archive[:4] != b'SARC':
            rows.append(dict(file=path.name, status='retained', reason='Not a model archive'))
            continue
        members = list(sarc_members(archive))
        fres = [(n, s, e) for n, s, e in members if archive[s:s+4] == b'FRES']
        if not fres:
            rows.append(dict(file=path.name, status='retained', reason='Texture/material variant; no model geometry'))
            continue
        if len(members) != 1:
            raise ValueError(f'{path.name}: support multi-member archives before modifying')
        _, start, end = fres[0]
        try:
            replacement, report = generate(Tree(archive[start:end]), path.name)
        except Exception as e:
            raise RuntimeError(f'{path.name}: {e}') from e
        changed = replacement != archive[start:end]
        row = dict(file=path.name, status='modified' if changed else 'retained', shapes=report,
                   source_sha256=hashlib.sha256(archive).hexdigest())
        if changed:
            output = bytearray(archive[:start]+replacement+archive[end:])
            struct.pack_into('<I', output, 8, len(output))
            struct.pack_into('<I', output, u16(output, 4)+24, len(replacement))
            target = args.out/'romfs/Model'/path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zstandard.ZstdCompressor(level=12).compress(output))
            growth[path.name] = max(0, len(output)-len(archive))
            row['output_sha256'] = hashlib.sha256(output).hexdigest()
        else:
            row['reason'] = 'No directional foliage surfaces; retained original geometry'
        rows.append(row)
        print(path.name, row['status'], len(report), flush=True)
    rst = pathlib.Path('System/Resource/ResourceSizeTable.srsizetable')
    packed, heaps = resource_sizes(ROOT/'private/game/romfs'/rst, growth)
    target = args.out/'romfs'/rst
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(packed)
    args.out.joinpath('report.json').write_text(json.dumps(dict(assets=rows, heap_changes=heaps), indent=2)+'\n')
    print(f'Completed {len(rows)} assets, {len(growth)} modified')


if __name__ == '__main__':
    main()
