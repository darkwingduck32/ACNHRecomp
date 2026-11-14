#!/usr/bin/env python3
"""Local mature-oak prototype: stock front plus a budgeted rear in the LeafBack slot."""
import argparse
import hashlib
import json
import math
import pathlib
import struct

import zstandard

from freecamera_trees import Tree, add, subtract, mm, mv, transpose, resource_sizes, sarc_members, string, u16, u32, u64


def components(triangles):
    parents = {i: i for tri in triangles for i in tri}
    def root(i):
        while parents[i] != i:
            parents[i] = parents[parents[i]]
            i = parents[i]
        return i
    for a, b, c in triangles:
        parents[root(b)] = root(a)
        parents[root(c)] = root(a)
    groups = {}
    for tri in triangles:
        groups.setdefault(root(tri[0]), []).append(tri)
    return list(groups.values())


def append_gpu(data, payload):
    """Temporarily extend the vertex region; compact_gpu later sorts buffers."""
    rlt = u32(data, 24)
    sections = [struct.unpack_from('<Q4I', data, rlt+16+24*i) for i in range(5)]
    end = sections[2][1]+sections[2][2]
    payload += b'\0'*(-len(payload)%4096)
    delta = len(payload)
    mapped = lambda p: p+(delta if p >= end else 0)
    out = bytearray(data[:end]+payload+data[end:])
    entries = rlt+16+24*5
    for i in range(max(s[3]+s[4] for s in sections)):
        p = entries+8*i
        pos, structs, offsets, padding = struct.unpack_from('<IHBB', data, p)
        for j in range(structs):
            for k in range(offsets):
                location = pos+8*(j*(offsets+padding)+k)
                value = u64(data, location)
                if value:
                    struct.pack_into('<Q', out, mapped(location), mapped(value))
        struct.pack_into('<I', out, mapped(p), mapped(pos))
    for i, (base, start, size, first, count) in enumerate(sections):
        struct.pack_into('<Q4I', out, mapped(rlt)+16+24*i, base, mapped(start), size+(delta if i == 2 else 0), first, count)
    struct.pack_into('<I', out, 24, mapped(rlt))
    struct.pack_into('<I', out, 28, len(out))
    struct.pack_into('<I', out, mapped(rlt)+4, mapped(rlt))
    info = u64(data, 0xb0)
    struct.pack_into('<I', out, info+4, u32(data, info+4)+delta)
    return out, end


def world_positions(tree, edit):
    points = tree.positions(edit['vertex'])
    transforms, indices = tree.rig(edit['model_offset'], edit['shape'], edit['vertex'])
    if indices is not None:
        points = [add(mv(transforms[indices[i]][0], point), transforms[indices[i]][1]) for i, point in enumerate(points)]
    return points


