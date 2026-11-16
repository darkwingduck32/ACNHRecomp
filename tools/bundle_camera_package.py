#!/usr/bin/env python3
"""Legacy package builder for older players; native-feature releases do not call this."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def bundle(dest, packages):
    mods=dest/'mods';manifest=mods/'bundle.txt'
    if not manifest.is_file():raise ValueError('Bundle the native release mods first; this tool does not manage them')
    # Preserve the native bundle verbatim, adding only our feature-owned files.
    lines=manifest.read_text().splitlines(); paths=set(lines[1:])
    for name in ('freecamera','tool_durability'):
        path=packages/(name+'.zip')
        with zipfile.ZipFile(path) as z:
            prefix=name+'/';m=json.loads(z.read(prefix+'manifest.json'))
            if m['id']!=name or any(c in ('ptbr','speedfix60') for c in m['conflicts']):raise ValueError('Invalid feature ownership')
            expected={prefix+k for k in m['files']}|{prefix+'manifest.json'}
            if set(z.namelist())!=expected:raise ValueError('Unexpected package contents')
            for item in sorted(expected):
                if any(p in ('','.','..') for p in item.split('/')) or '\\' in item or ':' in item:raise ValueError('Unsafe package path')
                data=z.read(item)
                if item!=prefix+'manifest.json' and hashlib.sha256(data).hexdigest()!=m['files'][item[len(prefix):]]:raise ValueError('Package checksum mismatch')
                target=mods/item;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);paths.add(item)
    (mods/'defaults').mkdir(exist_ok=True)
    (mods/'defaults/enabled.txt').write_text('freecamera\ntool_durability\n')
    paths.add('defaults/enabled.txt')
    # Stable derived version; repeat bundling is byte-identical.
    content='\n'.join(p+':'+hashlib.sha256((mods/p).read_bytes()).hexdigest() for p in sorted(paths))
    version=hashlib.sha256(content.encode()).hexdigest()[:32]
    manifest.write_text(version+'\n'+'\n'.join(sorted(paths))+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('dest',type=Path)
    p.add_argument('--packages',type=Path,default=ROOT/'private/share/packages');a=p.parse_args()
    if not a.dest.resolve().is_relative_to(ROOT/'private'):p.error('Keep release staging under private/')
    bundle(a.dest,a.packages)
if __name__=='__main__':main()
