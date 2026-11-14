"""Regressions for art-directed assemblies; requires private game assets."""
import pathlib
import sys
import unittest

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from assemble_freecamera_oak import world_positions
from freecamera_trees import Tree, string, u64
from generate_round_oak import build_buffers, model_shapes
from preview_trees import resources, material_faces
from tree_art_direction import snow_branch_plans


@unittest.skipUnless((ROOT/'private/game/romfs/Model/PltTreeOakSnow.Nin_NX_NVN.zs').exists(), 'Requires local assets')
class ArtDirectionTests(unittest.TestCase):
    def load(self, family):
        _, data = resources(ROOT/f'private/game/romfs/Model/PltTree{family}Snow.Nin_NX_NVN.zs')[0]
        return Tree(data)

    def test_native_snow_primitive_geometry_is_preserved(self):
        # Snow is copied as intact authored parts, never replaced with a dome.
        for family in ('Oak', 'Bamboo'):
            tree = self.load(family)
            for model, name, shapes in model_shapes(tree):
                plans = snow_branch_plans(tree, model, name, shapes)
                for shape, plan in plans.items():
                    if 'mWinterSnow' not in string(tree.data, u64(tree.data, shape+8)):
                        continue
                    original = np.asarray(world_positions(tree, dict(model_offset=model, shape=shape, vertex=u64(tree.data, shape+16))))
                    _, triangles, result = build_buffers(tree, model, shape, plan)
                    offset = 0
                    for item in plan:
                        ids = item['source']['vertices']
                        old = original[ids]
                        new = result[offset:offset+len(ids)]
                        np.testing.assert_allclose(np.linalg.norm(old-old[0], axis=1), np.linalg.norm(new-new[0], axis=1), atol=1e-5)
                        offset += len(ids)
                    self.assertEqual(len(triangles), sum(len(i['source']['triangles']) for i in plan))

    def test_snow_oak_has_mixed_material_culling(self):
        tree = self.load('Oak')
        model, _, shapes = next(m for m in model_shapes(tree) if m[1]=='PltTreeOak4Snow')
        faces = dict(material_faces(tree.data, model, shape) for shape in shapes)
        self.assertEqual(faces['mTreeOakLeafSnow'], 'both')
        self.assertEqual(faces['mTreeOakLeafSphSnow'], 'both')
        self.assertEqual(faces['mWinterSnow'], 'front')
        self.assertEqual(faces['mPltTreeOakWinterTrunk'], 'front')

    def test_bamboo_lower_sprays_stay_on_their_stalks(self):
        tree = self.load('Bamboo')
        for model, name, shapes in model_shapes(tree):
            for shape, plan in snow_branch_plans(tree, model, name, shapes).items():
                if not string(tree.data, u64(tree.data, shape+8)).endswith('LeafSnow'):
                    continue
                cutoff = 20.5 if 'Bamboo4' in name else 12.
                seen = set()
                for item in plan:
                    if item['source']['center'][1] >= cutoff:
                        continue
                    ids = (tuple(item['source']['vertices']), item.get('reverse', False))
                    self.assertNotIn(ids, seen)
                    seen.add(ids)
                    np.testing.assert_allclose(item['center'], item['source']['center'], atol=1e-8)
                    np.testing.assert_allclose(item['rotation'], np.eye(3), atol=1e-8)


if __name__ == '__main__':
    unittest.main()
