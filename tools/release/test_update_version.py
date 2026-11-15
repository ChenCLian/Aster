"""Regression tests for version synchronization and preservation of release evidence."""
import contextlib
import io
import json
from pathlib import Path
import os
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import update_version as bump


class VersionUpdateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.old, self.new = '1.3.0-alpha.6', '1.3.0-alpha.7'
        files = {
            'VERSION': self.old+'\n',
            'tools/release/version_policy.json': json.dumps({'schema':1,
                'current_documents':['*.md','docs/**/*.md'],
                'exclude':['docs/history/**','docs/validation_results/**','docs/development/CHANGELOG_*.md'],
                'required_headers':['README.md','RELEASE_NOTES.md','docs/reference.md']}),
            'README.md': '# Aster '+self.old+'\n\nKeep this body. Qt 6.8.2.\n[A history](docs/history/old.md)\n',
            'RELEASE_NOTES.md':'# Aster '+self.old+'\n\nOld notes. [Guide](docs/guide.md#help) [Review](REVIEW_CN.md)\n',
            'REVIEW_CN.md':'# Aster '+self.old+'\nOld tests: 2185 passed.\n',
            'docs/reference.md':'# Reference\nGenerated for Aster '+self.old+'.\n',
            'docs/guide.md':'# Aster '+self.old+'\nUse Aster-'+self.old+'-linux-x64.tar.gz\n',
            'docs/history/old.md':self.old+' historical\n',
            'docs/development/CHANGELOG_1.3.0.md':self.old+' prior entry\n',
            'docs/validation_results/'+self.old+'/checks.json':json.dumps({'version':self.old,'passed':2185}),
            'docs/validation_results/'+self.old+'/documentation_checks.json':json.dumps({'version':self.old,'documentation':{'passed':4}}),
            'PACKAGE_STATUS.json':json.dumps({'version':self.old,'release_channel':'alpha',
                'database_schema':3,'dependency_version':'6.8.2',
                'validation':{'record':'docs/validation_results/'+self.old+'/checks.json','scope':'2185 passed'},
                'documentation_review':{'version':self.old,'record':'docs/validation_results/'+self.old+'/documentation_checks.json'},
                'ci_regression_review':{'version':self.old,'status':'passed'}}),
            'config/version.pri':'ASTER_VERSION = $$cat($$PWD/../VERSION, lines)\n'
        }
        for name, value in files.items():
            path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(value,encoding='utf-8')

    def snapshot(self):
        return {p.relative_to(self.root).as_posix():p.read_bytes() for p in self.root.rglob('*') if p.is_file()}

    def test_small_bump_preserves_body_dependencies_and_evidence(self):
        before=self.snapshot(); changes=bump.plan(self.root,self.new,'修复偶发的刷新问题。')
        self.assertEqual(self.snapshot(),before)
        bump.write_changes(changes)
        self.assertEqual(bump.check(self.root),[])
        self.assertEqual((self.root/'README.md').read_text(),before['README.md'].decode().replace(self.old,self.new))
        self.assertEqual((self.root/'docs/history/old.md').read_bytes(),before['docs/history/old.md'])
        self.assertEqual((self.root/'docs/development/CHANGELOG_1.3.0.md').read_bytes(),before['docs/development/CHANGELOG_1.3.0.md'])
        self.assertEqual((self.root/'config/version.pri').read_bytes(),before['config/version.pri'])
        archived=(self.root/f'docs/history/releases/{self.old}/RELEASE_NOTES.md').read_text()
        self.assertIn(self.old,archived);self.assertIn('../../../guide.md#help',archived)
        self.assertIn('[Review](REVIEW_CN.md)',archived)
        self.assertNotIn('Old notes',(self.root/'RELEASE_NOTES.md').read_text())
        self.assertIn('修复偶发',(self.root/'RELEASE_NOTES.md').read_text())
        status=json.loads((self.root/'PACKAGE_STATUS.json').read_text())
        self.assertEqual(status['database_schema'],3);self.assertEqual(status['dependency_version'],'6.8.2')
        self.assertEqual(status['ci_regression_review']['version'],self.old)
        self.assertEqual(status['validation']['status'],'pending')
        self.assertNotIn('2185',json.dumps(status['validation']))
        record=json.loads((self.root/status['validation']['record']).read_text())
        self.assertEqual(record['status'],'pending');self.assertNotIn('passed',record)

    def test_cli_dry_run_and_notes_file(self):
        notes=self.root/'notes.txt';notes.write_text('较大的更新\n\n- 新功能说明\n',encoding='utf-8-sig')
        before=self.snapshot()
        with patch.object(bump,'ROOT',self.root),contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(bump.main([self.new,'--notes-file',str(notes),'--dry-run']),0)
            self.assertEqual(self.snapshot(),before)
            self.assertEqual(bump.main([self.new,'--notes-file',str(notes)]),0)
            self.assertEqual(bump.main(['--check']),0)
        self.assertIn('新功能说明',(self.root/'RELEASE_NOTES.md').read_text())

    def test_semver_order_and_invalid_versions(self):
        self.assertLess(bump.version_key('1.3.0-alpha.9'),bump.version_key('1.3.0-alpha.10'))
        self.assertLess(bump.version_key('1.3.0-rc.9'),bump.version_key('1.3.0'))
        for version in ['v1.3.0','01.3.0','1.3','1.3.0-alpha.01','../1.3.0','1.3.0+build']:
            with self.subTest(version=version),self.assertRaises(ValueError):bump.version_key(version)
        for version in [self.old,'1.3.0-alpha.5','1.2.9']:
            with self.subTest(version=version),self.assertRaises(ValueError):bump.plan(self.root,version,'note')

    def test_empty_notes_and_historical_collisions_leave_everything_untouched(self):
        before=self.snapshot()
        with self.assertRaises(ValueError):bump.plan(self.root,self.new,'  ')
        with self.assertRaises(ValueError):bump.plan(self.root,self.new,'# Aster 1.3.0 发布说明\nbody')
        self.assertEqual(self.snapshot(),before)
        path=self.root/f'docs/history/releases/{self.old}/RELEASE_NOTES.md';path.parent.mkdir(parents=True);path.write_text('saved')
        before=self.snapshot()
        with self.assertRaises(ValueError):bump.plan(self.root,self.new,'note')
        self.assertEqual(self.snapshot(),before)

    def test_token_boundaries_links_protection_and_stable_versions(self):
        text=f'Aster {self.old}. Aster-{self.old}-linux-x64.tar.gz {self.old}0\n'
        text+=f'[{self.old}](docs/validation_results/{self.old}/checks.json)\n'
        text+=f'<!-- version-sync: off -->{self.old}<!-- version-sync: on -->'
        actual=bump.replace_version(text,self.old,self.new)
        self.assertIn('Aster '+self.new+'.',actual)
        self.assertIn('Aster-'+self.new+'-linux',actual)
        self.assertIn(self.old+'0',actual)
        self.assertIn(f'[{self.old}](docs/validation_results/{self.old}/checks.json)',actual)
        self.assertIn('off -->'+self.old,actual)
        actual=bump.replace_version('Aster 1.3.0.\nDependency 1.3.0\n适用版本：1.3.0\n','1.3.0','1.3.1')
        self.assertIn('Aster 1.3.1.',actual);self.assertIn('Dependency 1.3.0',actual)
        self.assertIn('适用版本：1.3.1',actual)

    def test_check_and_preflight_reject_stale_header_or_metadata(self):
        (self.root/'docs/reference.md').write_text('# Aster 1.3.0-alpha.5\n')
        self.assertTrue(bump.check(self.root))
        before=self.snapshot()
        with self.assertRaises(ValueError):bump.plan(self.root,self.new,'note')
        self.assertEqual(before,self.snapshot())

    def test_release_tags_sync_and_stale_tags_are_detected(self):
        guide = self.root / 'docs/guide.md'
        guide.write_text(f'# Aster {self.old}\ngit tag v{self.old}\n'
                         f'https://github.com/example/Aster/releases/tag/v{self.old}\n')
        bump.write_changes(bump.plan(self.root, self.new, 'Update.'))
        self.assertEqual(bump.check(self.root), [])
        self.assertIn('git tag v' + self.new, guide.read_text())
        self.assertIn('/tag/v' + self.new, guide.read_text())
        guide.write_text(guide.read_text() + '\nStale tag: v' + self.old)
        self.assertTrue(any('Stale version ' + self.old in error for error in bump.check(self.root)))
        text = f'prefix{self.old} v{self.old}0 v{self.old}suffix'
        self.assertEqual(bump.replace_version(text, self.old, self.new), text)

    def test_rollback_and_stale_plan(self):
        before=self.snapshot();changes=bump.plan(self.root,self.new,'note')
        real_replace=os.replace;calls=0
        def fail_once(*args):
            nonlocal calls
            calls+=1
            if calls==3:raise OSError('simulated disk failure')
            return real_replace(*args)
        with patch.object(bump.os,'replace',fail_once),self.assertRaises(OSError):bump.write_changes(changes)
        self.assertEqual(before,self.snapshot())
        self.assertFalse(list(self.root.rglob('.version-*')))
        (self.root/'README.md').write_text('new user edit')
        changed=self.snapshot()
        with self.assertRaises(ValueError):bump.write_changes(changes)
        self.assertEqual(changed,self.snapshot())

    def test_symlink_not_written(self):
        target=self.root/'external.txt';target.write_text('Aster '+self.old)
        path=self.root/'linked.md'
        try:path.symlink_to(target)
        except (OSError,NotImplementedError):self.skipTest('Symlinks unavailable')
        with self.assertRaises(ValueError):bump.plan(self.root,self.new,'note')
        self.assertEqual(target.read_text(),'Aster '+self.old)

    def test_stable_release_and_following_patch_bump(self):
        bump.write_changes(bump.plan(self.root,'1.3.0','正式发布。'))
        self.assertEqual(bump.check(self.root),[])
        bump.write_changes(bump.plan(self.root,'1.3.1','小修复。'))
        self.assertEqual(bump.check(self.root),[])
        self.assertIn('Aster-1.3.1-linux', (self.root/'docs/guide.md').read_text())

if __name__=='__main__':unittest.main()
