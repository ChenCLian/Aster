"""Keep current release instructions in sync without rewriting historical evidence."""
import json
from pathlib import Path
import re
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_version

ROOT = Path(__file__).resolve().parents[2]


class CurrentVersionTests(unittest.TestCase):
    def test_current_document_headers_match_version(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        for name in update_version.policy(ROOT)['required_headers']:
            with self.subTest(file=name):
                header = '\n'.join((ROOT / name).read_text(encoding='utf-8').splitlines()[:8])
                self.assertIn(version, header)
        status = json.loads((ROOT / 'PACKAGE_STATUS.json').read_text(encoding='utf-8'))
        self.assertEqual(status['version'], version)
        self.assertEqual(status['documentation_review']['version'], version)

    def test_release_commands_do_not_use_an_old_candidate(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        # These are current operational instructions, not historical evidence.
        for name in ['README.md', 'QUICK_START.md', 'docs/release_maintenance_zh.md',
                     'docs/dependency_materials_zh.md']:
            with self.subTest(file=name):
                candidates = update_version.TOKENS.findall((ROOT / name).read_text(encoding='utf-8'))
                self.assertTrue(all(candidate == version for candidate in candidates), candidates)

    def test_all_current_docs_reject_stale_prerelease_text(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        self.assertEqual(update_version.check(ROOT), [])

    def test_documentation_review_has_its_own_matching_record(self):
        status = json.loads((ROOT / 'PACKAGE_STATUS.json').read_text(encoding='utf-8'))
        record = status['documentation_review']['record']
        self.assertNotEqual(record, status['validation']['record'],
                            'A UI regression report does not document a separate documentation audit')
        evidence = json.loads((ROOT / record).read_text(encoding='utf-8'))
        self.assertEqual(evidence['version'], status['version'])
        self.assertIn('documentation', evidence)


if __name__ == '__main__':
    unittest.main()
