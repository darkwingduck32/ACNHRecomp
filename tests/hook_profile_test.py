import tempfile
import unittest
from pathlib import Path
from recomp.hooks import read_hooks, hook_sources, hooks_fingerprint
class HookProfileTest(unittest.TestCase):
    def test_nested_profiles_and_cycles(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/'base').write_text('10 # shared\ninclude feature\n')
            (root/'feature').write_text('20\n10\n')
            self.assertEqual(read_hooks(root/'base'),{0x10,0x20})
            self.assertEqual(len(hook_sources(root/'base')),2)
            before=hooks_fingerprint(root/'base')
            (root/'feature').write_text('30\n')
            self.assertNotEqual(before,hooks_fingerprint(root/'base'))
            (root/'feature').write_text('include base\n')
            with self.assertRaises(ValueError):read_hooks(root/'base')
    def test_camera_hooks_present(self):
        root=Path(__file__).resolve().parents[1]
        hooks=read_hooks(root/'games/acnh/hooks-3.0.3.txt')
        self.assertTrue({0x37e4724,0x1462bb8,0x11fe020,0x14b76e8,0x10d660c,0x146ac2c,0x1571a70,0x146b1d4,0x146b1dc}<=hooks)
if __name__=='__main__':unittest.main()