def choose_rear(tree, edit, budget):
    """Select complete cards covering the rear silhouette, with a protected side join."""
    points = world_positions(tree, edit)
    triangles = tree.triangles(edit['shape'])[edit['index_count']//3:]
    groups = components(triangles)
    xmin, xmax = min(p[0] for p in points), max(p[0] for p in points)
    ymin, ymax = min(p[1] for p in points), max(p[1] for p in points)
    candidates = []
    for group in groups:
        vertices = sorted({i for tri in group for i in tri})
        center = [sum(points[i][k] for i in vertices)/len(vertices) for k in range(3)]
        # Keep rear cards away from the original side silhouette. No card edges
        # are cut, and no center-plane duplicate layer is added.
        if center[2] > -(xmax-xmin)*.08 or max(points[i][2] for i in vertices) > .25:
            continue
        cell = (min(6, int(7*(center[0]-xmin)/max(.001, xmax-xmin))),
                min(4, int(5*(center[1]-ymin)/max(.001, ymax-ymin))))
        candidates.append((cell, -center[2], group, vertices, center))
    selected, used, cells = [], set(), set()
    for cell, _, group, vertices, center in sorted(candidates, key=lambda item: -item[1]):
        if cell in cells or len(used | set(vertices)) > budget:
            continue
        cells.add(cell)
        used.update(vertices)
        selected.append((group, center, cell))
    if not selected:
        raise ValueError('No rear cards satisfy the prototype coverage/budget constraints')
    return [tri for group, _, _ in selected for tri in group], sorted(used), [dict(center=center, cell=cell,
        vertices=sorted({i for tri in group for i in tri})) for group, center, cell in selected]


def radial_cards(tree, edit, used, selected, raw_buffers):
    """Orient rear leaf cards radially; keep wind lever arms in the same frame."""
    attrs = {a[0]: a for a in tree.attributes(edit['vertex'])}
    transforms, bones = tree.rig(edit['model_offset'], edit['shape'], edit['vertex'])
    points = world_positions(tree, edit)
    remap = {old: new for new, old in enumerate(used)}
    out = [(bytearray(raw), stride) for raw, stride in raw_buffers]
    normal = attrs['_n0']
    for card in selected:
        center = tuple(card['center'])
        if 'turn_radians' not in card:
            n = [0, 0, 0]
            for old in card['vertices']:
                index = remap[old]
                _, _, offset, buffer = normal
                raw, stride = out[buffer]
                value = u32(raw, index*stride+offset)
                xyz = [(value >> shift) & 1023 for shift in (0, 10, 20)]
                xyz = [x-1024 if x & 512 else x for x in xyz]
                r = transforms[bones[old]][0]
                direction = mv(r, xyz)
                n = add(n, direction)
            angle = math.atan2(center[0], center[2])-math.atan2(n[0], n[2])
            card['turn_radians'] = angle if math.hypot(n[0], n[2]) > 1 else 0
        angle = card['turn_radians']
        c, s = math.cos(angle), math.sin(angle)
        rotation = ((c, 0, s), (0, 1, 0), (-s, 0, c))
        for old in card['vertices']:
            index = remap[old]
            r, t = transforms[bones[old]]
            local_rotation = mm(transpose(r), mm(rotation, r))
            target_point = add(center, mv(rotation, subtract(points[old], center)))
            for attr, fmt, offset, buffer in attrs.values():
                raw, stride = out[buffer]
                at = index*stride+offset
                if attr == '_p0':
                    struct.pack_into('<3f', raw, at, *mv(transpose(r), subtract(target_point, t)))
                elif attr == '_c0' and fmt == 0x0515:
                    x, y, z, weight = struct.unpack_from('<4e', raw, at)
                    pivot = add(center, mv(rotation, subtract((x, y, z), center)))
                    struct.pack_into('<4e', raw, at, *pivot, weight)
                    rounded = struct.unpack_from('<4e', raw, at)
                    arm = mv(rotation, subtract(points[old], (x, y, z)))
                    assert max(abs(target_point[k]-rounded[k]-arm[k]) for k in range(3)) < .04
                elif attr == '_n0' and fmt == 0x020e:
                    value = u32(raw, at)
                    xyz = [(value >> shift) & 1023 for shift in (0, 10, 20)]
                    xyz = [x-1024 if x & 512 else x for x in xyz]
                    value &= 0xc0000000
                    for shift, x in zip((0, 10, 20), mv(local_rotation, xyz)):
                        value |= (max(-511, min(511, round(x))) & 1023) << shift
                    struct.pack_into('<I', raw, at, value)
                elif attr == '_t0' and fmt == 0x020b:
                    xyz = struct.unpack_from('<3b', raw, at)
                    for k, x in enumerate(mv(local_rotation, xyz)):
                        struct.pack_into('<b', raw, at+k, max(-127, min(127, round(x))))
    return [(bytes(raw), stride) for raw, stride in out]


def assemble(tree):
    generated, edits = tree.fill_rear(0, only_models={'PltTreeOak3', 'PltTreeOak4'})
    temporary = Tree(generated)
    payload, changes, report = bytearray(), [], []
    rear_plans = {}
    for edit in edits:
        front = edit['shape']
        model = edit['model_offset']
        shapes = [u64(tree.data, model+40)+96*i for i in range(u16(tree.data, model+106))]
        backs = [p for p in shapes if 'LeafBack' in string(tree.data, u64(tree.data, p+8))]
        if len(backs) != 1:
            raise ValueError('Expected one original underside shape')
        back = backs[0]
        back_v = u64(tree.data, back+16)
        old_back_count = u32(tree.data, back_v+80)
        family = edit['model'].removesuffix('Node')
        if family not in rear_plans:
            rear_triangles, used, selected = choose_rear(temporary, edit, old_back_count)
            rear_plans[family] = (rear_triangles, used, selected, temporary.triangles(front))
        else:
            rear_triangles, used, selected, topology = rear_plans[family]
            if temporary.triangles(front) != topology:
                raise ValueError('Normal and collision foliage topology differ')
        remap = {old: new for new, old in enumerate(used)}
        rear_indices = [remap[i] for tri in rear_triangles for i in tri]
        front_indices = [i for tri in tree.triangles(front) for i in tri]
        source_v = edit['vertex']
        front_buffers = [(tree.data[p:p+size], stride) for p, size, stride in tree.buffers(source_v)]
        rear_buffers = [(b''.join(generated[p+i*stride:p+(i+1)*stride] for i in used), stride)
                        for p, _, stride in temporary.buffers(source_v)]
        rear_buffers = radial_cards(temporary, edit, used, selected, rear_buffers)
        for target, v, count, buffers, indices, rear in (
                (front, source_v, u32(tree.data, source_v+80), front_buffers, front_indices, False),
                (back, back_v, len(used), rear_buffers, rear_indices, True)):
            payload.extend(b'\0'*(-len(payload)%8))
            index_offset = len(payload)
            payload.extend(struct.pack(f'<{len(indices)}H', *indices))
            payload.extend(b'\0'*(-len(payload)%8))
            buffer_offset = len(payload)
            for raw, stride in buffers:
                payload.extend(b'\0'*(-len(payload)%8))
                payload.extend(raw)
            changes.append(dict(shape=target, v=v, count=count, buffers=buffers, indices=indices,
                                index_offset=index_offset, buffer_offset=buffer_offset, rear=rear,
                                source_shape=front, source_v=source_v))
        report.append(dict(model=edit['model'], original_front_vertices=u32(tree.data, source_v+80),
                           replaced_underside_vertices=old_back_count, rear_vertices=len(used), rear_cards=selected))
    out, start = append_gpu(generated, bytes(payload))
    for change in changes:
        shape, v, sv = change['shape'], change['v'], change['source_v']
        d = tree.data
        if change['rear']:
            if d[v+76:v+78] != d[sv+76:sv+78]:
                raise ValueError('Source/rear vertex declarations differ in size')
            attributes = d[sv+76]
            a, b = u64(d, v+8), u64(d, sv+8)
            out[a:a+16*attributes] = d[b:b+16*attributes]
            out[v+84:v+88] = d[sv+84:sv+88]
            struct.pack_into('<H', out, shape+82, u16(d, change['source_shape']+82))
            if u16(d, shape+88) != u16(d, change['source_shape']+88):
                raise ValueError('Original underside uses a different bone palette size')
            bone_count = u16(d, shape+88)
            if bone_count:
                a, b = u64(d, shape+32), u64(d, change['source_shape']+32)
                out[a:a+2*bone_count] = d[b:b+2*bone_count]
        struct.pack_into('<I', out, v+72, start+change['buffer_offset']-temporary.gpu)
        struct.pack_into('<I', out, v+80, change['count'])
        for i, (raw, stride) in enumerate(change['buffers']):
            struct.pack_into('<I', out, u64(d, v+48)+16*i, len(raw))
            struct.pack_into('<I', out, u64(d, v+56)+16*i, stride)
        mesh = u64(d, shape+24)
        struct.pack_into('<I', out, mesh+32, start+change['index_offset']-temporary.gpu)
        struct.pack_into('<I', out, mesh+44, len(change['indices']))
        struct.pack_into('<I', out, u64(d, mesh+24), len(change['indices'])*2)
        struct.pack_into('<I', out, u64(d, mesh)+4, len(change['indices']))
        points = Tree(bytes(out)).positions(v)
        low, high = ([f(p[k] for p in points) for k in range(3)] for f in (min, max))
        values = [(low[k]+high[k])/2 for k in range(3)]+[(high[k]-low[k])/2 for k in range(3)]
        for i in range(2):
            struct.pack_into('<6f', out, u64(d, shape+56)+24*i, *values)
        struct.pack_into('<f', out, u64(d, shape+64), max(math.sqrt(sum(x*x for x in point)) for point in points))
    final = Tree(bytes(out)).compact_gpu()
    result = Tree(final)
    rear_poses = {}
    for change in changes:
        if not change['rear']:
            assert result.triangles(change['shape']) == tree.triangles(change['shape'])
            for (a, n, _), (b, m, _) in zip(tree.buffers(change['v']), result.buffers(change['v'])):
                assert n == m and tree.data[a:a+n] == final[b:b+m]
        else:
            source = change['source_shape']
            assert u16(final, change['shape']+82) == u16(tree.data, source+82)
            assert u32(final, change['v']+80) <= u32(tree.data, change['v']+80)
            model = next(e for e in edits if e['shape'] == source)
            description = dict(vertex=change['v'], model_offset=model['model_offset'], shape=change['shape'])
            pose = world_positions(result, description)
            family = model['model'].removesuffix('Node')
            if family in rear_poses:
                assert len(pose) == len(rear_poses[family])
                assert max(abs(a[k]-b[k]) for a, b in zip(pose, rear_poses[family]) for k in range(3)) < .001
            else:
                rear_poses[family] = pose
    modified = {change['shape'] for change in changes}
    for shape, _, v in tree.shapes():
        if shape in modified:
            continue
        assert result.triangles(shape) == tree.triangles(shape)
        for (a, n, _), (b, m, _) in zip(tree.buffers(v), result.buffers(v)):
            assert n == m and tree.data[a:a+n] == final[b:b+m]
    return final, report


def radial_shell(tree):
    """One canopy: reorient native leaf cards in place, with no added vertices."""
    d = tree.data
    out, report, plans, poses = bytearray(d), [], {}, {}
    cursor = 0
    while True:
        model = d.find(b'FMDL', cursor)
        if model < 0:
            break
        cursor = model+4
        name = string(d, u64(d, model+8))
        family = name.removesuffix('Node')
        if family not in ('PltTreeOak3', 'PltTreeOak4'):
            continue
        shapes = [u64(d, model+40)+96*i for i in range(u16(d, model+106))]
        front = [p for p in shapes if 'Leaf' in string(d, u64(d, p+8)) and
                 not any(token in string(d, u64(d, p+8)) for token in ('Back', 'Shadow'))]
        back = [p for p in shapes if 'LeafBack' in string(d, u64(d, p+8))]
        if len(front) != 1 or len(back) != 1:
            raise ValueError('Expected one foliage shape and one underside shape')
        shape = front[0]
        v = u64(d, shape+16)
        edit = dict(vertex=v, shape=shape, model_offset=model)
        points = world_positions(tree, edit)
        triangles = tree.triangles(shape)
        if family not in plans:
            cards = []
            for group in components(triangles):
                used = sorted({i for tri in group for i in tri})
                center = [sum(points[i][k] for i in used)/len(used) for k in range(3)]
                cards.append(dict(center=center, vertices=used))
            plans[family] = (cards, triangles)
        cards, topology = plans[family]
        if triangles != topology:
            raise ValueError('Collision topology differs from the normal canopy')
        buffers = list(tree.buffers(v))
        raw = [(d[p:p+size], stride) for p, size, stride in buffers]
        transformed = radial_cards(tree, edit, list(range(len(points))), cards, raw)
        for (start, size, _), (data, _) in zip(buffers, transformed):
            assert len(data) == size
            out[start:start+size] = data
        # The original opaque underside must not remain as an interior wall.
        removed = 0
        for p in back:
            mesh = u64(d, p+24)
            removed += u32(d, mesh+44)//3
            struct.pack_into('<I', out, mesh+44, 0)
            submesh = u64(d, mesh)
            for i in range(u16(d, mesh+52)):
                struct.pack_into('<I', out, submesh+8*i+4, 0)
        local = Tree(bytes(out)).positions(v)
        low, high = ([f(p[k] for p in local) for k in range(3)] for f in (min, max))
        values = [(low[k]+high[k])/2 for k in range(3)]+[(high[k]-low[k])/2 for k in range(3)]
        for i in range(2):
            struct.pack_into('<6f', out, u64(d, shape+56)+24*i, *values)
        radius = max(math.sqrt(sum(x*x for x in p)) for p in local)
        struct.pack_into('<f', out, u64(d, shape+64), radius)
        final_points = world_positions(Tree(bytes(out)), edit)
        if family in poses:
            assert max(abs(a[k]-b[k]) for a, b in zip(poses[family], final_points) for k in range(3)) < .001
        else:
            poses[family] = final_points
        # Rotating each card preserves its authored shape, rather than cutting,
        # stretching or duplicating its triangles at a front/back join.
        for a, b, c in triangles:
            for i, j in ((a, b), (b, c), (c, a)):
                before = sum((points[i][k]-points[j][k])**2 for k in range(3))
                after = sum((final_points[i][k]-final_points[j][k])**2 for k in range(3))
                assert abs(before-after) < .001
        report.append(dict(model=name, vertices=len(points), added_vertices=0, leaf_cards=len(cards),
                           foliage_triangles=len(triangles), removed_underside_triangles=removed))
    assert len(out) == len(d)
    rlt = u32(d, 24)
    _, start, size, _, _ = struct.unpack_from('<Q4I', d, rlt+16+24*4)
    assert out[start:start+size] == d[start:start+size]
    return bytes(out), report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path, default=pathlib.Path('private/freecamera/assembled-oak4'))
    parser.add_argument('--radial-shell', action='store_true', help='Reorient the native canopy in place; add no geometry')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    if not args.out.resolve().is_relative_to(root/'private'):
        parser.error('Keep game-derived output under private/')
    source = root/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs'
    archive = zstandard.ZstdDecompressor().decompress(source.read_bytes())
    members = list(sarc_members(archive))
    if len(members) != 1:
        raise ValueError('Expected one BFRES member in the oak archive')
    name, start, end = members[0]
    replacement, report = (radial_shell if args.radial_shell else assemble)(Tree(archive[start:end]))
    output = bytearray(archive[:start]+replacement+archive[end:])
    struct.pack_into('<I', output, 8, len(output))
    struct.pack_into('<I', output, u16(output, 4)+24, len(replacement))
    target = args.out/'romfs/Model'/source.name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(zstandard.ZstdCompressor(level=12).compress(output))
    # Keep every other currently installed mod allocation, growing only this oak.
    table = root/'private/game/mods/freecamera/romfs/System/Resource/ResourceSizeTable.srsizetable'
    if args.radial_shell:
        assert len(output) == len(archive)
        packed, sizes = table.read_bytes(), []
    else:
        packed, sizes = resource_sizes(table, {source.name: max(0, len(output)-len(archive))},
                                      base_table=root/'private/game/romfs/System/Resource/ResourceSizeTable.srsizetable')
    target = args.out/'romfs/System/Resource/ResourceSizeTable.srsizetable'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(packed)
    args.out.joinpath('report.json').write_text(json.dumps(dict(models=report, heap_changes=sizes,
        stock_sha256=hashlib.sha256(archive).hexdigest(), output_sha256=hashlib.sha256(output).hexdigest()), indent=2)+'\n')
    for model in report:
        if args.radial_shell:
            print(f"{model['model']}: {model['leaf_cards']} native leaf cards, {model['vertices']} vertices; "
                  f"zero added geometry, {model['removed_underside_triangles']} underside triangles removed")
        else:
            print(f"{model['model']}: stock front {model['original_front_vertices']} vertices; "
              f"rear {model['rear_vertices']} vertices in {len(model['rear_cards'])} cards "
              f"replaces {model['replaced_underside_vertices']}-vertex underside")


if __name__ == '__main__':
    main()
