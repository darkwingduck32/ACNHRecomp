#!/usr/bin/env python3
"""Render local alpha-tested tree turnarounds without a browser or the game."""
import argparse
import base64
import io
import json
import math
import pathlib

import numpy as np
from PIL import Image, ImageDraw

from preview_trees import ROOT, animations, export


def matrices(model, clip_name=None, frame=0):
    result = []
    clip = next((c for c in model['clips'] if c['name'] == clip_name), None)
    if clip_name and not clip:
        raise ValueError(f'Clip not available: {clip_name}')
    for index, bone in enumerate(model['bones']):
        v = clip['samples'][min(frame, clip['frames'])][index] if clip else bone['transform']
        rx, ry, rz = v[3:6]
        cx, cy, cz, sx, sy, sz = math.cos(rx), math.cos(ry), math.cos(rz), math.sin(rx), math.sin(ry), math.sin(rz)
        r = np.array(((cz, -sz, 0), (sz, cz, 0), (0, 0, 1)))@np.array(((cy, 0, sy), (0, 1, 0), (-sy, 0, cy)))@np.array(((1, 0, 0), (0, cx, -sx), (0, sx, cx)))
        if bone.get('quaternion'):
            x,y,z,w=v[3:7]
            r=np.array(((1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w),
                        (2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w),
                        (2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y)))
        r=r@np.diag(v[:3])
        t = np.array(v[7:10])
        if bone['parent'] >= 0:
            pr, pt = result[bone['parent']]
            t, r = pr@t+pt, pr@r
        result.append((r, t))
    return result


def prepare(asset, name, clip_name=None, frame=0):
    model = asset['models'][name]
    bones = matrices(model, clip_name, frame)
    bind = matrices(model)
    textures = {n: np.array(Image.open(io.BytesIO(base64.b64decode(url.split(',')[1]))).convert('RGBA'))
                for n, url in asset['textures'].items()}
    shapes = []
    for src in model['shapes']:
        if 'Shadow' in src['name'] or not src['triangles']:
            continue
        positions, normals = [], []
        for i, point in enumerate(src['positions']):
            r, t = bones[src['bones'][i]]
            if src.get('weights'):
                r,t=np.zeros((3,3)),np.zeros(3)
                for index, weight in src['weights'][i]:
                    current_r,current_t=bones[index];bind_r,bind_t=bind[index]
                    skin_r=current_r@np.linalg.inv(bind_r)
                    r+=skin_r*weight;t+=(current_t-skin_r@bind_t)*weight
            positions.append(r@point+t)
            normals.append(np.linalg.inv(r).T@src['normals'][i])
        base = src['name'].split('__')[-1]
        fullcolor = base+'_Alb' in textures
        shapes.append(dict(points=np.array(positions), normals=np.array(normals), uv=np.array(src['uv'] if src['uv'] else [[0,0]]*len(positions)),
            display_face=src.get('displayFace') or 'front',
            triangles=src['triangles'], alpha=textures.get(base+'_OP'), gray=textures.get(base+'_Alb') if fullcolor else textures.get(base+'_AlbGry'),
            tint=np.array((255,255,255) if fullcolor or src.get('fruitSpecies') or 'WinterSnow' in base else (158, 121, 83) if 'Trunk' in src['name'] else (165, 207, 113)),
            foliage=any(token in src['name'] for token in ('Leaf', 'Bloom', 'Pineneedles'))))
    return shapes


