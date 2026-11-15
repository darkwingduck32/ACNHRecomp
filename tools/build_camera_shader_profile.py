#!/usr/bin/env python3
"""Identify native Park UBER shaders for the view-relative fog adapter.

Only hashes and provenance are emitted, under private/. No shader code is changed.
Additional model archives can be supplied when they embed Park UBER variations.
"""
import argparse
import hashlib
import json
from pathlib import Path
from maxwell import containers

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,action='append')
    parser.add_argument('--out',type=Path,default=ROOT/'private/freecamera/view_fog_shaders.txt')
    args=parser.parse_args()
    if not args.out.resolve().is_relative_to(ROOT/'private'):
        parser.error('Keep generated profiles under private/')
    archives=args.archive or [ROOT/'private/game/romfs/Shader/Park_UBER_All.Nin_NX_NVN.szs']
    hashes=set();sources=[]
    for path in archives:
        data=containers.read_file(str(path))
        archive=containers.open_blob(data,0,len(data),path.name)
        if not archive:continue
        found={('fragment' if stage.kind=='pixel' else stage.kind,stage.hash) for stage in containers.all_stages(archive)
               if stage.kind in ('vertex','pixel') and '/park_uber/' in stage.id}
        hashes.update(found)
        sources.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),shaders=len(found)))
    if not hashes:parser.error('No Park UBER shaders found')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text('ACNH_VIEW_FOG_V2\n'+'\n'.join(f'{kind} {hash}' for kind,hash in sorted(hashes))+'\n')
    args.out.with_suffix('.json').write_text(json.dumps(dict(sources=sources,shaders=len(hashes)),indent=2)+'\n')
    print(f'{len(hashes)} Park UBER shaders: {args.out}')

if __name__=='__main__':main()
