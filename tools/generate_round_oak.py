#!/usr/bin/env python3
"""Generate a local 360-degree oak foliage candidate and its comparison viewer."""
import argparse
import hashlib
import json
import math
import pathlib
import struct
import subprocess
import sys

import numpy as np
import zstandard

from assemble_freecamera_oak import append_gpu, components, world_positions
from freecamera_trees import Tree, resource_sizes, sarc_members, string, u16, u32, u64

ROOT = pathlib.Path(__file__).resolve().parents[1]


def rotation_between(source, target):
    a, b = np.asarray(source, dtype=float), np.asarray(target, dtype=float)
    a /= np.linalg.norm(a)
    b /= np.linalg.norm(b)
    cross, dot = np.cross(a, b), float(np.dot(a, b))
    if dot < -0.999999:
        axis = np.cross(a, (1, 0, 0) if abs(a[0]) < .9 else (0, 1, 0))
        axis /= np.linalg.norm(axis)
        return 2*np.outer(axis, axis)-np.eye(3)
    x, y, z = cross
    skew = np.array(((0, -z, y), (z, 0, -x), (-y, x, 0)))
    return np.eye(3)+skew+skew@skew/max(1e-12, 1+dot)


def model_shapes(tree):
    d, cursor = tree.data, 0
    while (model := d.find(b'FMDL', cursor)) >= 0:
        cursor = model+4
        name = string(d, u64(d, model+8))
        shapes = [u64(d, model+40)+96*i for i in range(u16(d, model+106))]
        yield model, name, shapes


def make_plan(tree, model, shape, density, depth, seed, style='layered'):
    v = u64(tree.data, shape+16)
    points = np.array(world_positions(tree, dict(vertex=v, shape=shape, model_offset=model)))
    cards = []
    for group in components(tree.triangles(shape)):
        used = sorted({i for tri in group for i in tri})
        center = points[used].mean(axis=0)
        normal = sum((np.cross(points[b]-points[a], points[c]-points[a]) for a, b, c in group), np.zeros(3))
        if np.linalg.norm(normal) < 1e-6:
            raise ValueError('Degenerate leaf cluster')
        cards.append(dict(vertices=used, triangles=group, center=center, normal=normal))
    centers = np.array([c['center'] for c in cards])
    low, high = centers.min(axis=0), centers.max(axis=0)
    center = (low+high)/2
    # Use the stock height/width; replace its shallow depth with a rounded crown.
    radii = (high-low)/2
    radii[2] = radii[0]*depth
    rng = np.random.default_rng(seed)
    count = round(len(cards)*density)
    order = rng.permutation(len(cards))
    plan = []
    for j in range(count):
        card = cards[order[j % len(cards)]]
        azimuth = j*math.pi*(3-math.sqrt(5))
        if style == 'sphere':
            y = 1-2*(j+.5)/count
            direction = np.array((math.sqrt(1-y*y)*math.sin(azimuth), y,
                                  math.sqrt(1-y*y)*math.cos(azimuth)))
            target = center+radii*direction
            rotation = rotation_between(card['normal'], direction/radii)
        else:
            # Revolve the authored tier silhouette; keep each cluster's height,
            # droop, scale, UVs, and internal shape instead of making a sphere.
            y = card['center'][1]
            band = np.abs(centers[:, 1]-y) < max(.001,(high[1]-low[1])*.065)
            radius = float(np.quantile(np.abs(centers[band, 0]-center[0]), .9))
            radius *= float(rng.uniform(.87, 1.03))
            target = np.array((center[0]+radius*math.sin(azimuth), y,
                               center[2]+radius*math.cos(azimuth)*depth))
            turn = azimuth-math.atan2(card['normal'][0], card['normal'][2])
            c, s = math.cos(turn), math.sin(turn)
            rotation = np.array(((c, 0, s), (0, 1, 0), (-s, 0, c)))
        plan.append(dict(source=card, center=target, rotation=rotation))
    return plan


