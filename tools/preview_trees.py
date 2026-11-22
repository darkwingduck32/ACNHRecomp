#!/usr/bin/env python3
"""Build an offline, shaded before/after tree viewer from local game assets."""
import argparse
import base64
import bisect
import hashlib
import io
import json
import pathlib
import re
import struct
import urllib.request
import webbrowser

import zstandard
from PIL import Image

from freecamera_trees import Tree, sarc_members, string, u16, u32, u64
from maxwell.bntx import find_textures
from bc.pil_bc import decode

ROOT = pathlib.Path(__file__).resolve().parents[1]


def resources(path):
    data = path.read_bytes()
    if path.suffix == '.zs':
        data = zstandard.ZstdDecompressor().decompress(data)
    if data[:4] == b'SARC':
        return [(name, data[start:end]) for name, start, end in sarc_members(data)
                if data[start:start+4] == b'FRES']
    return [(path.name, data)]


def curve(data, p):
    frames, keys = u64(data, p), u64(data, p+8)
    flags, count = u16(data, p+16), u16(data, p+18)
    if not count or flags & 0x70:
        raise ValueError('Preview currently supports nonempty cubic skeletal curves only')
    frame_type, key_type = flags & 3, (flags >> 2) & 3
    frame_fmt, frame_scale = {0: ('f', 1), 1: ('H', 32), 2: ('B', 1)}[frame_type]
    key_fmt = {0: 'f', 1: 'h', 2: 'b'}[key_type]
    times = [x/frame_scale for x in struct.unpack_from(f'<{count}{frame_fmt}', data, frames)]
    values = struct.unpack_from(f'<{count*4}{key_fmt}', data, keys)
    scale, offset = struct.unpack_from('<2f', data, p+32)
    if times != sorted(times) or len(set(times)) != len(times):
        raise ValueError('Invalid animation key times')
    return dict(target=u32(data, p+20), times=times,
                keys=[values[i:i+4] for i in range(0, len(values), 4)], scale=scale, offset=offset)


def evaluate(track, frame):
    times = track['times']
    frame = max(times[0], min(frame, times[-1]))
    i = max(0, bisect.bisect_right(times, frame)-1)
    t = (frame-times[i])/(times[i+1]-times[i]) if i+1 < len(times) else 0
    a, b, c, d = track['keys'][i]
    return (a+t*(b+t*(c+t*d)))*track['scale']+track['offset']


def animations(path):
    clips = []
    if path is None:
        return clips
    for _, data in resources(path):
        if u32(data, 8) != 0x90000:
            raise ValueError('Only BFRES 9 animations are supported')
        cursor = 0
        while (p := data.find(b'FSKA', cursor)) >= 0:
            cursor = p+4
            if not u32(data, p+4) & 0x1000:
                raise ValueError('Only Euler XYZ animation is supported')
            clip = dict(name=string(data, u64(data, p+8)), frames=u32(data, p+64), bones={})
            for i in range(u16(data, p+76)):
                q = u64(data, p+40)+56*i
                flags, base = u32(data, q+40), u64(data, q+16)
                constants = {}
                for bit, targets in ((8, (4, 8, 12)), (16, (32, 36, 40, 44)), (32, (16, 20, 24))):
                    if flags & bit:
                        for target in targets:
                            constants[target] = struct.unpack_from('<f', data, base)[0]
                            base += 4
                tracks = [curve(data, u64(data, q+8)+48*j) for j in range(data[q+46])]
                clip['bones'][string(data, u64(data, q))] = (constants, tracks)
            clips.append(clip)
    return clips


def skeleton(data, model):
    p = u64(data, model+24)
    bones = []
    for i in range(u16(data, p+56)):
        q = u64(data, p+16)+96*i
        quaternion = u32(data, q+52) & 0x7000 != 0x1000
        values = list(struct.unpack_from('<10f', data, q+56))
        parent = struct.unpack_from('<h', data, q+42)[0]
        if parent >= i:
            raise ValueError('Skeleton is not in parent-first order')
        bones.append(dict(name=string(data, u64(data, q)), parent=parent, transform=values, quaternion=quaternion))
    count = u16(data, p+58)+u16(data, p+60)
    palette = list(struct.unpack_from(f'<{count}H', data, u64(data, p+24)))
    return bones, palette


