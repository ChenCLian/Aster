import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import check_packaged as c

class PackagedChecksTests(unittest.TestCase):
    def test_failed_rerun_clears_stale_success_and_screenshots(self):
        with tempfile.TemporaryDirectory() as td:
            out=Path(td);(out/'result.json').write_text('{"status":"passed"}')
            for name in ('packaged-zh.png','packaged-en.png','version.log','notes.txt'):(out/name).write_text('old')
            with patch.object(c,'_run_checks',side_effect=ValueError('bad package')):
                with self.assertRaisesRegex(ValueError,'bad package'):c.run(Path('package.zip'),'1.3.0','windows-x64',out)
            result=json.loads((out/'result.json').read_text());self.assertEqual(result['status'],'failed')
            self.assertEqual(result['version'],'1.3.0');self.assertFalse((out/'packaged-zh.png').exists());self.assertFalse((out/'version.log').exists());self.assertTrue((out/'notes.txt').exists())
    def test_timeout_keeps_partial_diagnostic_output(self):
        with tempfile.TemporaryDirectory() as td:
            log=Path(td)/'log'
            with patch.object(c.subprocess,'run',side_effect=subprocess.TimeoutExpired(['app'],30,output=b'loading plugin\n')):
                with self.assertRaises(subprocess.TimeoutExpired):c.probe(['app'],Path(td),{},log)
            self.assertIn('loading plugin',log.read_text());self.assertIn('timed out',log.read_text())
    def test_new_result_does_not_inherit_previous_failure(self):
        with tempfile.TemporaryDirectory() as td:
            out=Path(td);(out/'result.json').write_text('{"status":"failed","error":"old failure"}')
            with patch.object(c,'_run_checks'):c.run(Path('package.zip'),'1.3.0','windows-x64',out)
            result=json.loads((out/'result.json').read_text());self.assertEqual(result['status'],'passed');self.assertNotIn('error',result)
