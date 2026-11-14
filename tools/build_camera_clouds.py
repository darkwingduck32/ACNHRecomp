#!/usr/bin/env python3
"""Give native CloudVR uniform panoramic UV coverage; keep game assets private."""
import argparse
import bisect
import collections
import hashlib
import json
import math
from pathlib import Path
import struct

import zstandard
from freecamera_trees import Tree, sarc_members
from preview_trees import attribute

ROOT = Path(__file__).resolve().parents[1]


def remap(data):
    tree = Tree(data)
    output = bytearray(data)
    reports = []
    for shape, name, vertex in tree.shapes():
        positions = tree.positions(vertex)
        old = attribute(tree, vertex, '_u0')
        triangles = tree.triangles(shape)
        # Preserve the authored vertical profile along the good front meridian.
        meridian = sorted(set((math.atan2(y, abs(z)), old[i][1])
                              for i, (x, y, z) in enumerate(positions) if abs(x) < .001 and z < 0))
        if len(meridian) < 8:
            raise ValueError('Missing native front meridian')
        elevations = [p[0] for p in meridian]
        neighbors = collections.defaultdict(set)
        for triangle in triangles:
            for i in triangle:
                neighbors[i].update(triangle)
        mapped = []
        for i, (x, y, z) in enumerate(positions):
            angle = math.atan2(x, -z)
            # The native mesh already duplicates its back-meridian vertices.
            # Assign each duplicate to its own side rather than interpolating
            # across the entire texture in the seam's adjacent triangles.
            if abs(x) < .001 and z > 0:
                sides = {1 if positions[j][0] > 0 else -1 for j in neighbors[i]
                         if abs(positions[j][0]) > .001}
                if len(sides) != 1:
                    raise ValueError('Back seam requires a split vertex')
                angle = math.pi if 1 in sides else -math.pi
            elevation = math.atan2(y, math.hypot(x, z))
            k = min(max(bisect.bisect_right(elevations, elevation), 1), len(meridian)-1)
            a, b = meridian[k-1], meridian[k]
            t = min(max((elevation-a[0])/(b[0]-a[0]), 0), 1)
            mapped.append((.5+angle/(2*math.pi), a[1]+t*(b[1]-a[1])))
        if any(max(mapped[i][0] for i in row)-min(mapped[i][0] for i in row) > .5 for row in triangles):
            raise ValueError('Triangle crosses panoramic UV seam')
        _, fmt, offset, buffer = next(a for a in tree.attributes(vertex) if a[0] == '_u0')
        if fmt != 0x512:
            raise ValueError('Expected native half-float UVs')
        start, _, stride = list(tree.buffers(vertex))[buffer]
        for i, uv in enumerate(mapped):
            struct.pack_into('<2e', output, start+offset+i*stride, *uv)
        reports.append(dict(shape=name, vertices=len(positions), triangles=len(triangles),
                            seam_crossings=0, native_meridian_samples=len(meridian)))
    if not reports:
        raise ValueError('No cloud shapes found')
    return output, reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'private/game/romfs/Model/CloudVR.Nin_NX_NVN.zs')
    parser.add_argument('--out', type=Path, default=ROOT/'private/freecamera/cloud-mod/romfs/Model/CloudVR.Nin_NX_NVN.zs')
    args = parser.parse_args()
    if not args.out.resolve().is_relative_to(ROOT/'private') or args.source.resolve() == args.out.resolve():
        parser.error('Write a separate output under private/')
    source = args.source.read_bytes()
    data = zstandard.ZstdDecompressor().decompress(source)
    output = bytearray(data)
    members = list(sarc_members(data)) if data[:4] == b'SARC' else [(args.source.name, 0, len(data))]
    reports = []
    for name, start, end in members:
        if data[start:start+4] != b'FRES':
            continue
        mapped, shapes = remap(data[start:end])
        output[start:end] = mapped
        reports.append(dict(member=name, shapes=shapes))
    if not reports:
        parser.error('No BFRES members found')
    assert len(output) == len(data)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(zstandard.ZstdCompressor(level=19).compress(output))
    report = dict(source_sha256=hashlib.sha256(source).hexdigest(),
                  output_sha256=hashlib.sha256(args.out.read_bytes()).hexdigest(),
                  changed_bytes=sum(a != b for a, b in zip(data, output)), members=reports)
    args.out.with_suffix('.report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