def attribute(tree, vertex, name):
    attr = next((a for a in tree.attributes(vertex) if a[0] == name), None)
    if attr is None:
        return None
    _, fmt, offset, index = attr
    start, _, stride = list(tree.buffers(vertex))[index]
    result = []
    for i in range(u32(tree.data, vertex+80)):
        p = start+offset+i*stride
        if fmt == 0x20e:
            v = u32(tree.data, p)
            result.append([max(-1, ((v >> k & 1023)-1024 if v >> k & 512 else v >> k & 1023)/511)
                           for k in (0, 10, 20)])
        elif fmt in (0x112, 0x212, 0x512):
            code, divisor = {0x112: ('H', 65535), 0x212: ('h', 32767), 0x512: ('e', 1)}[fmt]
            result.append([max(-1, x/divisor) for x in struct.unpack_from('<2'+code, tree.data, p)])
        elif fmt == 0x302:
            result.append(tree.data[p])
        elif fmt in (0x309, 0x30b, 0x109, 0x10b):
            count = 2 if fmt in (0x309, 0x109) else 4
            divisor = 255 if fmt in (0x109, 0x10b) else 1
            result.append([x/divisor if divisor != 1 else x for x in tree.data[p:p+count]])
        elif fmt in (0x515, 0x115):
            result.append([x/(65535 if fmt == 0x115 else 1) for x in struct.unpack_from('<4'+('e' if fmt == 0x515 else 'H'), tree.data, p)])
        else:
            raise ValueError(f'Unsupported {name} format {fmt:x}')
    return result


