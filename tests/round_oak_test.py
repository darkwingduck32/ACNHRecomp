import pathlib
import sys
import unittest

import numpy as np
import zstandard

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from freecamera_trees import Tree, sarc_members, u64
from generate_round_oak import build_buffers, generate, make_plan, model_shapes, rotation_between
from assemble_freecamera_oak import world_positions


class RotationTests(unittest.TestCase):
    def test_rotation_preserves_card_shape_and_faces_target(self):
        for source, target in (((0, 0, 1), (0, 0, -1)), ((0, 0, 1), (1, 0, 0)), ((1, 2, 3), (-4, 2, 1))):
            matrix = rotation_between(source, target)
            np.testing.assert_allclose(matrix.T@matrix, np.eye(3), atol=1e-10)
            np.testing.assert_allclose(matrix@(np.array(source)/np.linalg.norm(source)),
                                       np.array(target)/np.linalg.norm(target), atol=1e-10)
            self.assertAlmostEqual(np.linalg.det(matrix), 1)


@unittest.skipUnless((ROOT/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs').exists(), 'Requires local oak asset')
class LocalAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT/'private/game/romfs/Model/PltTreeOak.Nin_NX_NVN.zs'
        archive = zstandard.ZstdDecompressor().decompress(path.read_bytes())
        _, start, end = next(sarc_members(archive))
        cls.tree = Tree(archive[start:end])

    def test_generated_clusters_preserve_authored_edges(self):
        model, _, shapes = next(m for m in model_shapes(self.tree) if m[1] == 'PltTreeOak4')
        shape = shapes[0]
        plan = make_plan(self.tree, model, shape, 2, .95, 17)
        _, _, expected = build_buffers(self.tree, model, shape, plan)
        points = np.array(world_positions(self.tree, dict(model_offset=model, shape=shape, vertex=u64(self.tree.data, shape+16))))
        offset = 0
        for item in plan:
            used = item['source']['vertices']
            old = points[used]
            new = expected[offset:offset+len(used)]
            np.testing.assert_allclose(np.linalg.norm(old[:, None]-old[None, :], axis=-1),
                                       np.linalg.norm(new[:, None]-new[None, :], axis=-1), atol=1e-6)
            offset += len(used)

    def test_static_and_reaction_models_have_matching_bind_pose(self):
        data, report = generate(self.tree, density=2)
        result = Tree(data)
        models = {name: (model, shapes[0]) for model, name, shapes in model_shapes(result)}
        for family in ('PltTreeOak3', 'PltTreeOak4'):
            poses = []
            for name in (family, family+'Node'):
                model, shape = models[name]
                poses.append(world_positions(result, dict(model_offset=model, shape=shape, vertex=u64(data, shape+16))))
            np.testing.assert_allclose(poses[0], poses[1], atol=.001)
        self.assertEqual(len(report), 4)


if __name__ == '__main__':
    unittest.main()
