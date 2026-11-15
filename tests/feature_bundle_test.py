import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from bundle_camera_package import bundle
from build_camera_package import archive

class FeatureBundleTest(unittest.TestCase):
    def test_native_content_preserved_and_defaults_repeatable(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);dest=root/'release';mods=dest/'mods';packages=root/'packages'
            native={'speedfix60/romfs/Pack/StaticParam.pack':b'native SpeedFix data',
                    'ptbr/romfs/Message/text':b'native translation'}
            for name,data in native.items():
                f=mods/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(data)
            (mods/'bundle.txt').write_text('native-version\n'+'\n'.join(native)+'\n')
            archive(packages,'freecamera',{'romfs/Model/test':b'model','profiles/test.txt':b'profile'},'acnh.camera.v1',['camera303'])
            archive(packages,'tool_durability',{'romfs/Bcsv/test':b'tools'},'romfs.v1',[])
            bundle(dest,packages)
            once=(mods/'bundle.txt').read_bytes();bundle(dest,packages)
            self.assertEqual(once,(mods/'bundle.txt').read_bytes())
            self.assertEqual((mods/'defaults/enabled.txt').read_text(),'freecamera\ntool_durability\n')
            for name,data in native.items():self.assertEqual((mods/name).read_bytes(),data)
            self.assertFalse((mods/'freecamera/romfs/Pack/StaticParam.pack').exists())
            self.assertIn('freecamera/profiles/test.txt',(mods/'bundle.txt').read_text())

if __name__=='__main__':unittest.main()
