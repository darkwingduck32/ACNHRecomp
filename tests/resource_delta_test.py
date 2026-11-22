from pathlib import Path
import random
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from resource_delta import create,apply

class ResourceDeltaTest(unittest.TestCase):
    def test_checked_roundtrips(self):
        rng=random.Random(173)
        base=rng.randbytes(8192)
        for original,modified in ((b'',b''),(b'',b'new'),(b'old',b''),(base,base),
                                  (base,base[512:2048]+b'new geometry'+base[:1024]),
                                  (base,bytes((x+1)%256 for x in base))):
            patch=create(original,modified)
            self.assertEqual(apply(original,patch),modified)
            self.assertEqual(create(original,modified),patch)
        with self.assertRaises(ValueError):apply(base,b'unknown format')

if __name__=='__main__':unittest.main()
