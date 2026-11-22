#!/usr/bin/env python3
"""Build the pinned camera feature and optional durability package from local assets.
No installation logs, backups, saves, or local configuration are copied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile
from port_camera_mod import members, leaves

ROOT=Path(__file__).resolve().parents[1]
LOCK=ROOT/'games/acnh/features/camera/assets.lock.json'
BUILD_ID='FF1D1C05670DB6021C85B624A710B963'

def sha(data): return hashlib.sha256(data).hexdigest()

def compose_parameters(base, overlays):
    """Three-way merge at BYML scalar boundaries; reject conflicting edits/layouts."""
    layout=members(base); output=bytearray(base); edits={}
    for label, data in overlays:
        if members(data)!=layout or len(data)!=len(base):
            raise ValueError(f'{label}: parameter archive layout differs from the base')
        covered=set()
        for member,(start,end,_) in layout.items():
            original=base[start:end]; replacement=data[start:end]
            if original==replacement: continue
            if original[:2] not in (b'BY',b'YB'): raise ValueError(f'{label}: unsupported member {member}')
            before,after=leaves(original),leaves(replacement)
            if before.keys()!=after.keys(): raise ValueError(f'{label}: changed field layout in {member}')
            for path,(kind,_,offset) in before.items():
                other_kind,_,other_offset=after[path]
                if (kind,offset)!=(other_kind,other_offset): raise ValueError(f'{label}: field layout changed')
                old=original[offset:offset+4]; new=replacement[offset:offset+4]
                if old==new:continue
                key=(member,path)
                if key in edits and edits[key]!=new:raise ValueError(f'Conflicting parameter edits: {member}/{path}')
                edits[key]=new
                output[start+offset:start+offset+4]=new
                covered.update(range(start+offset,start+offset+4))
        if any(a!=b and i not in covered for i,(a,b) in enumerate(zip(base,data))):
            raise ValueError(f'{label}: edits outside supported scalar fields')
    return bytes(output)

def parameter_profile(base, camera):
    # Pin only our camera edits. Apply these to the archive selected by LayeredFS,
    # preserving every unrelated edit made by independently bundled mods.
    compose_parameters(base,[('camera',camera)])
    boundary=struct.unpack_from('<I' if base[6:8]==b'\xff\xfe' else '>I',base,12)[0]
    rows=[f'ACNH_CAMERA_PARAMS_V1 {len(base)} {boundary} {sha(base[:boundary])}']
    for member,(start,end,_) in members(base).items():
        if base[start:end]==camera[start:end]:continue
        for path,(kind,value,offset) in leaves(base[start:end]).items():
            at=start+offset; old=base[at:at+4];new=camera[at:at+4]
            if old!=new:rows.append(f'{at} {old.hex()} {new.hex()}')
    return ('\n'.join(rows)+'\n').encode()

def inventory(game):
    files={}
    for mod in ('freecamera','camera303','tool_durability'):
        for p in sorted((game/'mods'/mod/'romfs').rglob('*')):
            if p.is_file() and not p.name.endswith(('.json','.log','.md','.txt')):
                files[p.relative_to(game).as_posix()]=sha(p.read_bytes())
    for name in ('view_fog_shaders.txt','tree_fruit_offsets.txt'):
        p=game/'mods/freecamera'/name;files[p.relative_to(game).as_posix()]=sha(p.read_bytes())
    p=game/'romfs/Pack/StaticParam.pack';files[p.relative_to(game).as_posix()]=sha(p.read_bytes())
    return {'format':1,'build_id':BUILD_ID,'revision':'reviewed-forest-v6-camera-1','files':files}

def archive(out,id,files,runtime,conflicts):
    manifest={'format':1,'id':id,'version':'1.0.0','runtime':runtime,'build_ids':[BUILD_ID],
              'conflicts':conflicts,'files':{k:sha(v) for k,v in sorted(files.items())}}
    files=dict(files);files['manifest.json']=(json.dumps(manifest,indent=2,sort_keys=True)+'\n').encode()
    out.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(out/(id+'.zip'),'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for path,data in sorted(files.items()):
            i=zipfile.ZipInfo(id+'/'+path,(1980,1,1,0,0,0));i.compress_type=zipfile.ZIP_DEFLATED;i.create_system=3;i.external_attr=0o100644<<16;z.writestr(i,data)
    with zipfile.ZipFile(out/(id+'.zip')) as z:
        if z.testzip():raise ValueError('ZIP CRC verification failed')
        for path,data in files.items():
            if z.read(id+'/'+path)!=data:raise ValueError('ZIP content verification failed')
    print(f'{id}: {len(files)} files; SHA-256 {sha((out/(id+".zip")).read_bytes())}')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',type=Path,default=ROOT/'private/game')
    p.add_argument('--out',type=Path,default=ROOT/'private/share/packages')
    p.add_argument('--freeze',action='store_true',help='Explicitly update the reviewed source asset lock')
    a=p.parse_args()
    if not a.out.resolve().is_relative_to(ROOT/'private'):p.error('Keep derived assets under private/')
    from configparser import ConfigParser
    # Both importer and local builds carry build_id in their version/game INI files.
    import re
    ids=[]
    for name in ('game.ini','version.ini'):
        f=a.game/name
        if f.is_file():ids+=re.findall(r'(?m)^\s*build_id\s*=\s*([0-9A-Fa-f]{32})',f.read_text())
    if not ids or any(x.upper()!=BUILD_ID for x in ids):p.error('This recipe supports only ACNH 3.0.3 build FF1D1C05...')
    lock=inventory(a.game)
    if a.freeze:LOCK.write_text(json.dumps(lock,indent=2,sort_keys=True)+'\n')
    expected=json.loads(LOCK.read_text())
    if lock!=expected:p.error('Local assets differ from the reviewed lock; review them before --freeze')
    camera={};durability={}
    for rel in lock['files']:
        parts=rel.split('/');data=(a.game/rel).read_bytes()
        if parts[:2]==['mods','freecamera']:
            path='/'.join(parts[2:]);camera['profiles/'+path if '/' not in path else path]=data
        elif parts[:2]==['mods','tool_durability']:durability['/'.join(parts[2:])]=data
    base=(a.game/'romfs/Pack/StaticParam.pack').read_bytes()
    camera['profiles/parameter_edits.txt']=parameter_profile(base,(a.game/'mods/camera303/romfs/Pack/StaticParam.pack').read_bytes())
    camera['profiles/terrain_scale.txt']=b'2\n'
    camera['README.txt']=b'360 Camera 1.0.0\nInstall using the updated launcher Mods manager and restart.\nIncludes reviewed forest-v6 trees, clouds, ocean, camera parameter edits. SpeedFix stays separately managed by the release.\nRequires the matching player and imported guest library with the camera instruction hooks.\nReplaces the legacy freecamera and camera303 configuration. Leaves native bundled mods enabled.\nRight stick: orbit. Hold R3 + vertical stick: zoom. Tap R3: reset. F6 + I/K: zoom; F7: reset.\nTool durability is a separate optional package.\n'
    archive(a.out,'freecamera',camera,'acnh.camera.v1',['camera303'])
    archive(a.out,'tool_durability',durability,'romfs.v1',[])

if __name__=='__main__':main()
