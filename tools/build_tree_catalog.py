#!/usr/bin/env python3
"""Build one offline review UI for all generated and retained tree models."""
import argparse
import json
import pathlib
import re

from preview_trees import ROOT, animations, export
from tree_fruit_layout import attach_preview, write_profiles


def animation_path(name):
    for family in ('Oak', 'Cedar', 'Bamboo', 'Palm'):
        if name.startswith('PltTree'+family):
            return ROOT/'private/game/romfs/Model'/f'PltTree{family}Anim.Nin_NX_NVN.zs'
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--forest', type=pathlib.Path, default=ROOT/'private/freecamera/forest-v6')
    parser.add_argument('--previous', type=pathlib.Path, default=ROOT/'private/freecamera/forest-v5')
    parser.add_argument('--reviews', type=pathlib.Path, default=ROOT/'private/freecamera/tree-review/tree-reviews-2.json')
    parser.add_argument('--out', type=pathlib.Path, default=ROOT/'private/freecamera/tree-review')
    args = parser.parse_args()
    if not args.out.resolve().is_relative_to(ROOT/'private'):
        parser.error('Keep game-derived previews under private/')
    report = json.loads((args.forest/'report.json').read_text())
    fruit_assets={species:export(ROOT/f'private/game/romfs/Model/UnitIconPltFruit{species}.Nin_NX_NVN.zs',[]) for species in ('Apple','Orange','Pear','Peach','Cherry','Coconut')}
    write_profiles(args.forest/'tree_fruit_offsets.txt')
    assets, models, audited = [], 0, []
    for row in sorted(report['assets'], key=lambda r: (r['file']!='PltTreeOak.Nin_NX_NVN.zs', not r['file'].startswith('PltTree'), r['file'])):
        name = row['file']
        if 'Wreath' in name:
            continue
        before = ROOT/'private/game/romfs/Model'/name
        after = args.forest/'romfs/Model'/name
        if row['status'] != 'modified':
            after = before
        clips = animations(animation_path(name))
        old, new = export(before, clips), export(after, clips)
        previous_path=args.previous/'romfs/Model'/name
        previous=export(previous_path,clips) if previous_path.exists() else old
        # Avoid aliasing the stock object when no previous override exists.
        if previous is old:
            import copy
            previous=copy.deepcopy(old)
        attach_preview(old,fruit_assets)
        attach_preview(new,fruit_assets,revised=True)
        attach_preview(previous,fruit_assets)
        if not name.startswith(('PltTree','Ftr')) and 'Tree' not in name:
            # A museum scene can contain many unrelated plants and props.
            tree_models = {m for m, data in old['models'].items() if any(
                re.search('Tree|Mangrove', s['name'].split('__')[-1]) for s in data['shapes'])}
            old['models'] = {m: data for m, data in old['models'].items() if m in tree_models}
            new['models'] = {m: data for m, data in new['models'].items() if m in tree_models}
            previous['models']={m:data for m,data in previous['models'].items() if m in tree_models}
        audited.append(dict(file=name, status=row['status'], models=list(old['models'])))
        if not old['models']:
            continue
        slug=name.removesuffix('.Nin_NX_NVN.zs')
        label=slug.replace('PltTree', '').replace('IdrObj', '').replace('Ftr', '')
        label=re.sub(r'(Snow|Sakura|Deco)', r' · \1', label)
        label += ' · modified' if row['status']=='modified' else ' · original retained'
        payload=dict(before=old, after=new, previous=previous, meta=row)
        path=args.out/'assets'/f'{slug}.js'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('window.TREE_ASSET='+json.dumps(payload,separators=(',', ':'),allow_nan=False).replace('</','<\\/')+';\n', encoding='utf-8')
        assets.append(dict(label=label, file=name, data='assets/'+path.name, models=list(old['models']), status=row['status']))
        models += len(old['models'])
        print(slug, len(old['models']), flush=True)
    config=dict(assets=assets, modelCount=models, reviews=json.loads(args.reviews.read_text()) if args.reviews.exists() else {})
    library=ROOT/'private/third_party/tree-preview/three.min.js'
    template=(ROOT/'tools/tree_preview.html').read_text(encoding='utf-8')
    html=template.replace('/* THREE_LIBRARY */',library.read_text(encoding='utf-8')).replace('/* TREE_DATA */',json.dumps(config))
    args.out.joinpath('index.html').write_text(html,encoding='utf-8')
    args.out.joinpath('catalog.json').write_text(json.dumps(config,indent=2)+'\n')
    args.out.joinpath('audit.json').write_text(json.dumps(audited,indent=2)+'\n')
    print(f'Built {args.out}/index.html: {len(assets)} assets, {models} models')


if __name__=='__main__':
    main()
