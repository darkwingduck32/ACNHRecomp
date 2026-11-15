#!/usr/bin/env python3
"""Build a local RomFS tool durability mod from the user's ItemOutfitInfo.bcsv.

Uses the 65,535-use approach demonstrated by Weaselgrease's mod:
https://gamebanana.com/mods/342961
Game-derived output stays under private/ and is never committed.
"""
import argparse
import json
from pathlib import Path
import struct
import zipfile

LIMIT_HASH = 0x68DB76C2
NAME_HASH = 0x87BF00E8
FAMILIES = ('Axe', 'Net', 'FishingRod', 'Slingshot', 'Scoop', 'Watering')


def patch_table(data):
    if len(data) < 28:
        raise ValueError('Truncated BCSV header')
    count, stride, columns, version, _ = struct.unpack_from('<IIHBB', data)
    if version != 1 or data[12:16] != b'VSCB':
        raise ValueError('Expected a version 1 BCSV table')
    start = 28 + columns * 8
    if stride == 0 or start + count * stride != len(data):
        raise ValueError('Invalid BCSV table size')
    specs = [struct.unpack_from('<II', data, 28 + i * 8) for i in range(columns)]
    if any(offset >= stride for _, offset in specs):
        raise ValueError('Invalid BCSV column offset')
    fields = {key: (offset, (specs[i + 1][1] if i + 1 < columns else stride) - offset)
              for i, (key, offset) in enumerate(specs)}
    if LIMIT_HASH not in fields or NAME_HASH not in fields:
        raise ValueError('Missing durability or equipment name column')
    limit, width = fields[LIMIT_HASH]
    name_offset, name_width = fields[NAME_HASH]
    if width != 2 or name_width != 32:
        raise ValueError('Unexpected durability or name column width')
    output = bytearray(data)
    changes = []
    for i in range(count):
        row = start + i * stride
        name = data[row + name_offset:row + name_offset + name_width].split(b'\0')[0].decode('ascii')
        old, = struct.unpack_from('<H', data, row + limit)
        if not old:
            continue
        if not name.startswith(FAMILIES):
            raise ValueError(f'Unexpected breakable equipment: {name}')
        struct.pack_into('<H', output, row + limit, 65535)
        changes.append({'name': name, 'before': old, 'after': 65535})
    if not changes:
        raise ValueError('No breakable tools found')
    return bytes(output), changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True, help='New mod folder under private/')
    args = parser.parse_args()
    private = Path(__file__).resolve().parents[1] / 'private'
    if not args.out.resolve().is_relative_to(private):
        parser.error('Keep game-derived output under private/.')
    if args.out.exists():
        parser.error('Output folder already exists; choose a new folder.')
    source = args.romfs / 'Bcsv' / 'ItemOutfitInfo.bcsv'
    patched, changes = patch_table(source.read_bytes())
    target = args.out / 'romfs' / 'Bcsv' / source.name
    target.parent.mkdir(parents=True)
    target.write_bytes(patched)
    readme = Path(__file__).resolve().parents[1] / 'games/acnh/mods/tool_durability/README.md'
    (args.out / 'README.md').write_bytes(readme.read_bytes())
    (args.out / 'changes.json').write_text(json.dumps(changes, indent=2) + '\n', encoding='utf-8')
    archive = args.out.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
        for file in sorted(args.out.rglob('*')):
            if file.is_file():
                z.write(file, file.relative_to(args.out))
    print(f'Updated {len(changes)} tool profiles; mod: {args.out}; archive: {archive}')


if __name__ == '__main__':
    main()