def texture_image(texture):
    """Deswizzle level zero with the same GOB layout as nvn_tiling.cpp."""
    if texture.layers != 1 or texture.depth != 1:
        raise ValueError('Only 2D single-layer preview textures are supported')
    rows = (texture.height+texture.block_h-1)//texture.block_h
    pitch = ((texture.width+texture.block_w-1)//texture.block_w)*texture.bpb
    bh = 1 << texture.block_height_log2
    tiled = texture.tiled()
    linear = bytearray(pitch*rows)
    for y in range(rows):
        for x in range(pitch):
            source = (y//(8*bh))*((pitch+63)//64)*512*bh+(x//64)*512*bh
            source += (y//8 % bh)*512+(x%64//32)*256+(y%8//2)*64+(x%32//16)*32+(y%2)*16+x%16
            if texture.tile_mode == 1:
                source = y*((pitch+31)//32*32)+x
            linear[y*pitch+x] = tiled[source]
    channel = texture.format >> 8
    if channel == 0xb:
        return Image.frombytes('RGBA', (texture.width, texture.height), bytes(linear))
    if channel in (0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x20):
        return Image.frombytes('RGBA', (texture.width, texture.height),
                               decode({0x1a: 1, 0x1b: 2, 0x1c: 3, 0x1d: 4, 0x1e: 5, 0x20: 7}[channel], texture.width, texture.height, bytes(linear)))
    raise ValueError(f'Unsupported preview texture {texture.format_name}')


def textures(path):
    result = {}
    for texture in find_textures(str(path)):
        if not texture.name.endswith(('_OP', '_AlbGry', '_Alb')):
            continue
        image = texture_image(texture)
        stream = io.BytesIO()
        image.save(stream, format='PNG')
        result[texture.name] = 'data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode()
    return result


def material_faces(data, model, shape):
    """Read BFRES v9 FMAT render info through the shape's material index.

    Layout reference: BfresLibrary Switch/Model/MaterialParser.cs and
    Shared/Models/Material/RenderInfo.cs. Never infer culling from a name.
    """
    index = u16(data, shape+82)
    if index >= u16(data, model+108):
        raise ValueError('Shape material index is outside its model')
    material = u64(data, model+56)+168*index
    if data[material:material+4] != b'FMAT':
        raise ValueError('Expected BFRES v9 material record')
    name = string(data, u64(data, material+8))
    for i in range(u16(data, material+154)):
        info = u64(data, material+16)+24*i
        if string(data, u64(data, info)) != 'gsys_render_state_display_face':
            continue
        if data[info+18] != 2 or u16(data, info+16) != 1 or not u64(data, info+8):
            raise ValueError(f'{name}: invalid display-face render info')
        face = string(data, u64(data, u64(data, info+8)))
        if face not in ('front', 'back', 'both', 'none'):
            raise ValueError(f'{name}: unsupported display face {face!r}')
        return name, face
    return name, None


def export(path, clips):
    result = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), models={}, textures=textures(path))
    for member, data in resources(path):
        tree = Tree(data)
        cursor = 0
        while (m := data.find(b'FMDL', cursor)) >= 0:
            cursor = m+4
            name = string(data, u64(data, m+8))
            bones, palette = skeleton(data, m)
            model = dict(bones=bones, shapes=[], clips=[])
            for i in range(u16(data, m+106)):
                p = u64(data, m+40)+96*i
                v = u64(data, p+16)
                material, display_face = material_faces(data, m, p)
                skin = u16(data, v+84)
                indices = attribute(tree, v, '_i0') if skin else None
                shape = dict(name=string(data, u64(data, p+8)),
                    material=material, displayFace=display_face,
                    positions=tree.positions(v), normals=attribute(tree, v, '_n0'),
                    uv=attribute(tree, v, '_u0'), triangles=tree.triangles(p),
                    bones=[palette[j] for j in indices] if skin == 1 else [u16(data, p+84)]*u32(data, v+80))
                if skin > 1:
                    weights = attribute(tree, v, '_w0')
                    shape['weights'] = []
                    for ids, ws in zip(indices, weights):
                        pairs = [(palette[int(b)], w) for b, w in zip(ids[:skin], ws[:skin]) if w > 0]
                        total = sum(w for _, w in pairs)
                        if not .98 <= total <= 1.02:
                            raise ValueError(f'{name}: invalid skin weights: {total}')
                        shape['weights'].append([(b, w/total) for b, w in pairs])
                model['shapes'].append(shape)
            if name.endswith('Node'):
                for clip in clips:
                    bind_name = re.sub('Snow|Sakura', '', name.removesuffix('Node'))
                    bind_name = re.sub('CedarDeco[AB]', 'Cedar4', bind_name)
                    if not clip['name'].startswith(bind_name):
                        continue
                    if set(clip['bones'])-set(b['name'] for b in bones):
                        raise ValueError(f"{clip['name']}: animation bones do not match {name}")
                    samples = []
                    for frame in range(clip['frames']+1):
                        transforms = []
                        for bone in bones:
                            values = bone['transform'].copy()
                            constants, tracks = clip['bones'].get(bone['name'], ({}, []))
                            targets = {4: 0, 8: 1, 12: 2, 32: 3, 36: 4, 40: 5, 44: 6, 16: 7, 20: 8, 24: 9}
                            for target, value in constants.items():
                                values[targets[target]] = value
                            for track in tracks:
                                values[targets[track['target']]] = evaluate(track, frame)
                            transforms.append(values)
                        samples.append(transforms)
                    model['clips'].append(dict(name=clip['name'], frames=clip['frames'], samples=samples))
            result['models'][name] = model
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=pathlib.Path, default=ROOT/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs')
    parser.add_argument('--after', type=pathlib.Path, default=ROOT/'private/game/mods/freecamera/romfs/Model/PltTreeOak.Nin_NX_NVN.zs')
    parser.add_argument('--animations', type=pathlib.Path, default=ROOT/'private/game/romfs/Model/PltTreeOakAnim.Nin_NX_NVN.zs')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT/'private/freecamera/preview/index.html')
    parser.add_argument('--open', action='store_true')
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to(ROOT/'private'):
        parser.error('Keep game-derived previews under private/')
    clips = animations(args.animations)
    payload = dict(before=export(args.before, clips), after=export(args.after, clips))
    library = ROOT/'private/third_party/tree-preview/three.min.js'
    if not library.exists():
        library.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve('https://unpkg.com/three@0.160.1/build/three.min.js', library)
    template = pathlib.Path(__file__).with_name('tree_preview.html').read_text(encoding='utf-8')
    html = template.replace('/* THREE_LIBRARY */', library.read_text(encoding='utf-8'))
    html = html.replace('/* TREE_DATA */', json.dumps(payload, separators=(',', ':'), allow_nan=False).replace('</', '<\\/'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(html, encoding='utf-8')
    print(f'Wrote {args.output.resolve()} ({len(html):,} characters); {len(clips)} source clips')
    if args.open:
        webbrowser.open(args.output.resolve().as_uri())


if __name__ == '__main__':
    main()
