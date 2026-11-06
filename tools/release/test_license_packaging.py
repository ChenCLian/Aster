"""Check distributable license files and the notice collection paths."""
# Copyright (C) 2026 ZiJie Chen
# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('stage_notices', ROOT / 'tools/release/stage_notices.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)

class LicensePackagingTests(unittest.TestCase):
    def test_notice_collection_preserves_bytes_and_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            qt = base / 'Qt'
            (qt / 'LICENSES').mkdir(parents=True)
            (qt / 'LICENSES/fixture.txt').write_text('Qt notice fixture')
            dependency = base / 'dependency'
            dependency.mkdir()
            (dependency / 'COPYING').write_text('Dependency notice fixture')
            dest = base / 'package'
            with patch.object(sys, 'argv', ['stage_notices.py', str(dest), '--qt-prefix', str(qt),
                                          '--dependency-prefix', str(dependency)]):
                stage.main()
            for name in ['LICENSE', 'COPYRIGHT.md', 'SOURCE_CODE.md', 'AUTHORS.md']:
                self.assertEqual((ROOT / name).read_bytes(), (dest / name).read_bytes())
            for name in ['GPL-3.0.txt', 'AstroGeoFit_LICENSE.txt']:
                self.assertEqual((ROOT / 'licenses' / name).read_bytes(), (dest / 'licenses' / name).read_bytes())
            self.assertFalse((dest / 'licenses/licenses').exists())
            for name in ['lowess/LICENSE.txt', 'pocketfft/LICENSE.md', 'wavelib/COPYRIGHT', 'qcustomplot/COPYING']:
                self.assertEqual((ROOT / 'src/third_party' / name).read_bytes(),
                                 (dest / 'licenses/src/third_party' / name).read_bytes())
            self.assertEqual((dest / 'licenses/dependencies/Qt/LICENSES/fixture.txt').read_text(), 'Qt notice fixture')
            self.assertEqual((dest / 'licenses/dependencies/dependency/COPYING').read_text(), 'Dependency notice fixture')

    def test_embedded_license_resources(self):
        files = {entry.attrib['alias']: (ROOT / 'resources' / entry.text).resolve()
                 for entry in ET.parse(ROOT / 'resources/help.qrc').iter('file')}
        for name in ['LICENSE', 'COPYRIGHT.md', 'SOURCE_CODE.md', 'docs/LICENSE_GUIDE_ZH.md',
                     'licenses/AstroGeoFit_LICENSE.txt']:
            self.assertTrue(files[name].is_file(), name)
        self.assertEqual(files['LICENSE'].read_bytes(), (ROOT / 'licenses/GPL-3.0.txt').read_bytes())
        self.assertIn('END OF TERMS AND CONDITIONS', files['LICENSE'].read_text())

if __name__ == '__main__':
    unittest.main()