def render(shapes, angle, bounds, width=280, height=330, elevation=0):
    yaw = math.radians(angle)
    camera = np.array(((math.cos(yaw), 0, -math.sin(yaw)), (0, 1, 0), (math.sin(yaw), 0, math.cos(yaw))))
    pitch=math.radians(elevation)
    camera=np.array(((1,0,0),(0,math.cos(pitch),-math.sin(pitch)),(0,math.sin(pitch),math.cos(pitch))))@camera
    image = np.zeros((height, width, 3), dtype=np.uint8)
    image[:] = (24, 37, 45)
    zbuffer = np.full((height, width), -np.inf)
    foliage = np.zeros((height, width), dtype=bool)
    center, scale, bottom = bounds
    light = np.array((.4, .8, .6));light /= np.linalg.norm(light)
    for shape in shapes:
        world = shape['points']
        p = world@camera.T
        p[:, 0] = (p[:, 0]-center[0])*scale+width/2
        p[:, 1] = height-20-(p[:, 1]-bottom)*scale
        for tri in shape['triangles']:
            a, b, c = p[list(tri)]
            denominator = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            face=shape.get('display_face','front')
            if abs(denominator)<1e-8 or face=='none' or (face=='front' and denominator>0) or (face=='back' and denominator<0):
                continue
            xmin, ymin = np.maximum(0, np.floor(np.min(p[list(tri), :2], axis=0))).astype(int)
            xmax, ymax = np.minimum((width-1, height-1), np.ceil(np.max(p[list(tri), :2], axis=0))).astype(int)
            if xmin > xmax or ymin > ymax:
                continue
            yy, xx = np.mgrid[ymin:ymax+1, xmin:xmax+1];xx=xx+.5;yy=yy+.5
            wa = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
            wb = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
            wc = 1-wa-wb
            weights = np.stack((wa, wb, wc), axis=-1)
            depth = weights@p[list(tri), 2]
            region = np.s_[ymin:ymax+1, xmin:xmax+1]
            mask = (wa >= 0)&(wb >= 0)&(wc >= 0)&(depth > zbuffer[region])
            uv = weights@shape['uv'][list(tri)]
            def sample(texture, rgb=False):
                h, w = texture.shape[:2]
                result=texture[(uv[..., 1]%1*h).astype(int), (uv[..., 0]%1*w).astype(int)]
                return result[...,:3]/255 if rgb else result[...,0]/255
            if shape['alpha'] is not None:
                mask &= sample(shape['alpha']) >= .5
            normals = weights@shape['normals'][list(tri)]
            if denominator>0:
                normals=-normals
            normals /= np.maximum(np.linalg.norm(normals, axis=-1, keepdims=True), 1e-9)
            brightness = .48+.52*np.maximum(0, normals@light)
            gray = sample(shape['gray'], True) if shape['gray'] is not None else 1
            color = np.clip(brightness[..., None]*gray*shape['tint']*1.45, 0, 255).astype(np.uint8)
            image[region][mask] = color[mask]
            zbuffer[region][mask] = depth[mask]
            foliage[region][mask] = shape['foliage']
    return Image.fromarray(image), int(foliage.sum())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=pathlib.Path, default=ROOT/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs')
    parser.add_argument('--after', type=pathlib.Path, required=True)
    parser.add_argument('--model', default='PltTreeOak4Node')
    parser.add_argument('--clip', help='Exact game clip name, for example PltTreeOak4ShakeL')
    parser.add_argument('--animations', type=pathlib.Path, default=ROOT/'private/game/romfs/Model/PltTreeOakAnim.Nin_NX_NVN.zs')
    parser.add_argument('--frame', type=int, default=0)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--elevation', type=float, default=0)
    parser.add_argument('--before-label', default='Stock')
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to(ROOT/'private'):
        parser.error('Keep game-derived renders under private/')
    if args.frame < 0:
        parser.error('Frame must be nonnegative')
    clips = animations(args.animations) if args.clip else []
    assets = [export(args.before, clips), export(args.after, clips)]
    shapes = [prepare(asset, args.model, args.clip, args.frame) for asset in assets]
    points = np.concatenate([s['points'] for group in shapes for s in group])
    bottom, top = points[:, 1].min(), points[:, 1].max()
    radius = max(np.max(np.linalg.norm(points[:, (0, 2)], axis=1)), 1)
    bounds = ((0, 0), min(240/(radius*2), 285/(top-bottom)), bottom)
    if args.elevation:
        # Keep the same camera framing for both rows at every azimuth.
        center_y=(top+bottom)/2
        for group in shapes:
            for shape in group:
                shape['points']=shape['points']-np.array((0,center_y,0))
        extent=max(radius,(top-bottom)/2)*1.35
        bounds=((0,0),min(240/(2*extent),285/(2*extent)),-extent)
    sheet = Image.new('RGB', (280*8, 380*2), (17, 26, 32));draw = ImageDraw.Draw(sheet)
    metrics = {}
    for row, (label, group) in enumerate(zip((args.before_label, 'Candidate'), shapes)):
        counts = []
        for col, angle in enumerate(range(0, 360, 45)):
            rendered, coverage = render(group, angle, bounds, elevation=args.elevation)
            counts.append(coverage)
            sheet.paste(rendered, (col*280, row*380+30))
            draw.text((col*280+12, row*380+8), f'{label} | {angle} degrees', fill='#dce6e8')
            draw.text((col*280+12, row*380+363), f'Foliage pixels: {coverage:,}', fill='#adc4bd')
        metrics[label] = dict(angles=list(range(0, 360, 45)), foliage_pixels=counts,
                              min_to_max_coverage=min(counts)/max(1, max(counts)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)
    args.output.with_suffix('.json').write_text(json.dumps(metrics, indent=2)+'\n')
    print(json.dumps(metrics))


if __name__ == '__main__':
    main()
