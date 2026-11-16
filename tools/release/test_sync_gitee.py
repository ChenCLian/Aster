import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from sync_gitee import mirror_packages, public_download, publish_metadata, rank, allowed_public_url
from release_delivery import package_names
from download_metadata import digest

class API:
    def __init__(self,assets=None): self.uploaded=[];self.existing=assets or []
    def assets(self,rid): return self.existing
    def upload(self,rid,path): self.uploaded.append(path.name)

class MirrorTests(unittest.TestCase):
    def fixtures(self,folder):
        packages=[]
        for name in sorted(package_names('1.3.0-beta.18')):
            p=folder/name;p.write_bytes(b'archive')
            packages.append(dict(file=name,bytes=7,sha256=digest(p),sources=[dict(name='GitHub',url='https://github.com/original')]))
        (folder/'dependency-materials-windows-x64').mkdir()
        (folder/'dependency-materials-windows-x64'/packages[0]['file']).write_bytes(b'do not upload')
        return dict(schema=1,version='1.3.0-beta.18',packages=packages)
    def test_only_verified_programs_gain_mirror(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=self.fixtures(folder);api=API();calls=[]
            result,plan=mirror_packages(api,1,folder,m,'1.3.0-beta.18',lambda *args:calls.append(args))
            self.assertEqual(set(api.uploaded),package_names('1.3.0-beta.18'))
            self.assertEqual(len(calls),4)
            self.assertTrue(all(p['sources'][-1]['name']=='Gitee' for p in result['packages']))
            self.assertEqual(len(m['packages'][0]['sources']),1)
            self.assertIn('dependency-materials-never-mirrored',[p['reason'] for p in plan['excluded']])
    def test_hash_failure_never_publishes_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=self.fixtures(folder);api=API()
            def reject(*args):raise ValueError('Mirror SHA-256 mismatch')
            with self.assertRaises(ValueError):mirror_packages(api,1,folder,m,'1.3.0-beta.18',reject)
            self.assertNotIn('Aster-update.json',api.uploaded)
    def test_existing_programs_are_verified_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=self.fixtures(folder);api=API([{'name':p['file']} for p in m['packages']])
            mirror_packages(api,1,folder,m,'1.3.0-beta.18',lambda *args:None)
            self.assertEqual(api.uploaded,[])
    def test_conflicting_metadata_not_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'Aster-update.json';p.write_text('{"version":"1.3.0-beta.18"}')
            api=API([{'name':p.name,'id':12}])
            with self.assertRaises(ValueError):publish_metadata(api,1,p,lambda *a:(_ for _ in ()).throw(ValueError('mismatch')))
            self.assertFalse(api.uploaded)
    def test_unapproved_redirect_never_requested(self):
        with tempfile.TemporaryDirectory() as tmp:
            for url in ('http://gitee.com/a','https://evil.test/asset','https://token@gitee.com/attachments/download/a','https://gitee.com/other/asset'):
                with self.assertRaises(ValueError):public_download(url,Path(tmp)/'x',100)
    def test_official_attachment_redirects_only(self):
        self.assertTrue(allowed_public_url('https://gitee.com/ChenCLian/Aster/releases/download/v1.3.0/Aster-update.json'))
        self.assertTrue(allowed_public_url('https://gitee.com/ChenCLian/Aster/attach_files/123/download/Aster-update.json',True))
        self.assertTrue(allowed_public_url('https://foruda.gitee.com/attach_file/123/archive.zip?token=temporary',True))
        self.assertFalse(allowed_public_url('https://foruda.gitee.com/attach_file/123/archive.zip'))
        for url in ('https://foruda.gitee.com.evil.test/attach_file/123/a','http://foruda.gitee.com/attach_file/123/a','https://gitee.com/other/repo/attach_files/123/download','https://foruda.gitee.com/images/123/a'):
            self.assertFalse(allowed_public_url(url,True))
    def test_channels_use_numeric_order_and_exclude_alpha(self):
        self.assertGreater(rank('1.3.0-beta.18'),rank('1.3.0-beta.9'))
        self.assertGreater(rank('1.3.0'),rank('1.3.0-beta.18'))
        with self.assertRaises(ValueError):rank('1.3.0-alpha.19')

if __name__=='__main__': unittest.main()

class SourceMirrorTests(unittest.TestCase):
    def test_source_snapshot_is_idempotent_and_materials_never_enter_git(self):
        import subprocess
        import sync_gitee as module
        import package_source
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);source=base/'source';source.mkdir();remote=base/'remote.git'
            subprocess.run(['git','init','--bare','-q',str(remote)],check=True)
            (source/'VERSION').write_text('1.3.0-beta.18\n')
            (source/'README.md').write_text('[Guide](docs/guide.md)')
            (source/'docs').mkdir();(source/'docs/guide.md').write_text('Shared guide')
            (source/'config').mkdir();(source/'config/local.pri').write_text('PRIVATE')
            (source/'dependency-materials-linux-x64').mkdir();(source/'dependency-materials-linux-x64/large.tar.xz').write_bytes(b'must not be mirrored')
            real_run=subprocess.run
            def run(args,**kwargs):
                if args[-4:-1]==['remote','add','origin']:args=[*args[:-1],str(remote)]
                return real_run(args,**kwargs)
            with patch.object(module,'ROOT',source),patch.object(package_source,'ROOT',source),patch.object(module.subprocess,'run',side_effect=run):
                for i in range(2):
                    work=base/f'work-{i}';work.mkdir()
                    self.assertEqual(module.snapshot_source('1.3.0-beta.18','test-token',work),'release-beta')
                tracked=real_run(['git','--git-dir',str(remote),'ls-tree','-r','--name-only','v1.3.0-beta.18'],capture_output=True,text=True,check=True).stdout
                self.assertIn('docs/guide.md',tracked)
                self.assertNotIn('dependency-material',tracked);self.assertNotIn('config/local.pri',tracked)
                (source/'VERSION').write_text('1.3.0-beta.17\n');work=base/'old';work.mkdir()
                with self.assertRaisesRegex(ValueError,'backwards'):module.snapshot_source('1.3.0-beta.17','test-token',work)
                (source/'VERSION').write_text('1.3.0-beta.18\n');(source/'README.md').write_text('conflicting source');work=base/'conflict';work.mkdir()
                with self.assertRaisesRegex(ValueError,'different source'):module.snapshot_source('1.3.0-beta.18','test-token',work)
