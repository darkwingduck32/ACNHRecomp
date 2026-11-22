import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from preview_trees import resources
from freecamera_trees import Tree, u32, u64, string
from generate_round_oak import finish, model_shapes
from generate_forest import generate, two_sided
from tree_crown_surfaces import crown_closure, snow_surface
from tree_snow_deposits import support_mesh, ray
from assemble_freecamera_oak import world_positions, components
from collections import Counter
import numpy as np


@unittest.skipUnless((ROOT/'private/game/romfs/Model/PltTreePalm.Nin_NX_NVN.zs').exists(), 'Requires local game assets')
class ForestTests(unittest.TestCase):
    def test_top_closure_has_upward_facing_clusters(self):
        _,data=resources(ROOT/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs')[0]
        tree=Tree(data)
        model,_,shapes=next(m for m in model_shapes(tree) if m[1]=='PltTreeOak4')
        plan=crown_closure(tree,model,shapes[0])
        self.assertGreaterEqual(len(plan),10)
        for item in plan:
            normal=item['rotation']@item['source']['normal']
            self.assertGreater(normal[1]/np.linalg.norm(normal),.65)

    def test_snow_surface_is_closed_without_duplicate_overlapping_caps(self):
        _,data=resources(ROOT/'private/game/romfs/Model/PltTreeOakSnow.Nin_NX_NVN.zs')[0]
        tree=Tree(data)
        model,_,shapes=next(m for m in model_shapes(tree) if m[1]=='PltTreeOak4Snow')
        shape=next(s for s in shapes if 'mWinterSnow' in string(data,u64(data,s+8)))
        leaf=shapes[0];v=u64(data,leaf+16)
        buffers=[(data[p:p+size],stride) for p,size,stride in tree.buffers(v)]
        support=support_mesh(tree,v,buffers,world_positions(tree,dict(vertex=v,shape=leaf,model_offset=model)),tree.triangles(leaf),'mTreeOakLeafSnow')
        _,triangles,points=snow_surface(tree,model,shape,'PltTreeOakSnow.Nin_NX_NVN.zs',support)
        edges=Counter(tuple(sorted(edge)) for a,b,c in triangles for edge in ((a,b),(b,c),(c,a)))
        self.assertTrue(all(count==2 for count in edges.values()),'Snow must have no boundary or nonmanifold seams')
        self.assertTrue(np.isfinite(points).all())
        for a,b,c in triangles:
            self.assertGreater(np.linalg.norm(np.cross(points[b]-points[a],points[c]-points[a])),1e-6)
        for group in components(triangles):
            ids=sorted({i for tri in group for i in tri})
            self.assertLess(np.ptp(points[ids,0]),2)
            self.assertLess(np.ptp(points[ids,2]),2)
        for x,y,z in points:
            surface=ray(support,x,z)
            self.assertIsNotNone(surface)
            self.assertLessEqual(y-surface,.38)

    def test_rear_faces_preserve_weighted_vertex_records(self):
        _, data = resources(ROOT/'private/game/romfs/Model/PltTreePalm.Nin_NX_NVN.zs')[0]
        tree = Tree(data)
        model, _, shapes = next(m for m in model_shapes(tree) if m[1] == 'PltTreePalm4Node')
        shape = shapes[0]
        v = u64(data, shape+16)
        count = u32(data, v+80)
        buffers, triangles, positions = two_sided(tree, model, shape)
        self.assertEqual(triangles[:len(triangles)//2], tree.triangles(shape))
        self.assertEqual(triangles[len(triangles)//2:], [(c+count,b+count,a+count) for a,b,c in tree.triangles(shape)])
        for name, fmt, offset, index in tree.attributes(v):
            if name not in ('_i0','_w0','_c0','_u0'):
                continue
            raw, stride = buffers[index]
            for i in range(count):
                self.assertEqual(raw[i*stride:(i+1)*stride],raw[(i+count)*stride:(i+count+1)*stride],name)
        self.assertEqual(len(positions),2*count)

    def test_forest_preserves_all_skeleton_bytes(self):
        _, data = resources(ROOT/'private/game/romfs/Model/PltTreeBamboo.Nin_NX_NVN.zs')[0]
        final, reports = generate(Tree(data),'PltTreeBamboo.Nin_NX_NVN.zs')
        self.assertTrue(any(r['action']=='radial_branches' for r in reports))
        for model, _, _ in model_shapes(Tree(data)):
            skeleton = u64(data,model+24)
            bones = u64(data,skeleton+16)
            # The native skeleton remains at the same offset, before GPU buffers.
            self.assertEqual(data[skeleton:skeleton+64],final[skeleton:skeleton+64])
            self.assertEqual(data[bones:bones+96],final[bones:bones+96])

    def test_decorative_wreath_is_not_treated_as_a_tree(self):
        path=ROOT/'private/game/romfs/Model/FtrDoorOrnamentWreathFruit.Nin_NX_NVN.zs'
        _, data=resources(path)[0]
        final,reports=generate(Tree(data),path.name)
        self.assertEqual(final,data)
        self.assertEqual(reports,[])


if __name__=='__main__':
    unittest.main()
