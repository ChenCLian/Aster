"""Keep current release instructions in sync without rewriting historical evidence."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CurrentVersionTests(unittest.TestCase):
    def test_current_document_headers_match_version(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        for name in ['README.md', 'RELEASE_NOTES.md', 'QUICK_START.md',
                     'FAQ.md', 'docs/README.md', 'docs/USER_GUIDE.md', 'docs/USER_GUIDE_EN.md',
                     'docs/methods/cwt.md',
                     'docs/Aster_development_checklist.md', 'docs/numeric_backends.md',
                     'docs/building.md', 'docs/architecture.md', 'docs/add_task.md',
                     'docs/task_catalogue.md', 'docs/ui/plotting.md', 'docs/ui/live_preview.md',
                     'docs/development/cross_spectrum_contract.md',
                     'docs/development/documentation.md', 'docs/development/plotting.md',
                     'docs/development/workflow_preview.md',
                     'docs/release_maintenance_zh.md', 'docs/dependency_materials_zh.md',
                     'docs/development/cross_spectrum_interfaces.md',
                     'docs/development/batch_joint_analysis.md', 'docs/validation_current.md',
                     'docs/reference/task_parameters.md', 'docs/reference/task_parameters_en.md']:
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
                candidates = re.findall(r'\b\d+\.\d+\.\d+-(?:alpha|beta|rc)\.\d+\b',
                                        (ROOT / name).read_text(encoding='utf-8'))
                self.assertTrue(all(candidate == version for candidate in candidates), candidates)

    def test_all_current_docs_reject_stale_prerelease_text(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        # History and validation indices deliberately cite earlier versions.
        historical = ('docs/history/', 'docs/validation_results/')
        indices = {'docs/validation_current.md', 'docs/development/CHANGELOG_1.2.0.md'}
        documents = list(ROOT.glob('*.md')) + list((ROOT / 'docs').rglob('*.md'))
        for path in documents:
            name = path.relative_to(ROOT).as_posix()
            if name.startswith(historical) or name in indices:
                continue
            text = path.read_text(encoding='utf-8')
            # A link to a retained historical report keeps its original label/path.
            text = re.sub(r'\[[^\]\n]*\]\((?:\.\./)*(?:docs/)?(?:history|validation_results)/[^)\n]+\)', '', text)
            with self.subTest(file=name):
                candidates = re.findall(r'\b\d+\.\d+\.\d+-(?:alpha|beta|rc)\.\d+\b', text)
                self.assertTrue(all(candidate == version for candidate in candidates), candidates)

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
