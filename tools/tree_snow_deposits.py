"""Small snow deposits supported by opaque leaf surfaces, not canopy-wide hats."""
import functools
import math
import struct

import numpy as np

from assemble_freecamera_oak import components
from preview_trees import ROOT, find_textures, texture_image


def support_mesh(tree, vertex, buffers, points, triangles, material):
    name,fmt,offset,index=next(a for a in tree.attributes(vertex) if a[0]=='_u0')
    raw,stride=buffers[index]
    code,scale={0x112:('H',65535),0x212:('h',32767),0x512:('e',1)}[fmt]
    uv=np.array([struct.unpack_from('<2'+code,raw,i*stride+offset) for i in range(len(points))])/scale
    return dict(points=np.asarray(points),triangles=np.asarray(triangles),uv=uv,material=material)


@functools.lru_cache(maxsize=8)
def opacity(asset,material):
    path=ROOT/'private/game/romfs/Model'/asset
    texture=next((t for t in find_textures(str(path)) if t.name==material+'_OP'),None)
    return np.array(texture_image(texture))[:,:,0]/255 if texture else None


def ray(mesh,x,z,triangles=None,alpha=None):
    ids=mesh['triangles'] if triangles is None else np.asarray(triangles)
    points=mesh['points'][ids]
    a,b,c=points[:,0],points[:,1],points[:,2]
    den=(b[:,2]-c[:,2])*(a[:,0]-c[:,0])+(c[:,0]-b[:,0])*(a[:,2]-c[:,2])
    valid=np.abs(den)>1e-8
    den=np.where(valid,den,1)
    wa=((b[:,2]-c[:,2])*(x-c[:,0])+(c[:,0]-b[:,0])*(z-c[:,2]))/den
    wb=((c[:,2]-a[:,2])*(x-c[:,0])+(a[:,0]-c[:,0])*(z-c[:,2]))/den
    wc=1-wa-wb
    valid&=(wa>=-1e-7)&(wb>=-1e-7)&(wc>=-1e-7)
    if alpha is not None:
        uv=mesh['uv'][ids]
        coord=uv[:,0]*wa[:,None]+uv[:,1]*wb[:,None]+uv[:,2]*wc[:,None]
        h,w=alpha.shape
        valid&=alpha[(coord[:,1]%1*h).astype(int),(coord[:,0]%1*w).astype(int)]>=.65
    y=a[:,1]*wa+b[:,1]*wb+c[:,1]*wc
    if not valid.any():return None
    return float(y[valid].max())


def build_deposits(mesh,asset):
    if mesh is None:
        raise ValueError('Snow deposits require the generated foliage support mesh')
    alpha=opacity(asset,mesh['material'])
    points=mesh['points'];candidates=[]
    for group in components(mesh['triangles']):
        ids=sorted({i for tri in group for i in tri})
        ps=points[ids]
        for tri in group:
            a,b,c=points[list(tri)]
            normal=np.cross(b-a,c-a)
            if normal[1]<=np.linalg.norm(normal)*.18:continue
            center=(a+b+c)/3
            y=ray(mesh,center[0],center[2],group,alpha)
            top=ray(mesh,center[0],center[2],alpha=alpha)
            if y is None or top is None or top-y>.12:continue
            extent=np.ptp(ps[:,[0,2]],axis=0)
            radius=min(.95,float(extent.min())*.3)
            if radius<.12:continue
            candidates.append((y,center[0],center[2],radius,group))
            break
    candidates.sort(reverse=True,key=lambda p:p[0])
    positions,triangles=[],[];placed=[]
    segments,rings=12,3
    for y,x,z,initial,group in candidates:
        if len(placed)>=56:break
        if any(math.hypot(x-px,z-pz)<max(initial,pr)*1.6 for px,pz,pr in placed):continue
        radius=initial;rim=None
        # Keep the footprint on one leaf cluster, with an opaque center and
        # at least half its rim supported by opaque texels. Small accumulations
        # may bridge adjacent leaflets, but never span the entire canopy.
        for attempt in range(8):
            rim=[];opaque=0
            for s in range(segments):
                angle=s*math.tau/segments
                px,pz=x+radius*math.sin(angle),z+radius*math.cos(angle)
                py=ray(mesh,px,pz,group)
                if py is None:break
                rim.append((px,py,pz))
                opaque+=ray(mesh,px,pz,group,alpha) is not None
            if len(rim)==segments and opaque>=segments//2:break
            radius*=.76
        if len(rim)!=segments or opaque<segments//2 or radius<.08:continue
        thickness=min(.35,max(.06,radius*.3))
        start=len(positions)
        positions.append((x,y+thickness+.018,z))
        valid=True
        temporary=[]
        for ring in range(1,rings+1):
            t=ring/rings
            for s in range(segments):
                angle=s*math.tau/segments
                px,pz=x+radius*t*math.sin(angle),z+radius*t*math.cos(angle)
                py=ray(mesh,px,pz,group)
                if py is None:valid=False;break
                temporary.append((px,py+.018+thickness*math.sqrt(max(0,1-t*t)),pz))
            if not valid:break
        if not valid:
            positions.pop();continue
        positions.extend(temporary)
        for s in range(segments):triangles.append((start,start+1+s,start+1+(s+1)%segments))
        for ring in range(rings-1):
            a=start+1+ring*segments;b=a+segments
            for s in range(segments):
                n=(s+1)%segments
                triangles.extend(((a+s,b+s,a+n),(a+n,b+s,b+n)))
        bottom=len(positions)
        positions.append((x,y+.006,z))
        edge=start+1+(rings-1)*segments
        for s in range(segments):triangles.append((bottom,edge+(s+1)%segments,edge+s))
        placed.append((x,z,radius))
    if not positions:raise ValueError('No supported opaque foliage for snow deposits')
    positions=np.asarray(positions)
    normals=np.zeros_like(positions)
    for a,b,c in triangles:
        n=np.cross(positions[b]-positions[a],positions[c]-positions[a])
        for i in (a,b,c):normals[i]+=n
    normals/=np.maximum(1e-9,np.linalg.norm(normals,axis=1,keepdims=True))
    uv=[(.5,.5)]*len(positions)
    return positions,normals,uv,triangles
