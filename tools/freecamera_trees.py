#!/usr/bin/env python3
"""Inspect local ACNH tree meshes. Game-derived reports must stay under private/."""
import argparse
import hashlib
import json
import math
import pathlib
import struct
import zlib

import zstandard
from maxwell import yaz0


IDENTITY = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
def mv(matrix, vector):
    return tuple(sum(row[k]*vector[k] for k in range(3)) for row in matrix)


def mm(a, b):
    return tuple(tuple(sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def transpose(matrix):
    return tuple(zip(*matrix))


def add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def subtract(a, b):
    return tuple(x-y for x, y in zip(a, b))


def rear(vector):
    return (-vector[0], vector[1], -vector[2])


def whole_leaf_groups(points, triangles):
    """Keep complete connected leaf cards; never slice their alpha textures."""
    parents = list(range(len(points)))
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
    centers = {}
    for key, group in groups.items():
        used = {i for tri in group for i in tri}
        centers[key] = sum(points[i][2] for i in used)/len(used)
    side = 1 if any(z > 0 for z in centers.values()) else -1
    kept = [tri for key, group in groups.items() if side*centers[key] >= 0 for tri in group]
    used = sorted({i for tri in kept for i in tri})
    remap = {old: new for new, old in enumerate(used)}
    return [points[i] for i in used], [tuple(remap[i] for i in tri) for tri in kept], used, side, len(groups)


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def u64(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]


def string(data, offset):
    size = u16(data, offset)
    return data[offset + 2:offset + 2 + size].decode('utf-8')


def resource_sizes(path, changes, base_table=None):
    """Grow per-resource heaps as well as the file. RSTC sizes are uint64."""
    data = yaz0.decompress(path.read_bytes())
    if data[:4] != b'RSTC':
        raise ValueError('Expected ACNH RSTC resource-size table')
    count = u32(data, 4)
    if 12+12*count > len(data):
        raise ValueError('Invalid resource-size table')
    records = {}
    for i in range(count):
        p = 12+12*i
        records.setdefault(u32(data, p), []).append(p)
    baseline = {}
    if base_table is not None:
        stock = yaz0.decompress(base_table.read_bytes())
        if stock[:4] != b'RSTC':
            raise ValueError('Expected stock ACNH RSTC table')
        baseline = {u32(stock, 12+12*i): u64(stock, 16+12*i) for i in range(u32(stock, 4))}
    report = []
    for name, delta in changes.items():
        key = 'Model/' + name.removesuffix('.zs')
        crc = zlib.crc32(key.encode())
        matches = records.get(crc, [])
        if len(matches) != 1:
            raise ValueError(f'Ambiguous or missing resource size: {key}')
        p = matches[0]+4
        old = u64(data, p)
        # Include resource setup scratch space in addition to the expanded file.
        source_size = baseline[crc] if baseline else old
        new = max(old, (source_size+2*delta+65536+4095) & ~4095)
        struct.pack_into('<Q', data, p, new)
        report.append(dict(resource=key, before=old, after=new))
    # Literal-only Yaz0 keeps the format and decompressed length unchanged.
    packed = bytearray(b'Yaz0'+struct.pack('>I', len(data))+b'\0'*8)
    for p in range(0, len(data), 8):
        packed.append(255)
        packed.extend(data[p:p+8])
    assert yaz0.decompress(packed) == data
    return bytes(packed), report


def sarc_members(data):
    if data[:4] != b'SARC' or data[6:8] != b'\xff\xfe':
        raise ValueError('Expected little-endian SARC')
    header, start = u16(data, 4), u32(data, 12)
    count = u16(data, header + 6)
    names = header + 12 + 16 * count + 8
    for i in range(count):
        node = header + 12 + 16 * i
        name = names + (u32(data, node + 4) & 0xffffff) * 4
        name = data[name:data.index(b'\0', name)].decode()
        yield name, start + u32(data, node + 8), start + u32(data, node + 12)


class Tree:
    def __init__(self, data):
        self.data = data
        if data[:4] != b'FRES' or u32(data, 8) != 0x00090000:
            raise ValueError('Only Switch BFRES 9 is supported')
        self.gpu = u64(data, u64(data, 0xb0) + 8)

    def shapes(self):
        d = self.data
        # Signature candidates are validated against their referenced vertex buffer.
        cursor = 0
        while True:
            p = d.find(b'FSHP', cursor)
            if p < 0:
                return
            cursor = p + 4
            v = u64(d, p + 16)
            if d[v:v+4] != b'FVTX':
                continue
            yield p, string(d, u64(d, p + 8)), v

    def compact_gpu(self):
        """Repack referenced buffers, dropping superseded source/copy storage."""
        d = self.data
        rlt = u32(d, 24)
        sections = [struct.unpack_from('<Q4I', d, rlt+16+24*i) for i in range(5)]
        _, first, old_i, _, n_i = sections[1]
        _, split, old_v, _, n_v = sections[2]
        end = split+old_v
        if split != first+old_i or n_i != 1 or n_v != 0:
            raise ValueError('Unsupported buffer relocation references')
        indices, vertices, meshes, vtx = bytearray(), bytearray(), {}, {}
        for shape, _, v in self.shapes():
            mesh = u64(d, shape+24)
            if d[shape+91] != 1:
                raise ValueError('Expected one tree mesh')
            if mesh not in meshes:
                indices.extend(b'\0'*(-len(indices)%8))
                meshes[mesh] = len(indices)
                size = u32(d, u64(d, mesh+24))
                start = self.gpu+u32(d, mesh+32)
                indices.extend(d[start:start+size])
            if v in vtx:
                continue
            vertices.extend(b'\0'*(-len(vertices)%8))
            vtx[v] = len(vertices)
            for start, size, _ in self.buffers(v):
                vertices.extend(b'\0'*(-len(vertices)%8))
                vertices.extend(d[start:start+size])
        indices.extend(b'\0'*(-len(indices)%4096))
        vertices.extend(b'\0'*(-len(vertices)%4096))
        di, dv = len(indices)-old_i, len(vertices)-old_v
        def mapped(p):
            return p+(di if p >= split else 0)+(dv if p >= end else 0)
        out = bytearray(d[:first]+indices+vertices+d[end:])
        entries = rlt+16+24*5
        for i in range(max(s[3]+s[4] for s in sections)):
            record = entries+8*i
            pos, structs, offsets, padding = struct.unpack_from('<IHBB', d, record)
            for j in range(structs):
                for k in range(offsets):
                    location = pos+8*(j*(offsets+padding)+k)
                    value = u64(d, location)
                    if first < value < end:
                        raise ValueError('Unexpected pointer into a movable GPU buffer')
                    if value:
                        struct.pack_into('<Q', out, mapped(location), mapped(value))
            struct.pack_into('<I', out, mapped(record), mapped(pos))
        for i, (base, start, size, entry, count) in enumerate(sections):
            size += di if i == 1 else dv if i == 2 else 0
            struct.pack_into('<Q4I', out, mapped(rlt)+16+24*i, base, mapped(start), size, entry, count)
        struct.pack_into('<I', out, 24, mapped(rlt))
        struct.pack_into('<I', out, 28, len(out))
        struct.pack_into('<I', out, mapped(rlt)+4, mapped(rlt))
        struct.pack_into('<I', out, u64(d, 0xb0)+4, len(indices)+len(vertices))
        for mesh, offset in meshes.items():
            struct.pack_into('<I', out, mesh+32, offset)
        for v, offset in vtx.items():
            struct.pack_into('<I', out, v+72, len(indices)+offset)
        result = Tree(bytes(out))
        for v in vtx:
            for (a, size, stride), (b, size2, stride2) in zip(self.buffers(v), result.buffers(v)):
                assert size == size2 and stride == stride2 and d[a:a+size] == out[b:b+size]
        # Embedded textures/shaders and the native material/skeleton counts stay intact.
        _, start, size, _, _ = sections[4]
        assert d[start:start+size] == out[mapped(start):mapped(start)+size]
        return bytes(out)

    def attributes(self, v):
        d = self.data
        for i in range(d[v + 76]):
            p = u64(d, v + 8) + 16 * i
            yield string(d, u64(d, p)), int.from_bytes(d[p+8:p+10], 'big'), u16(d, p+12), u16(d, p+14)

    def buffers(self, v):
        d = self.data
        offset = self.gpu + u32(d, v + 72)
        for i in range(d[v + 77]):
            offset = (offset + 7) & ~7
            size = u32(d, u64(d, v + 48) + 16*i)
            stride = u32(d, u64(d, v + 56) + 16*i)
            yield offset, size, stride
            offset += size

    def positions(self, v):
        d = self.data
        buffers = list(self.buffers(v))
        for name, fmt, offset, index in self.attributes(v):
            if name != '_p0':
                continue
            if fmt != 0x0518:
                raise ValueError(f'Unsupported position format {fmt:x}')
            start, size, stride = buffers[index]
            count = u32(d, v + 80)
            if (count - 1) * stride + offset + 12 > size:
                raise ValueError('Positions exceed vertex buffer')
            return [struct.unpack_from('<3f', d, start + offset + i*stride) for i in range(count)]
        raise ValueError('No positions')

    def rig(self, model, shape, v):
        """Rigid one-bone-per-vertex tree deformation, in bind-pose model space."""
        d = self.data
        if u16(d, v+84) == 0:
            return [(IDENTITY, (0, 0, 0))], [0]*u32(d, v+80)
        skeleton = u64(d, model+24)
        array = u64(d, skeleton+16)
        bones = []
        for i in range(u16(d, skeleton+56)):
            p = array+96*i
            parent = struct.unpack_from('<h', d, p+42)[0]
            sx, sy, sz, rx, ry, rz, _, tx, ty, tz = struct.unpack_from('<10f', d, p+56)
            if max(abs(s-1) for s in (sx, sy, sz)) > .0001 or (u32(d, p+52) & 0x7000) != 0x1000:
                raise ValueError('Expected unit-scale Euler tree bones')
            cx, cy, cz, ax, ay, az = math.cos(rx), math.cos(ry), math.cos(rz), math.sin(rx), math.sin(ry), math.sin(rz)
            rotation = mm(((cz, -az, 0), (az, cz, 0), (0, 0, 1)),
                          mm(((cy, 0, ay), (0, 1, 0), (-ay, 0, cy)), ((1, 0, 0), (0, cx, -ax), (0, ax, cx))))
            position = (tx, ty, tz)
            if parent >= 0:
                pr, pt = bones[parent]
                position, rotation = add(mv(pr, position), pt), mm(pr, rotation)
            bones.append((rotation, position))
        palette_count = u16(d, skeleton+58)+u16(d, skeleton+60)
        palette = struct.unpack_from(f'<{palette_count}H', d, u64(d, skeleton+24))
        transforms = [bones[i] for i in palette]
        if u16(d, v+84) > 1:
            # Smooth skinning includes inverse bind matrices: its bind-pose
            # vertices are in model space, unlike rigid one-bone vertices.
            if u16(d, skeleton+58) == 0:
                raise ValueError('Weighted tree has no smooth bind matrices')
            return transforms, None
        attr = next(a for a in self.attributes(v) if a[0] == '_i0')
        _, fmt, offset, index = attr
        if fmt != 0x0302:
            raise ValueError('Expected uint8 tree bone indices')
        start, _, stride = list(self.buffers(v))[index]
        indices = [d[start+i*stride+offset] for i in range(u32(d, v+80))]
        if max(indices) >= len(transforms):
            raise ValueError('Invalid tree bone palette index')
        return transforms, indices

    def triangles(self, shape):
        d = self.data
        if d[shape + 91] != 1:
            raise ValueError('Expected one mesh')
        m = u64(d, shape + 24)
        if u32(d, m + 36) != 3 or u32(d, m + 40) != 1:
            raise ValueError('Expected triangle list with uint16 indices')
        count = u32(d, m + 44)
        if count % 3:
            raise ValueError('Incomplete triangle list')
        first = u32(d, m + 48)
        start = self.gpu + u32(d, m + 32)
        values = struct.unpack_from(f'<{count}H', d, start)
        return [tuple(first + x for x in values[i:i+3]) for i in range(0, count, 3)]

    def report(self):
        shapes = []
        for p, name, v in self.shapes():
            points = self.positions(v)
            triangles = self.triangles(p)
            areas = [0.0, 0.0]
            for a, b, c in triangles:
                # Signed XY projection: how much surface faces either Z direction.
                a, b, c = points[a], points[b], points[c]
                z = (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0])
                areas[z < 0] += abs(z) / 2
            shapes.append(dict(name=name, offset=p, vertices=len(points), triangles=len(triangles),
                               projected_area_positive_z=areas[0], projected_area_negative_z=areas[1],
                               bounds=[[min(p[k] for p in points), max(p[k] for p in points)] for k in range(3)]))
        return shapes

    def fill_rear(self, depth_fraction=0.08, only_models=None):
        """Add a 180-degree copy to strongly front-biased canopy meshes.

        Preserve original vertex attributes, UVs, materials and shape names;
        retain complete connected leaf groups before adding the rear half.
        Grow the existing GPU sections and relocate declared BFRES pointers; do
        not create extra shapes or change the game's model selection.
        """
        d = self.data
        rlt = u32(d, 24)
        if d[rlt:rlt+4] != b'_RLT' or u32(d, rlt+8) != 5:
            raise ValueError('Unsupported relocation layout')
        sections = [struct.unpack_from('<Q4I', d, rlt+16+24*i) for i in range(5)]
        _, index_start, index_size, _, _ = sections[1]
        _, vertex_start, vertex_size, _, _ = sections[2]
        if index_start != self.gpu or index_start + index_size != vertex_start:
            raise ValueError('GPU sections are not adjacent')
        vertex_end = vertex_start + vertex_size
        reports = {s['offset']: s for s in self.report()}
        selected = []
        models = []
        cursor = 0
        while True:
            m = d.find(b'FMDL', cursor)
            if m < 0:
                break
            cursor = m+4
            name = string(d, u64(d, m+8))
            models.append((m, name))
        primary = {}
        for m, name in models:
            if 'Node' in name or 'Stump' in name:
                continue
            if only_models is not None and name not in only_models:
                continue
            for i in range(u16(d, m+106)):
                p = u64(d, m+40) + 96*i
                if d[p:p+4] != b'FSHP':
                    raise ValueError('Invalid model shape array')
                s = reports[p]
                if 'Leaf' not in s['name'] or any(x in s['name'] for x in ('Back', 'Sph', 'Shadow')):
                    continue
                a, b = s['projected_area_positive_z'], s['projected_area_negative_z']
                if max(a, b) < 4 * max(min(a, b), .001):
                    continue
                primary[(name, s['name'])] = p
                selected.append((p, u64(d, p+16), s['name'], name, m))
        for m, name in models:
            if not name.endswith('Node'):
                continue
            for i in range(u16(d, m+106)):
                p = u64(d, m+40)+96*i
                s = reports[p]
                if (name.removesuffix('Node'), s['name']) in primary:
                    selected.append((p, u64(d, p+16), s['name'], name, m))
        if not selected:
            return d, []
        indices, vertices, edits = bytearray(), bytearray(), []
        seen_vertices = set()
        for p, v, name, model, model_offset in selected:
            count = u32(d, v+80)
            source_count = count
            if count*2 > 65535 or v in seen_vertices:
                raise ValueError('Shared or oversized canopy vertex buffer')
            seen_vertices.add(v)
            attrs = list(self.attributes(v))
            buffers = list(self.buffers(v))
            transforms, bone_indices = self.rig(model_offset, p, v)
            # Reuse the opposite branch in the existing rig, so a bend/shake
            # follows the matching side instead of pivoting about the old side.
            opposite = [min(range(len(transforms)), key=lambda j: sum((transforms[j][1][k]-rear(t)[k])**2 for k in range(3)))
                        for _, t in transforms]
            world_points = self.positions(v)
            if bone_indices is not None:
                world_points = [add(mv(transforms[bone_indices[i]][0], point), transforms[bone_indices[i]][1])
                                for i, point in enumerate(world_points)]
            source_triangles = self.triangles(p)
            world_points, triangles, used, side, leaf_groups = whole_leaf_groups(world_points, source_triangles)
            prepared = [bytearray().join(d[start+i*stride:start+(i+1)*stride] for i in used)
                        for start, _, stride in buffers]
            if bone_indices is not None:
                bone_indices = [bone_indices[i] for i in used]
            count = len(used)
            if count*2 > 65535:
                raise ValueError('Tree exceeds uint16 indices')
            # Modest depth separation; intact cards overlap naturally at the join.
            width = reports[p]['bounds'][0][1]-reports[p]['bounds'][0][0]
            if model.endswith('Node'):
                template = primary[(model.removesuffix('Node'), name)]
                width = reports[template]['bounds'][0][1]-reports[template]['bounds'][0][0]
            depth = (max(.10, width*depth_fraction) if depth_fraction else 0)*side
            rotations, translations = [], []
            front_translations = []
            reflection = ((-1, 0, 0), (0, 1, 0), (0, 0, -1))
            for index in bone_indices or []:
                r, t = transforms[index]
                rr, rt = transforms[opposite[index]]
                rotations.append(mm(transpose(rr), mm(reflection, r)))
                translations.append(mv(transpose(rr), add(subtract(rear(t), rt), (0, 0, -depth))))
                front_translations.append(mv(transpose(r), (0, 0, depth)))
            if bone_indices is None:
                rotations, translations = [reflection]*count, [(0, 0, -depth)]*count
                front_translations = [(0, 0, depth)]*count
            new_buffer_offset = len(vertices)
            for index, (start, size, stride) in enumerate(buffers):
                vertices.extend(b'\0' * (-len(vertices) % 8))
                if index == 0:
                    new_buffer_offset = len(vertices)
                original = bytes(prepared[index])
                shifted = bytearray(original)
                rotated = bytearray(original)
                for attr, fmt, offset, buffer_index in attrs:
                    if buffer_index != index or attr not in ('_p0', '_n0', '_t0', '_b0', '_c0', '_i0'):
                        continue
                    if attr == '_c0' and fmt != 0x0515:
                        continue
                    for i in range(count):
                        at = i*stride+offset
                        if attr == '_p0' and fmt == 0x0518:
                            x, y, z = struct.unpack_from('<3f', original, at)
                            struct.pack_into('<3f', shifted, at, *add((x, y, z), front_translations[i]))
                            struct.pack_into('<3f', rotated, at, *add(mv(rotations[i], (x, y, z)), translations[i]))
                        elif attr == '_c0' and fmt == 0x0515:
                            # In tree leaf materials this float16 attribute is a
                            # spatial wind pivot, not an ordinary vertex color.
                            # Moving the leaf without its pivot creates huge bends.
                            x, y, z, weight = struct.unpack_from('<4e', original, at)
                            struct.pack_into('<4e', shifted, at, x, y, z+depth, weight)
                            struct.pack_into('<4e', rotated, at, -x, y, -z-depth, weight)
                        elif attr == '_i0' and fmt == 0x0302:
                            rotated[at] = opposite[original[at]]
                        elif attr == '_i0' and fmt in (0x0309, 0x030b):
                            for component in range(2 if fmt == 0x0309 else 4):
                                rotated[at+component] = opposite[original[at+component]]
                        elif fmt == 0x020e:
                            value = u32(original, at)
                            xyz = [(value >> shift) & 1023 for shift in (0, 10, 20)]
                            xyz = [x-1024 if x & 512 else x for x in xyz]
                            transformed = mv(rotations[i], xyz)
                            for shift, x in zip((0, 10, 20), transformed):
                                x = max(-511, min(511, round(x))) & 1023
                                value = (value & ~(1023 << shift)) | (x << shift)
                            struct.pack_into('<I', rotated, at, value)
                        elif fmt == 0x020b:
                            xyz = struct.unpack_from('<3b', original, at)
                            for component, x in enumerate(mv(rotations[i], xyz)):
                                struct.pack_into('<b', rotated, at+component, max(-127, min(127, round(x))))
                        else:
                            raise ValueError(f'Unsupported {attr} format {fmt:x}')
                vertices.extend(shifted)
                vertices.extend(rotated)
            if not triangles:
                raise ValueError(f'No front hemisphere: {model}/{name}')
            mesh = u64(d, p+24)
            if u32(d, mesh+48) or u16(d, mesh+52) != 1:
                raise ValueError('Expected one submesh and zero first vertex')
            flat = [x for tri in triangles for x in tri]
            indices.extend(b'\0' * (-len(indices) % 8))
            index_offset = len(indices)
            indices.extend(struct.pack(f'<{len(flat)*2}H', *(flat + [x+count for x in flat])))
            edits.append(dict(shape=p, vertex=v, mesh=mesh, name=name, model=model, vertex_count=count,
                              model_offset=model_offset, vertex_offset=new_buffer_offset, index_offset=index_offset,
                              source_vertex_count=count, source_vertices=used, source_total_vertices=source_count,
                              seam_vertices=0, leaf_groups=leaf_groups, depth_offset=depth, depth_fraction=depth_fraction,
                              residual_join_penetration=max(0, -min(side*point[2]+abs(depth) for point in world_points)),
                              source_triangles=len(source_triangles), retained_front_triangles=len(triangles), index_count=len(flat)))
        indices.extend(b'\0' * (-len(indices) % 4096))
        vertices.extend(b'\0' * (-len(vertices) % 4096))
        ni, nv = len(indices), len(vertices)
        def mapped(p):
            return p + (ni if p >= vertex_start else 0) + (nv if p >= vertex_end else 0)
        out = bytearray(d[:vertex_start] + indices + d[vertex_start:vertex_end] + vertices + d[vertex_end:])
        # Relocate every declared pointer and the relocation records themselves.
        entries = rlt+16+24*5
        total = max(s[3]+s[4] for s in sections)
        for i in range(total):
            record = entries+8*i
            pos, structs, offsets, padding = struct.unpack_from('<IHBB', d, record)
            for j in range(structs):
                for k in range(offsets):
                    location = pos + 8*(j*(offsets+padding)+k)
                    value = u64(d, location)
                    if value:
                        struct.pack_into('<Q', out, mapped(location), mapped(value))
            struct.pack_into('<I', out, mapped(record), mapped(pos))
        for i, (base, start, size, first, number) in enumerate(sections):
            size += ni if i == 1 else nv if i == 2 else 0
            struct.pack_into('<Q4I', out, mapped(rlt)+16+24*i, base, mapped(start), size, first, number)
        struct.pack_into('<I', out, 24, mapped(rlt))
        struct.pack_into('<I', out, 28, len(out))
        struct.pack_into('<I', out, mapped(rlt)+4, mapped(rlt))
        # All existing vertex offsets are relative to the unchanged GPU base.
        vertex_buffers = {v for _, _, v in self.shapes()}
        for v in vertex_buffers:
            struct.pack_into('<I', out, v+72, u32(d, v+72)+ni)
        info = u64(d, 0xb0)
        struct.pack_into('<I', out, info+4, u32(d, info+4)+ni+nv)
        for edit in edits:
            p, v, mesh, count = (edit[k] for k in ('shape', 'vertex', 'mesh', 'vertex_count'))
            struct.pack_into('<I', out, v+72, vertex_end+ni+edit['vertex_offset']-self.gpu)
            struct.pack_into('<I', out, v+80, count*2)
            for i, (_, _, stride) in enumerate(self.buffers(v)):
                struct.pack_into('<I', out, u64(d, v+48)+16*i, count*stride*2)
            struct.pack_into('<I', out, mesh+32, vertex_start+edit['index_offset']-self.gpu)
            struct.pack_into('<I', out, mesh+44, edit['index_count']*2)
            struct.pack_into('<I', out, u64(d, mesh+24), edit['index_count']*4)
            struct.pack_into('<I', out, u64(d, mesh)+4, edit['index_count']*2)
            # Node variants use bone-local axes, so recompute all three bounds
            # instead of assuming their local Y is the world's vertical axis.
            points = Tree(bytes(out)).positions(v)
            low = [min(point[k] for point in points) for k in range(3)]
            high = [max(point[k] for point in points) for k in range(3)]
            center = [(low[k]+high[k])/2 for k in range(3)]
            extent = [(high[k]-low[k])/2 for k in range(3)]
            for i in range(2):
                bound = u64(d, p+56)+24*i
                struct.pack_into('<6f', out, bound, *center, *extent)
            radius = u64(d, p+64)
            old_radius = struct.unpack_from('<f', d, radius)[0]
            new_radius = max(math.sqrt(sum(x*x for x in point)) for point in points)
            struct.pack_into('<f', out, radius, max(old_radius, new_radius))
        actual = Tree(bytes(out))
        after = {s['offset']: s for s in actual.report()}
        for edit in edits:
            before, result = reports[edit['shape']], after[edit['shape']]
            assert result['vertices'] == edit['vertex_count']*2
            assert result['triangles'] == edit['index_count']*2//3
            v, count = edit['vertex'], edit['vertex_count']
            world = actual.positions(v)
            rig, bone_indices = actual.rig(edit['model_offset'], edit['shape'], v)
            if bone_indices is not None:
                world = [add(mv(rig[bone_indices[i]][0], point), rig[bone_indices[i]][1]) for i, point in enumerate(world)]
            # No extra texture/material/bone records are created. Only referenced
            # source vertices and their rear counterparts enter the new buffers.
            draw_triangles = actual.triangles(edit['shape'])
            assert len({i for tri in draw_triangles for i in tri}) == count*2
            old_buffers, new_buffers = list(self.buffers(v)), list(actual.buffers(v))
            for i, (start, size, stride) in enumerate(old_buffers):
                new_start, new_size, new_stride = new_buffers[i]
                assert new_size == count*stride*2 and new_stride == stride
                # Source UVs, weights, normals, tangents and skin indices stay
                # unchanged; only positions and float16 pivots move forward.
                for attr, fmt, offset, buffer_index in self.attributes(v):
                    if buffer_index != i or attr == '_p0' or (attr == '_c0' and fmt == 0x0515):
                        continue
                    width = {0x0518:12, 0x0515:8, 0x0512:4, 0x0112:4, 0x010b:4, 0x0109:2,
                             0x0302:1, 0x0309:2, 0x030b:4, 0x020b:4, 0x020e:4}[fmt]
                    for vertex in range(edit['source_vertex_count']):
                        a, b = new_start+vertex*stride+offset, start+edit['source_vertices'][vertex]*stride+offset
                        assert out[a:a+width] == d[b:b+width]
            pivots = [a for a in self.attributes(v) if a[0] == '_c0' and a[1] == 0x0515]
            if pivots:
                _, _, offset, index = pivots[0]
                start, size, stride = new_buffers[index]
                points = world
                for i in range(count):
                    pivot = struct.unpack_from('<4e', out, start+i*stride+offset)
                    rear_pivot = struct.unpack_from('<4e', out, start+size//2+i*stride+offset)
                    assert rear_pivot == (-pivot[0], pivot[1], -pivot[2], pivot[3])
                    # The lever arm used by the wind shader must rotate with the leaf.
                    for axis, sign in enumerate((-1, 1, -1)):
                        before_arm = points[i][axis]-pivot[axis]
                        after_arm = points[i+count][axis]-rear_pivot[axis]
                        # Half-precision pivot translation rounds by up to one ULP.
                        assert abs(after_arm-sign*before_arm) < .04
                edit['wind_pivots_rotated'] = count
        compacted = Tree(bytes(out)).compact_gpu()
        assert Tree(compacted).report() == actual.report()
        return compacted, edits


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=pathlib.Path, default=pathlib.Path('private/game/romfs/Model'))
    parser.add_argument('--report', type=pathlib.Path, default=pathlib.Path('private/freecamera/trees.json'))
    parser.add_argument('--fill-rear', type=pathlib.Path, help='Experimental output mod folder under private/')
    parser.add_argument('--depth-fraction', type=float, default=0.08,
                        help='Move each canopy half by this fraction of canopy width (0 to 0.25; default 0.08).')
    args = parser.parse_args()
    if not math.isfinite(args.depth_fraction) or not 0 <= args.depth_fraction <= .25:
        parser.error('--depth-fraction must be between 0 and 0.25')
    private = pathlib.Path(__file__).resolve().parents[1] / 'private'
    if not args.report.resolve().is_relative_to(private):
        parser.error('Keep game-derived reports under private/')
    if args.fill_rear and not args.fill_rear.resolve().is_relative_to(private):
        parser.error('Keep modified assets under private/')
    report = []
    growth = {}
    for path in sorted(args.models.glob('PltTree*.zs')):
        if 'Anim' in path.name:
            continue
        source = path.read_bytes()
        data = zstandard.ZstdDecompressor().decompress(source)
        item = dict(file=path.name, sha256=hashlib.sha256(source).hexdigest(), members=[])
        output = bytearray(data)
        for name, start, end in sarc_members(data):
            if data[start:start+4] == b'FRES':
                tree = Tree(data[start:end])
                member = dict(name=name, shapes=tree.report())
                item['members'].append(member)
                if args.fill_rear:
                    if len(list(sarc_members(data))) != 1:
                        raise ValueError('Tree SARC must have exactly one member')
                    replacement, edits = tree.fill_rear(args.depth_fraction)
                    member['rear_fill'] = edits
                    if edits:
                        output = bytearray(data[:start] + replacement + data[end:])
                        struct.pack_into('<I', output, 8, len(output))
                        node = u16(data, 4)+12
                        struct.pack_into('<I', output, node+12, end-u32(data, 12)+len(replacement)-(end-start))
                        dest = args.fill_rear / 'romfs' / 'Model' / path.name
                        if dest.resolve() == path.resolve():
                            raise ValueError('Do not overwrite stock assets')
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        compressed = zstandard.ZstdCompressor(level=12).compress(output)
                        assert zstandard.ZstdDecompressor().decompress(compressed) == output
                        dest.write_bytes(compressed)
                        growth[path.name] = len(output)-len(data)
                        print(f'{path.name}: filled {len(edits)} front-biased canopies')
        report.append(item)
    if args.fill_rear and growth:
        relative = pathlib.Path('System/Resource/ResourceSizeTable.srsizetable')
        packed, sizes = resource_sizes(args.models.parent / relative, growth)
        target = args.fill_rear / 'romfs' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(packed)
        report.append(dict(resource_heap_changes=sizes))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Inspected {sum("file" in r for r in report)} tree assets; wrote {args.report}')


if __name__ == '__main__':
    main()
