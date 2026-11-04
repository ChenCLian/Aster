from pathlib import Path
import importlib.util, tempfile, unittest, json, urllib.request, io
from unittest.mock import patch
p=Path(__file__).with_name('manage_data.py').resolve()
spec=importlib.util.spec_from_file_location('orbital_manager',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class ToolTests(unittest.TestCase):
 def test_verify_and_unpack(self):
  with tempfile.TemporaryDirectory() as d, patch('sys.stdout',io.StringIO()):
   self.assertEqual(m.main(['verify']),0)
   self.assertEqual(m.main(['unpack',d]),0)
   for e in m.catalogue().values():self.assertEqual((Path(d)/e['filename']).read_bytes(),m.embedded(e))
   with self.assertRaises(FileExistsError):m.main(['unpack',d])
 def test_pt_grid(self):
  self.assertEqual(m.pt_filename('1','0.4')[0],'PT.De1.0000Td0.4000.dat')
  for ed,td in [('NaN','1'),('1.0001','1'),('1','-1'),('.99','1'),('1','1.21')]:
   with self.assertRaises(ValueError):m.pt_filename(ed,td)
 def test_pt_schema(self):
  raw=m.embedded(m.catalogue()['pt_zb18a'])
  self.assertEqual(m.validate_pt(raw),249480)
  for bad in [b'',b'0 .4 0 .01\n-1 .4 0 .02\n',b'0 nan 0 .1\n',b'0 .4 7 .1\n',raw+b'0 .4 0 .01\n']:
   with self.assertRaises(ValueError):m.validate_pt(bad)
 def test_download_is_bounded_validated_and_non_overwriting(self):
  raw=m.embedded(m.catalogue()['pt_zb18a'])
  with tempfile.TemporaryDirectory() as d, patch('urllib.request.urlopen',return_value=io.BytesIO(raw)) as request, patch('sys.stdout',io.StringIO()):
   self.assertEqual(m.main(['fetch-pt',d]),0);request.assert_called_once()
   path=Path(d)/'PT.De1.0000Td1.0000.dat';self.assertEqual(path.read_bytes(),raw)
   self.assertEqual(json.loads(path.with_suffix('.provenance.json').read_text())['rows'],249480)
   with self.assertRaises(FileExistsError):m.main(['fetch-pt',d])
  with tempfile.TemporaryDirectory() as d, patch('urllib.request.urlopen',return_value=io.BytesIO(b'html error')):
   with self.assertRaises(ValueError):m.main(['fetch-pt',d])
   self.assertEqual(list(Path(d).iterdir()),[])
if __name__ == '__main__':
 unittest.main()
