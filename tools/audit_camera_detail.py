#!/usr/bin/env python3
"""Read BFRES mesh-slot counts for outdoor assets; keep reports under private/."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

import zstandard
from freecamera_trees import Tree, sarc_members, u32, u64


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    private = Path(__file__).resolve().parents[1] / 'private'
    if not args.out.resolve().is_relative_to(private):
        parser.error('Game-derived reports must stay under private/.')
    records, errors, histogram = [], [], Counter()
    paths = sorted(set(args.models.glob('PltTree*.zs')) | set(args.models.glob('Fld*.zs')))
    if not paths:
        parser.error('No tree or field archives found in --models.')
    for path in paths:
        source = path.read_bytes()
        record = dict(file=path.name, sha256=hashlib.sha256(source).hexdigest(), members=[])
        try:
            data = zstandard.ZstdDecompressor().decompress(source)
            for name, start, end in sarc_members(data):
                if data[start:start+4] != b'FRES':
                    continue
                tree = Tree(data[start:end])
                shapes = []
                for offset, shape_name, vertex in tree.shapes():
                    mesh = u64(tree.data, offset + 24)
                    count = tree.data[offset + 91]
                    if not count or mesh + 56 * count > len(tree.data):
                        raise ValueError('Invalid mesh array')
                    shapes.append(dict(name=shape_name, mesh_slots=count,
                                       first_mesh_indices=u32(tree.data, mesh + 44)))
                    histogram[count] += 1
                record['members'].append(dict(name=name, shapes=shapes))
            records.append(record)
        except (ValueError, IndexError, RuntimeError, struct.error, zstandard.ZstdError) as exc:
            errors.append(dict(file=path.name, error=str(exc)))
    result = dict(source=str(args.models), archives=len(paths),
                  shape_mesh_slot_histogram=dict(sorted(histogram.items())),
                  limitation='Mesh slots describe stored geometry alternatives, not runtime distance selection. Separate models and shader simplification require separate investigation.',
                  errors=errors, records=records)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'records'}, indent=2))
    if errors:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
