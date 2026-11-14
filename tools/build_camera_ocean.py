#!/usr/bin/env python3
"""Extend native outer-island water for the 360 camera; keep game assets private.

The water lives in the FldOut shoreline archives, including airport and rocky
coast variants. SeaOnly archives alone do not cover ordinary island layouts.
Only water/underside positions and their bounds change. The inner 80 units,
shore geometry, UVs, materials, indices and collision data remain untouched.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

import zstandard
from freecamera_trees import Tree, sarc_members, u16, u32, u64
from preview_trees import attribute

ROOT = Path(__file__).resolve().parents[1]


def extend_archive(data, direction, outer):
    output = bytearray(data)
    allowed = bytearray(len(data))
    reports = []

    def extend(value, axis):
        sign = (1 if (axis == 0 and 'E' in direction) or (axis == 2 and 'S' in direction)
                else -1 if (axis == 0 and 'W' in direction) or (axis == 2 and 'N' in direction) else 0)
        return sign * (80 + (sign * value - 80) * (outer - 80) / 160) if sign * value > 80 else value

    for member, start, end in sarc_members(data):
        if data[start:start + 4] != b'FRES':
            continue
        original = data[start:end]
        if u32(original, 8) != 0x90000:
            raise ValueError(f'{member}: expected BFRES 9')
        tree = Tree(original)
        edit = bytearray(original)

        def write(offset, code, *values):
            size = struct.calcsize('<' + code)
            if offset < 0 or offset + size > len(edit):
                raise ValueError('Out-of-range mesh field')
            struct.pack_into('<' + code, edit, offset, *values)
            allowed[start + offset:start + offset + size] = b'\1' * size

        for shape, name, vertex in tree.shapes():
            if 'seawave' not in name.lower() and 'seaback' not in name.lower():
                continue
            _, fmt, offset, buffer = next(a for a in tree.attributes(vertex) if a[0] == '_p0')
            if fmt not in (0x518, 0x515):
                raise ValueError(f'{name}: unsupported position format {fmt:x}')
            points = tree.positions(vertex) if fmt == 0x518 else attribute(tree, vertex, '_p0')
            base, size, stride = list(tree.buffers(vertex))[buffer]
            code = '3f' if fmt == 0x518 else '4e'
            transformed = []
            for i, point in enumerate(points):
                value = list(point)
                for axis in (0, 2):
                    value[axis] = extend(point[axis], axis)
                if not all(math.isfinite(v) for v in value):
                    raise ValueError(f'{name}: non-finite position')
                if offset + i * stride + struct.calcsize('<' + code) > size:
                    raise ValueError(f'{name}: position exceeds vertex buffer')
                write(base + offset + i * stride, code, *value)
                transformed.append(value[:3])
            if original[shape + 91] != 1:
                raise ValueError(f'{name}: expected one mesh level')
            mesh = u64(original, shape + 24)
            boxes = u16(original, mesh + 52) + 1
            for i in range(boxes):
                at = u64(original, shape + 56) + 24 * i
                box = struct.unpack_from('<6f', original, at)
                lo = [extend(box[k] - box[k + 3], k) for k in range(3)]
                hi = [extend(box[k] + box[k + 3], k) for k in range(3)]
                write(at, '6f', *[(lo[k] + hi[k]) / 2 for k in range(3)],
                      *[(hi[k] - lo[k]) / 2 for k in range(3)])
            write(u64(original, shape + 64), 'f',
                  max(math.sqrt(sum(v * v for v in p)) for p in transformed))
            reports.append(dict(member=member, shape=name, vertices=len(points),
                                bounds=[(min(p[k] for p in transformed), max(p[k] for p in transformed))
                                        for k in range(3)]))
        output[start:end] = edit
    changed = [i for i, (a, b) in enumerate(zip(data, output)) if a != b]
    if len(data) != len(output) or any(not allowed[i] for i in changed):
        raise ValueError('Changes escaped position/bounds/radius fields')
    return output, reports, len(changed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'private/game/romfs/Model')
    parser.add_argument('--out', type=Path, default=ROOT / 'private/freecamera/ocean-shore-mod/romfs/Model')
    parser.add_argument('--outer', type=float, required=True,
                        help='Outer water extent in world units; validate coverage and frame cost with the current runtime')
    args = parser.parse_args()
    if not args.out.resolve().is_relative_to(ROOT / 'private') or args.out.resolve() == args.source.resolve():
        parser.error('Use a separate output directory under private/')
    if not math.isfinite(args.outer) or not 240 <= args.outer <= 1200:
        parser.error('--outer must be between 240 and 1200 world units')
    reports = []
    for path in sorted(args.source.glob('FldOut*.Nin_NX_NVN.zs')):
        match = re.fullmatch(r'FldOut([NSEW]{1,2})[^.]*\.Nin_NX_NVN.zs', path.name)
        if not match:
            continue
        source = path.read_bytes()
        data = zstandard.ZstdDecompressor().decompress(source)
        output, shapes, changed = extend_archive(data, match[1], args.outer)
        if not changed:
            continue
        packed = zstandard.ZstdCompressor(level=12).compress(output)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / path.name).write_bytes(packed)
        reports.append(dict(file=path.name, source_sha256=hashlib.sha256(source).hexdigest(),
                            output_sha256=hashlib.sha256(packed).hexdigest(), changed_bytes=changed,
                            outside_allowed=0, shapes=shapes))
    if not reports:
        parser.error('No outer water meshes changed')
    (args.out / 'ocean-extension.json').write_text(json.dumps(dict(outer=args.outer, models=reports), indent=2) + '\n')
    print(f'Extended {len(reports)} shoreline water archives; byte-range audit passed')


if __name__ == '__main__':
    main()
