import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from freecamera_trees import whole_leaf_groups


class WholeLeafTests(unittest.TestCase):
    def test_crossing_leaf_keeps_both_triangles(self):
        source = [(-1, 0, -1), (1, 0, 2), (-1, 1, 2), (1, 1, -1)]
        points, triangles, used, _, groups = whole_leaf_groups(source, [(0, 1, 2), (1, 3, 2)])
        self.assertEqual(points, source)
        self.assertEqual(triangles, [(0, 1, 2), (1, 3, 2)])
        self.assertEqual(used, [0, 1, 2, 3])
        self.assertEqual(groups, 1)

    def test_unused_back_vertices_are_removed(self):
        source = [(0, 0, 2), (1, 0, 2), (0, 1, 2), (0, 0, -2), (1, 0, -2), (0, 1, -2)]
        points, triangles, used, _, groups = whole_leaf_groups(source, [(0, 1, 2), (3, 4, 5)])
        self.assertEqual(len(points), 3)
        self.assertEqual(triangles, [(0, 1, 2)])
        self.assertEqual(used, [0, 1, 2])
        self.assertEqual(groups, 2)

    def test_negative_side_is_supported(self):
        _, triangles, _, side, _ = whole_leaf_groups([(0, 0, -2), (1, 0, -1), (0, 1, -1)], [(0, 1, 2)])
        self.assertEqual(side, -1)
        self.assertEqual(triangles, [(0, 1, 2)])

    def test_compaction_remaps_sparse_indices(self):
        source = [(0, 0, -2), (1, 0, -2), (0, 1, -2), (0, 0, 2), (1, 0, 2), (0, 1, 2)]
        points, triangles, used, _, _ = whole_leaf_groups(source, [(0, 1, 2), (3, 4, 5)])
        self.assertEqual(points, source[3:])
        self.assertEqual(used, [3, 4, 5])
        self.assertEqual(triangles, [(0, 1, 2)])


if __name__ == '__main__':
    unittest.main()
