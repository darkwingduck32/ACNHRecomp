#!/usr/bin/env python3
"""Compile reviewed private asset inputs into native feature resources.
No runtime RomFS mod installation is produced or required. --freeze records
reviewed stock/replacement hashes; normal builds reject changed inputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
from embed_resources import generate
from embed_resources import write_changed

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT/'games/acnh/features/native_assets.lock.json'
PACKAGE = ROOT/'games/acnh/features/deltas'


def inventory(game):
    records = []
    for owner, folder in [('acnh.camera', 'freecamera'), ('acnh.tool-durability', 'tool_durability')]:
        for replacement in sorted((game/'mods'/folder/'romfs').rglob('*')):
            if not replacement.is_file():
                continue
            path = replacement.relative_to(game/'mods'/folder/'romfs').as_posix()
            if replacement.suffix in ('.json', '.txt', '.md', '.log'):
                continue
            source = game/'romfs'/path
            if not source.is_file():
                raise ValueError(f'Stock resource missing: {path}')
            records.append(dict(feature=owner, path=path,
                                input_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                                output_sha256=hashlib.sha256(replacement.read_bytes()).hexdigest()))
    return {'format': 1, 'build_id': 'FF1D1C05670DB6021C85B624A710B963', 'resources': records}


def build(game, output, freeze=False, allow_missing=False, export=False):
    profiles = []
    for path in sorted((ROOT/'games/acnh/features/camera/data').glob('*.txt')):
        checksum = hashlib.sha256(path.read_bytes()).hexdigest()
        profiles.append(dict(feature='acnh.camera.profile', path=path.name,
                             input=str(path), replacement=str(path), input_sha256=checksum, output_sha256=checksum))
    manifest=PACKAGE/'manifest.json'
    if manifest.is_file() and not freeze and not export:
        package=json.loads(manifest.read_text())
        if package['format']!=1 or package['build_id']!=json.loads(LOCK.read_text())['build_id']:
            raise ValueError('Unsupported native delta package')
        expected=json.loads(LOCK.read_text())['resources']
        actual=[{key:item[key] for key in ('feature','path','input_sha256','output_sha256')} for item in package['resources']]
        if actual!=expected:
            raise ValueError('Native delta package differs from reviewed asset lock')
        records=[];total=0
        for item in package['resources']:
            filename=item['payload_sha256']+'.nxdelta'
            payload=PACKAGE/filename
            records.append(dict(item,prebuilt=True,payload=str(payload)))
            total+=payload.stat().st_size
        count,_=generate(records+profiles,output,require_delta_models=True)
        write_changed(Path(output)/'delta-inventory.json',json.dumps(package['resources'],indent=2)+'\n')
        print(f'Native ACNH features: {count} resources from checked distributable deltas ({total} bytes); no model build inputs needed')
        return count
    if not (game/'mods/freecamera/romfs').is_dir():
        if not allow_missing:
            raise ValueError('Reviewed private resource inputs are missing')
        generate(profiles, output)
        return 0
    actual = inventory(game)
    if freeze:
        LOCK.parent.mkdir(parents=True, exist_ok=True)
        LOCK.write_text(json.dumps(actual, indent=2, sort_keys=True)+'\n')
    if not LOCK.is_file() or actual != json.loads(LOCK.read_text()):
        raise ValueError('Native resource inputs differ from the reviewed lock')
    # Retain the existing review lock too: this port must not change the assets.
    from build_camera_package import inventory as camera_inventory, LOCK as CAMERA_LOCK
    if camera_inventory(game) != json.loads(CAMERA_LOCK.read_text()):
        raise ValueError('Camera source review lock mismatch')
    records = []
    # Python diff generation is offline only. The shipped player has a small
    # bounded native decoder and never needs Python or complete new model data.
    try:
        from resource_delta import create
        import zstandard
    except ImportError as exc:
        raise RuntimeError('Install build-only dependencies with: python -m pip install -r tools/requirements-native-features.txt') from exc
    patch_dir=Path(output)/'patches-v1';patch_dir.mkdir(parents=True,exist_ok=True)
    patch_total=0
    for item in actual['resources']:
        folder = 'freecamera' if item['feature'] == 'acnh.camera' else 'tool_durability'
        source=game/'romfs'/item['path'];replacement=game/'mods'/folder/'romfs'/item['path']
        before,after=source.read_bytes(),replacement.read_bytes()
        encoding='zstd-delta' if item['path'].startswith('Model/') else 'delta'
        if encoding=='zstd-delta':
            decoder=zstandard.ZstdDecompressor()
            before=decoder.decompress(before,max_output_size=128*1024*1024)
            after=decoder.decompress(after,max_output_size=128*1024*1024)
        identity=item['input_sha256']+'-'+item['output_sha256']
        patch_path=patch_dir/(identity+'.delta');meta=patch_dir/(identity+'.json')
        patch=patch_path.read_bytes() if patch_path.is_file() else b''
        checksum=hashlib.sha256(patch).hexdigest()
        try:
            recorded=json.loads(meta.read_text()).get('sha256')
        except (OSError,ValueError):
            recorded=None
        if recorded!=checksum:
            patch=create(before,after);patch_path.write_bytes(patch)
            checksum=hashlib.sha256(patch).hexdigest()
            meta.write_text(json.dumps({'sha256':checksum}))
        records.append(dict(item,input=str(source),replacement=str(replacement),encoding=encoding,
                            payload=str(patch_path),payload_sha256=checksum,
                            content_sha256=hashlib.sha256(after).hexdigest(),content_size=len(after)))
        patch_total+=len(patch)
    count, size = generate(records+profiles, output,require_delta_models=True)
    report=[{k:v for k,v in record.items() if k not in ('input','replacement','payload')} for record in records]
    if export:
        PACKAGE.mkdir(parents=True,exist_ok=True)
        for record in records:
            target=PACKAGE/(record['payload_sha256']+'.nxdelta')
            data=Path(record['payload']).read_bytes()
            if not target.is_file() or target.read_bytes()!=data: target.write_bytes(data)
        write_changed(PACKAGE/'manifest.json',json.dumps(dict(format=1,build_id=actual['build_id'],resources=report),indent=2)+'\n')
    write_changed(Path(output)/'delta-inventory.json',json.dumps(report,indent=2)+'\n')
    print(f'Native ACNH features: {count} checked resources; {patch_total} delta bytes; no full models embedded')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=ROOT/'private/game')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--freeze', action='store_true')
    parser.add_argument('--allow-missing', action='store_true')
    parser.add_argument('--export', action='store_true',help='Review/generate the distributable delta package from private source assets')
    args = parser.parse_args()
    build(args.game, args.out, args.freeze, args.allow_missing,args.export)


if __name__ == '__main__':
    main()
