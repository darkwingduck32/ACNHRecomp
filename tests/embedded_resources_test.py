import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from embed_resources import generate

class EmbeddedResourcesTest(unittest.TestCase):
    def test_checked_repeatable_binary_generation(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);stock=root/'stock';replacement=root/'replacement';out=root/'generated'
            stock.write_bytes(b'stock');replacement.write_bytes(b'\x00\xffpayload\x00')
            item=dict(feature='example.first',path='Test/data',input=str(stock),replacement=str(replacement),
                      input_sha256=hashlib.sha256(stock.read_bytes()).hexdigest(),output_sha256=hashlib.sha256(replacement.read_bytes()).hexdigest())
            self.assertEqual(generate([item],out),(1,10))
            once={p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in out.iterdir()}
            generate([item],out)
            self.assertEqual(once,{p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in out.iterdir()})
            for data,_ in once.values():self.assertNotIn(str(root).encode(),data)
            self.assertIn(b'\\x00\\xff',next(out.glob('asset_*.cpp')).read_bytes())
            with self.assertRaises(ValueError):generate([item,item],out)
            with self.assertRaisesRegex(ValueError,'must be distributed'):
                generate([dict(item,path='Model/test')],out,require_delta_models=True)
            for path in ('../private','/absolute','a\\b','a/./b','C:/data','','a\0b','a\nb'):
                with self.assertRaises(ValueError):generate([dict(item,path=path)],out)
            replacement.write_bytes(b'changed')
            with self.assertRaises(ValueError):generate([item],out)

    def test_empty_catalog_cleans_only_generated_payloads(self):
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory);(out/'asset_old.cpp').write_text('stale');(out/'keep.txt').write_text('keep')
            generate([],out)
            self.assertFalse((out/'asset_old.cpp').exists())
            self.assertEqual((out/'keep.txt').read_text(),'keep')
            self.assertIn('return {};',(out/'index.cpp').read_text())

    def test_prebuilt_delta_needs_no_complete_model_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);payload=root/'payload';payload.write_bytes(b'NXDP0001fixture')
            checksum=hashlib.sha256(payload.read_bytes()).hexdigest()
            item=dict(feature='example.model',path='Model/test',encoding='zstd-delta',prebuilt=True,
                      input_sha256='1'*64,output_sha256='2'*64,content_sha256='3'*64,content_size=100,
                      payload=str(payload),payload_sha256=checksum)
            self.assertEqual(generate([item],root/'out'),(1,100))
            index=(root/'out/index.cpp').read_text()
            self.assertIn('ResourceEncoding::ZstdDelta',index)
            self.assertNotIn(str(root),index)
            payload.write_bytes(b'NXDP0001tampered')
            with self.assertRaisesRegex(ValueError,'Invalid native delta'):
                generate([item],root/'out')

if __name__=='__main__':unittest.main()
