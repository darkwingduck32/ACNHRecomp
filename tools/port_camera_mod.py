#!/usr/bin/env python3
"""Port Alree's 2.0.0 camera values onto a newer local StaticParam.pack.

Only matching float leaves in Param/Camera/CameraParams.byml are transferred.
Optionally scale foreground/tree visibility and furniture visibility/creation ranges.
The target archive layout and all other members remain byte-for-byte intact.
No game data is bundled. Outputs must stay under private/.
"""
import argparse
import hashlib
import math
import zlib
import pathlib,struct,json

def members(d):
 e='<' if d[6:8]==b'\xff\xfe' else '>'
 h=struct.unpack_from(e+'H',d,4)[0]; off=struct.unpack_from(e+'I',d,12)[0]
 count=struct.unpack_from(e+'H',d,h+6)[0]; names=h+12+16*count+8
 out={}
 for i in range(count):
  node=h+12+16*i
  _,a,s,t=struct.unpack_from(e+'IIII',d,node)
  p=names+(a&0xffffff)*4; name=d[p:d.index(b'\0',p)].decode()
  out[name]=(off+s,off+t,node)
 return out
def leaves(d):
 e='<' if d[:2]==b'YB' else '>'
 u=lambda p:struct.unpack_from(e+'I',d,p)[0]
 def count(p):return int.from_bytes(d[p+1:p+4],'little' if e=='<' else 'big')
 def strings(p):
  if not p:return []
  return [d[p+u(p+4+4*i):d.index(b'\0',p+u(p+4+4*i))].decode() for i in range(count(p))]
 keys=strings(u(4)); ss=strings(u(8)); out={}
 def val(t,p,path):
  v=u(p)
  if t in (0xc0,0xc1):walk(v,path)
  else:
   v=ss[v] if t==0xa0 else struct.unpack_from(e+'f',d,p)[0] if t==0xd2 else struct.unpack_from(e+'i',d,p)[0] if t==0xd1 else v
   out[path]=(t,v,p)
 def walk(p,path):
  t=d[p]; n=count(p)
  for i in range(n):
   if t==0xc1:
    q=p+4+8*i; k=int.from_bytes(d[q:q+3],'little' if e=='<' else 'big');val(d[q+3],q+4,path+(keys[k],))
   elif t==0xc0:val(d[p+4+i],(p+4+n+3)//4*4+4*i,path+(i,))
   else:raise ValueError(hex(t))
 walk(u(12),());return out

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=pathlib.Path, required=True)
    parser.add_argument('--base', type=pathlib.Path, required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--draw-distance-scale', type=float, default=1.0,
                        help='Scale outdoor foreground/tree and furniture visibility/creation bounds (1 to 4; default 1).')
    args = parser.parse_args()
    if not math.isfinite(args.draw_distance_scale) or not 1 <= args.draw_distance_scale <= 4:
        parser.error('--draw-distance-scale must be between 1 and 4.')
    private = pathlib.Path(__file__).resolve().parents[1] / 'private'
    if not args.out.resolve().is_relative_to(private):
        parser.error('Keep game-derived output under private/.')
    if args.out.resolve() in (args.base.resolve(), args.reference.resolve()):
        parser.error('Output must not overwrite either input.')
    source = args.reference.read_bytes()
    base = args.base.read_bytes()
    if source[:4] != b'SARC' or base[:4] != b'SARC':
        parser.error('Both inputs must be uncompressed SARC archives.')
    if source[6:8] != base[6:8]:
        parser.error('Source and target archive byte orders must match.')
    name = 'Param/Camera/CameraParams.byml'
    ss, st, _ = members(source)[name]
    bs, bt, _ = members(base)[name]
    src = source[ss:st]
    dst = base[bs:bt]
    if src[:2] != dst[:2] or src[:2] not in (b'YB', b'BY'):
        parser.error('Camera tables must be BYML with matching byte orders.')
    source_leaves, target_leaves = leaves(src), leaves(dst)
    output = bytearray(base)
    changes = []
    skipped = []
    for path, (kind, value, offset) in source_leaves.items():
        if path not in target_leaves:
            skipped.append(list(path))
            continue
        target_kind, old, target_offset = target_leaves[path]
        if kind != target_kind:
            raise ValueError(f'Camera field type mismatch: {path}')
        if value == old:
            continue
        if kind != 0xd2:
            raise ValueError(f'Unexpected non-float difference: {path}')
        output[bs+target_offset:bs+target_offset+4] = src[offset:offset+4]
        changes.append({'path': list(path), 'before': old, 'after': value})
    # Verify all source camera values and retain target-only fields.
    actual = leaves(output[bs:bt])
    for path, (kind, value, _) in source_leaves.items():
        if path in target_leaves:
            assert actual[path][:2] == (kind, value), path
    for path in target_leaves.keys() - source_leaves.keys():
        assert actual[path] == target_leaves[path], path
    assert output[:bs] == base[:bs] and output[bt:] == base[bt:]
    distance_changes = []
    changed_members = {name}
    if args.draw_distance_scale != 1:
        def scaled_member(member, root, fields, presets=(None,)):
            start, end, _ = members(base)[member]
            original = base[start:end]
            original_leaves = leaves(original)
            endian = '<' if original[:2] == b'YB' else '>'
            patched = bytearray(original)
            expected_changes = {}
            key = lambda text: format(zlib.crc32(text.encode()), 'x')
            for preset in presets:
                prefix = (key(root),) if preset is None else (key(root), key('mParams'), key(preset))
                for field in fields:
                    path = prefix + (key(field),)
                    kind, old, offset = original_leaves[path]
                    if kind != 0xd2 or old < 0:
                        raise ValueError(f'Unexpected visibility bound: {member}/{field}: {old}')
                    # Keep zero extensions disabled; scale distances from the stock base,
                    # never from a previously generated pack.
                    new = old * args.draw_distance_scale
                    struct.pack_into(endian + 'f', patched, offset, new)
                    expected_changes[path] = struct.unpack_from(endian + 'f', patched, offset)[0]
                    if new != old:
                        distance_changes.append({'member': member, 'preset': preset,
                                                 'field': field, 'before': old, 'after': new})
            output[start:end] = patched
            updated = leaves(patched)
            assert updated.keys() == original_leaves.keys()
            for path, (kind, old, offset) in original_leaves.items():
                assert updated[path] == (kind, expected_changes.get(path, old), offset), path
            changed_members.add(member)

        foreground_fields = [f'm{group}Area{side}.f32'
                             for group in ('', 'Tree', 'Landing')
                             for side in ('Left', 'Right', 'Near', 'Far')]
        foreground_fields += ['mAreaWidthBuff.f32', 'mAreaNearBuff.f32', 'mAreaFarBuff.f32',
                              'mLandingAreaWidthBuff.f32', 'mLandingAreaNearFarBuff.f32']
        scaled_member('Param/Fg/AreaParam.byml', 'AreaParam', foreground_fields)
        furniture_fields = [f'm{group}Range{side}.f32'
                            for group in ('Camera', 'Create')
                            for side in ('Left', 'Right', 'Near', 'Far')]
        furniture_fields += [f'mCreateRange{side}Extension.f32'
                             for side in ('Left', 'Right', 'Near', 'Far')]
        furniture_fields += ['mRangeWidthBuff.f32', 'mRangeNearBuff.f32', 'mRangeFarBuff.f32',
                             'mWherearenAddRight.f32', 'mWherearenAddFar.f32']
        scaled_member('Param/Ftr/DynamicGenerateParams.byml', 'DynamicGenerateParams',
                      furniture_fields, ('MainField', 'DemoLanding'))
    for member, (start, end, _) in members(base).items():
        if member not in changed_members:
            assert output[start:end] == base[start:end], member
    assert members(output) == members(base)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(output)
    report = {'reference_sha256': hashlib.sha256(source).hexdigest(),
              'base_sha256': hashlib.sha256(base).hexdigest(),
              'output_sha256': hashlib.sha256(output).hexdigest(),
              'changed_member': name, 'changed_values': len(changes),
              'draw_distance_scale': args.draw_distance_scale,
              'draw_distance_changes': distance_changes,
              'changes': changes, 'removed_reference_fields': skipped,
              'preserved_new_fields': [list(p) for p in target_leaves.keys() - source_leaves.keys()]}
    args.out.with_suffix('.report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Wrote {args.out}: {len(changes)} camera values, '
          f'{len(distance_changes)} visibility bounds; all other members unchanged.')

if __name__ == '__main__':
    main()
