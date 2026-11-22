"""Crown closure and continuous snow surfaces for the local tree generator."""
import math
import struct

import numpy as np

from assemble_freecamera_oak import components, world_positions
from freecamera_trees import u32, u64
from generate_round_oak import rotation_between


def crown_closure(tree, model, shape, seed=53):
    v=u64(tree.data,shape+16)
    points=np.array(world_positions(tree,dict(vertex=v,shape=shape,model_offset=model)))
    cards=[]
    for triangles in components(tree.triangles(shape)):
        used=sorted({i for tri in triangles for i in tri})
        ps=points[used]
        normal=sum((np.cross(points[b]-points[a],points[c]-points[a]) for a,b,c in triangles),np.zeros(3))
        if np.linalg.norm(normal)<1e-6:
            continue
        cards.append(dict(vertices=used,triangles=triangles,center=ps.mean(0),normal=normal))
    centers=np.array([c['center'] for c in cards])
    low,high=points.min(0),points.max(0)
    height=high[1]-low[1]
    center_x=float((low[0]+high[0])/2)
    upper=points[points[:,1]>high[1]-height*.24]
    radius=float(np.quantile(np.abs(upper[:,0]-center_x),.95))*.72
    pool=[c for c in cards if c['center'][1]>=centers[:,1].max()-height*.35 and
          np.max(np.ptp(points[c['vertices']],axis=0))<radius*1.4]
    if not pool:
        return []  # Never turn long hanging vines into a horizontal crown lid.
    areas=[]
    for card in pool:
        areas.append(sum(np.linalg.norm(np.cross(points[b]-points[a],points[c]-points[a]))/2 for a,b,c in card['triangles']))
    count=max(10,min(70,math.ceil(math.pi*radius*radius*2/max(.1,float(np.mean(areas))))))
    rng=np.random.default_rng(seed)
    plan=[]
    for i in range(count):
        angle=i*math.pi*(3-math.sqrt(5))
        radial=radius*math.sqrt(i/max(1,count-1))
        card=pool[int(rng.integers(len(pool)))]
        # A shallow dome of intact alpha-tested clusters closes the center.
        outward=np.array((.45*radial/max(radius,.01)*math.sin(angle),1,
                          .45*radial/max(radius,.01)*math.cos(angle)))
        rotation=rotation_between(card['normal'],outward)
        relative=(points[card['vertices']]-card['center'])@rotation.T
        footprint=float(np.linalg.norm(relative[:,[0,2]],axis=1).max())
        radial=min(radial,max(0,radius-footprint-.15))
        target=np.array((center_x+radial*math.sin(angle),
                         high[1]-.35-float(relative[:,1].max())-.15*(radial/max(radius,.01))**2,
                         radial*math.cos(angle)*.95))
        plan.append(dict(source=card,center=target,rotation=rotation))
    return plan


def snow_surface(tree,model,shape,asset_name,support=None):
    """Drape small closed snow deposits onto actual opaque leaf clusters."""
    from tree_snow_deposits import build_deposits
    d,v=tree.data,u64(tree.data,shape+16)
    source=np.array(world_positions(tree,dict(vertex=v,shape=shape,model_offset=model)))
    positions,normals,uvs,triangles=build_deposits(support,asset_name)
    transforms,indices=tree.rig(model,shape,v)
    if indices is None:
        transforms,indices=[(np.eye(3),np.zeros(3))],[0]*len(source)
    buffers=list(tree.buffers(v));output=[bytearray() for _ in buffers]
    attributes=list(tree.attributes(v))
    for point,normal,uv in zip(positions,normals,uvs):
        point,normal=np.array(point,dtype=float),np.array(normal,dtype=float)
        nearest=int(np.argmin(np.sum((source-point)**2,axis=1)))
        r,t=transforms[indices[nearest]];r,t=np.array(r),np.array(t)
        records=[bytearray(d[p+nearest*stride:p+(nearest+1)*stride]) for p,_,stride in buffers]
        for name,fmt,offset,buffer in attributes:
            raw=records[buffer]
            if name=='_p0':
                struct.pack_into('<3f',raw,offset,*(r.T@(point-t)))
            elif name=='_n0':
                packed=u32(raw,offset)&0xc0000000
                for shift,n in zip((0,10,20),r.T@normal):
                    packed|=(max(-511,min(511,round(n*511)))&1023)<<shift
                struct.pack_into('<I',raw,offset,packed)
            elif name=='_t0' and fmt==0x20b:
                tangent=np.cross(normal,(0,0,1) if abs(normal[2])<.9 else (1,0,0));tangent/=np.linalg.norm(tangent)
                struct.pack_into('<3b',raw,offset,*(max(-127,min(127,round(x*127))) for x in r.T@tangent))
            elif name=='_u0' and fmt==0x112:
                struct.pack_into('<2H',raw,offset,*(round(x*65535) for x in uv))
        for record,out in zip(records,output):out.extend(record)
    return [(bytes(out),stride) for out,(_,_,stride) in zip(output,buffers)],triangles,np.array(positions)
