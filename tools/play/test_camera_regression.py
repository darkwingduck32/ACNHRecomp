from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import camera_regression as regression


class CameraRegressionTests(unittest.TestCase):
    def title(self, terrain=True, fov='.277'):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = ('camera303: terrain tile range 2x enabled\n' if terrain else '')
            log += f'freecamera: active terrain 38x12\nfreecamera: perspective tan/aspect {fov} 1.778\n'
            log += ''.join(f'freecamera: visibility family {n} registered\n' for n in range(5))
            log += 'freecamera: input frame 900 enabled 1 active 1 yaw 0 pitch 0 zoom 1\n'
            (root/'nx.log').write_text(log)
            with patch.object(regression, 'usable_frame', return_value=True), \
                 patch.object(regression.live, 'need_alive'), patch.object(regression.live, 'frame', return_value=900), \
                 patch.object(regression.live, 'wait_frames'), patch.object(regression.live, 'shot', return_value='capture.png'):
                return regression.run(root, 'title', samples=1)

    def test_missing_legacy_terrain_adapter_fails(self):
        with self.assertRaisesRegex(AssertionError, 'Terrain profile installed'):
            self.title(terrain=False)

    def test_visibility_using_wrong_projection_fails(self):
        with self.assertRaisesRegex(AssertionError, 'Configured FOV'):
            self.title(fov='.176')

    def test_passing_assertions_still_require_visual_review(self):
        result = self.title()
        self.assertTrue(result['passed'])
        self.assertEqual(result['visual_review'], 'required')
        self.assertEqual(len(result['captures']), 1)

    def test_pose_gate_detects_zoom_as_well_as_orbit(self):
        before = dict(yaw=0, pitch=0, zoom=1)
        with self.assertRaisesRegex(AssertionError, 'zoom changed'):
            regression.assert_pose(before, dict(before, zoom=.75))

    def test_loading_screen_is_not_visual_evidence(self):
        from PIL import Image, ImageDraw
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'frame.png'
            frame = Image.new('RGB', (1280, 720))
            ImageDraw.Draw(frame).rectangle((1160, 620, 1240, 700), fill=(50, 180, 90))
            frame.save(path)
            self.assertFalse(regression.usable_frame(path))
            Image.new('RGB', (1280, 720), (4, 5, 7)).save(path)
            self.assertTrue(regression.usable_frame(path))


if __name__ == '__main__': unittest.main()