def build_buffers(tree, model, shape, plan):
    d, v = tree.data, u64(tree.data, shape+16)
    points = np.array(world_positions(tree, dict(vertex=v, shape=shape, model_offset=model)))
    transforms, indices = tree.rig(model, shape, v)
    # Smooth-skinned bind-pose vertices are already in model space. Keep their
    # original weights/palette; rigid vertices need their bone-local conversion.
    if indices is None:
        transforms, indices = [(np.eye(3), np.zeros(3))], [0]*len(points)
    buffers = list(tree.buffers(v))
    output = [bytearray() for _ in buffers]
    triangles, expected, count = [], [], 0
    for item in plan:
        card, rotation, target = item['source'], item['rotation'], item['center']
        reverse = item.get('reverse', False)
        used = card['vertices']
        source_center = points[used].mean(axis=0)
        remap = {old: count+j for j, old in enumerate(used)}
        # Transform each entire connected cluster rigidly, including its wind pivots.
        for old in used:
            r, t = transforms[indices[old]]
            r, t = np.array(r), np.array(t)
            local_rotation = r.T@rotation@r
            world = target+rotation@(points[old]-source_center)
            expected.append(world)
            records = [bytearray(d[start+old*stride:start+(old+1)*stride]) for start, _, stride in buffers]
            for attr, fmt, offset, buffer in tree.attributes(v):
                raw = records[buffer]
                if attr == '_p0':
                    struct.pack_into('<3f', raw, offset, *(r.T@(world-t)))
                elif attr == '_c0' and fmt == 0x515:
                    x, y, z, weight = struct.unpack_from('<4e', raw, offset)
                    pivot = target+rotation@(np.array((x, y, z))-source_center)
                    struct.pack_into('<4e', raw, offset, *pivot, weight)
                elif attr == '_n0' and fmt == 0x20e:
                    value = u32(raw, offset)
                    normal = [(value >> k & 1023)-1024 if value >> k & 512 else value >> k & 1023 for k in (0, 10, 20)]
                    packed = value & 0xc0000000
                    for k, n in zip((0, 10, 20), (local_rotation@normal)*(-1 if reverse else 1)):
                        packed |= (max(-511, min(511, round(n))) & 1023) << k
                    struct.pack_into('<I', raw, offset, packed)
                elif attr == '_t0' and fmt == 0x20b:
                    tangent = local_rotation@struct.unpack_from('<3b', raw, offset)
                    struct.pack_into('<3b', raw, offset, *(max(-127, min(127, round(x))) for x in tangent))
                    if reverse:
                        handedness = struct.unpack_from('<b', raw, offset+3)[0]
                        struct.pack_into('<b', raw, offset+3, max(-127, min(127, -handedness)))
            for raw, dest in zip(records, output):
                dest.extend(raw)
        triangles.extend(tuple(remap[i] for i in (tri[::-1] if reverse else tri)) for tri in card['triangles'])
        count += len(used)
    if count >= 65536:
        raise ValueError('Candidate exceeds uint16 vertex index range')
    return [(bytes(raw), stride) for raw, (_, _, stride) in zip(output, buffers)], triangles, np.array(expected)


def generate(tree, density=3, depth=.95, seed=17, style='layered'):
    d = tree.data
    plans, changes, report, payload = {}, [], [], bytearray()
    for model, name, shapes in model_shapes(tree):
        family = name.removesuffix('Node')
        if family not in ('PltTreeOak3', 'PltTreeOak4'):
            continue
        fronts = [s for s in shapes if string(d, u64(d, s+8)).endswith('__mTreeOakLeaf')]
        backs = [s for s in shapes if 'LeafBack' in string(d, u64(d, s+8))]
        if len(fronts) != 1 or len(backs) != 1:
            raise ValueError('Unexpected oak foliage layout')
        shape = fronts[0]
        if family not in plans:
            plans[family] = make_plan(tree, model, shape, density, depth, seed, style)
        plan = plans[family]
        buffers, triangles, expected = build_buffers(tree, model, shape, plan)
        v = u64(d, shape+16)
        payload.extend(b'\0'*(-len(payload)%8))
        index_offset = len(payload)
        flat = [i for tri in triangles for i in tri]
        payload.extend(struct.pack(f'<{len(flat)}H', *flat))
        payload.extend(b'\0'*(-len(payload)%8))
        vertex_offset = len(payload)
        for raw, _ in buffers:
            payload.extend(b'\0'*(-len(payload)%8))
            payload.extend(raw)
        changes.append(dict(model=model, shape=shape, v=v, buffers=buffers, triangles=triangles,
                            index_offset=index_offset, vertex_offset=vertex_offset, expected=expected, backs=backs))
        report.append(dict(model=name, leaf_clusters=len(plan), vertices=len(expected), triangles=len(triangles)))
    return finish(tree, changes, payload), report


def finish(tree, changes, payload):
    """Write replacement buffers and validate unchanged shapes/resources."""
    d = tree.data
    out, start = append_gpu(d, bytes(payload))
    for change in changes:
        shape, v = change['shape'], change['v']
        mesh = u64(d, shape+24)
        count = len(change['triangles'])*3
        struct.pack_into('<I', out, v+72, start+change['vertex_offset']-tree.gpu)
        struct.pack_into('<I', out, v+80, len(change['expected']))
        for i, (raw, _) in enumerate(change['buffers']):
            struct.pack_into('<I', out, u64(d, v+48)+16*i, len(raw))
        struct.pack_into('<I', out, mesh+32, start+change['index_offset']-tree.gpu)
        struct.pack_into('<I', out, mesh+44, count)
        struct.pack_into('<I', out, mesh+48, 0)
        struct.pack_into('<I', out, u64(d, mesh+24), count*2)
        if u16(d, mesh+52) != 1:
            raise ValueError('Expected one leaf submesh')
        struct.pack_into('<II', out, u64(d, mesh), 0, count)
        for back in change['backs']:
            bm = u64(d, back+24)
            struct.pack_into('<I', out, bm+44, 0)
            for i in range(u16(d, bm+52)):
                struct.pack_into('<I', out, u64(d, bm)+8*i+4, 0)
        positions = np.array(Tree(bytes(out)).positions(v))
        low, high = positions.min(axis=0), positions.max(axis=0)
        bounds = [*((low+high)/2), *((high-low)/2)]
        for i in range(2):
            struct.pack_into('<6f', out, u64(d, shape+56)+24*i, *bounds)
        struct.pack_into('<f', out, u64(d, shape+64), float(np.linalg.norm(positions, axis=1).max()))
    final = Tree(bytes(out)).compact_gpu()
    result = Tree(final)
    changed_shapes = {c['shape'] for c in changes} | {s for c in changes for s in c['backs']}
    for change in changes:
        pose = world_positions(result, dict(vertex=change['v'], shape=change['shape'], model_offset=change['model']))
        if np.max(np.abs(np.array(pose)-change['expected'])) > .001:
            raise ValueError('Generated geometry differs from its planned bind pose')
        assert result.triangles(change['shape']) == change['triangles']
    for shape, _, v in tree.shapes():
        if shape in changed_shapes:
            continue
        assert result.triangles(shape) == tree.triangles(shape)
        for (a, n, _), (b, size, _) in zip(tree.buffers(v), result.buffers(v)):
            assert size == n and d[a:a+n] == final[b:b+size]
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--density', type=float, default=3)
    parser.add_argument('--depth', type=float, default=.95, help='Canopy depth relative to width')
    parser.add_argument('--seed', type=int, default=17)
    parser.add_argument('--style', choices=('layered', 'sphere'), default='layered')
    parser.add_argument('--out', type=pathlib.Path, default=ROOT/'private/freecamera/round-oak-next')
    parser.add_argument('--preview', action='store_true')
    parser.add_argument('--render', action='store_true', help='Render an eight-angle stock/candidate contact sheet')
    args = parser.parse_args()
    if not args.out.resolve().is_relative_to(ROOT/'private'):
        parser.error('Keep generated models under private/')
    if not .5 <= args.density <= 8 or not .4 <= args.depth <= 1.4:
        parser.error('Use density 0.5–8 and depth 0.4–1.4')
    source = ROOT/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs'
    archive = zstandard.ZstdDecompressor().decompress(source.read_bytes())
    members = list(sarc_members(archive))
    if len(members) != 1:
        raise ValueError('Expected one oak model archive member')
    _, start, end = members[0]
    replacement, report = generate(Tree(archive[start:end]), args.density, args.depth, args.seed, args.style)
    output = bytearray(archive[:start]+replacement+archive[end:])
    struct.pack_into('<I', output, 8, len(output))
    struct.pack_into('<I', output, u16(output, 4)+24, len(replacement))
    target = args.out/'romfs/Model'/source.name
    if target.resolve() == source.resolve():
        parser.error('Output must not overwrite stock models')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(zstandard.ZstdCompressor(level=12).compress(output))
    rst = pathlib.Path('System/Resource/ResourceSizeTable.srsizetable')
    packed, sizes = resource_sizes(ROOT/'private/game/romfs'/rst, {source.name: max(0, len(output)-len(archive))})
    table = args.out/'romfs'/rst
    table.parent.mkdir(parents=True, exist_ok=True)
    table.write_bytes(packed)
    args.out.joinpath('report.json').write_text(json.dumps(dict(density=args.density, depth=args.depth, seed=args.seed, style=args.style,
        models=report, resource_sizes=sizes, source_sha256=hashlib.sha256(archive).hexdigest(),
        output_sha256=hashlib.sha256(output).hexdigest()), indent=2)+'\n')
    for item in report:
        print(item)
    if args.preview:
        subprocess.run([sys.executable, str(ROOT/'tools/preview_trees.py'), '--after', str(target),
                        '--output', str(args.out/'preview.html')], check=True)
    if args.render:
        subprocess.run([sys.executable, str(ROOT/'tools/render_tree_views.py'), '--after', str(target),
                        '--output', str(args.out/'turnaround.png')], check=True)


if __name__ == '__main__':
    main()
