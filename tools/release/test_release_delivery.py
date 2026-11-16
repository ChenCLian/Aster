from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import release_delivery as r

class ReleaseDeliveryTests(unittest.TestCase):
    def test_only_four_exact_program_names(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);v='1.3.0-beta.18';names=r.package_names(v)
            for n in names:(root/n).write_bytes(b'program')
            for platform in r.PLATFORMS:
                p=root/('dependency-materials-'+platform);p.mkdir();(p/next(iter(names))).write_bytes(b'not a program')
                (p/'qt-everywhere.tar.xz').write_bytes(b'material')
            for n in ['Aster-update.json','Aster-source.zip','README.md','gsl.tar.gz']:(root/n).write_bytes(b'not program')
            plan=r.mirror_plan(root,v)
            self.assertEqual({p['file'] for p in plan['upload']},names)
            self.assertEqual(sum(p['reason']=='dependency-materials-never-mirrored' for p in plan['excluded']),8)

    def test_size_limit_never_recompresses(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);v='1.3.0';name=next(iter(r.package_names(v)));(root/name).write_bytes(b'12345')
            self.assertEqual(r.mirror_plan(root,v,limit=4)['upload'],[])
            self.assertEqual((root/name).read_bytes(),b'12345')

    def test_alpha_never_delivered(self):
        with self.assertRaises(ValueError):r.package_names('1.3.0-alpha.1')

    def test_missing_platform_stops_draft_preparation(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            with self.assertRaisesRegex(ValueError,'four'):r.prepare(root,root/'out','1.3.0')

    def test_public_release_never_overwritten(self):
        with patch.object(r,'github_json',return_value=[{'tag_name':'v1.3.0','draft':False}]):
            with self.assertRaisesRegex(ValueError,'published'):r.publish_draft(Path('.'),'1.3.0','a'*40)

    def prepared(self,folder,version='1.3.0-beta.18'):
        import io,json,tarfile,zipfile
        for platform in r.PLATFORMS:
            root=f'Aster-{version}-{platform}';name=root+('.tar.gz' if platform=='linux-x64' else '.zip')
            marker=json.dumps(dict(schema=1,version=version,platform=platform)).encode()
            if platform=='linux-x64':
                with tarfile.open(folder/name,'w:gz') as archive:
                    info=tarfile.TarInfo(root+'/aster-package.json');info.size=len(marker);archive.addfile(info,io.BytesIO(marker))
            else:
                with zipfile.ZipFile(folder/name,'w') as archive:archive.writestr(root+'/aster-package.json',marker)
        with zipfile.ZipFile(folder/f'Aster-{version}-source.zip','w') as archive:archive.writestr('VERSION',version)
        (folder/'RELEASE_GITHUB.md').write_text('Prepared notes')
        metadata=r.generate([folder/name for name in sorted(r.package_names(version))],version)
        (folder/'Aster-update.json').write_text(json.dumps(metadata))

    def test_partial_or_modified_packages_do_not_reach_remote_mutation(self):
        import json
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td)
            with self.assertRaisesRegex(ValueError,'Incomplete'):r.prepared_files(folder,'1.3.0-beta.18')
            self.prepared(folder)
            data=json.loads((folder/'Aster-update.json').read_text());data['packages'][0]['sha256']='0'*64
            (folder/'Aster-update.json').write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError,'differs'):r.prepared_files(folder,'1.3.0-beta.18')

    def test_publication_during_upload_stops_further_overwrites(self):
        import subprocess
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            release={'id':42,'tag_name':'v1.3.0-beta.18','draft':True,'prerelease':True}
            uploads=[]
            def api(args):
                if '/releases?' in args[0]:return [release.copy()]
                return release.copy()
            def run(args,**kw):
                if args[:2]==['gh','api']:return subprocess.CompletedProcess(args,1,'','HTTP 404')
                if args[1:3]==['release','upload']:uploads.append(args);release['draft']=False
                return subprocess.CompletedProcess(args,0,'','')
            with patch.object(r,'github_json',side_effect=api),patch.object(r.subprocess,'run',side_effect=run):
                with self.assertRaisesRegex(ValueError,'no longer'):r.publish_draft(folder,'1.3.0-beta.18','a'*40)
            self.assertEqual(len(uploads),1)

    def test_existing_draft_channel_fixed_notes_preserved_and_retry_sums_clean(self):
        import subprocess
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder);(folder/'SHA256SUMS.txt').write_text('old stale checksum')
            release={'id':42,'tag_name':'v1.3.0-beta.18','draft':True,'prerelease':False}
            calls=[]
            def api(args):return [release.copy()] if '/releases?' in args[0] else release.copy()
            def run(args,**kw):
                calls.append(args)
                if args[:2]==['gh','api']:return subprocess.CompletedProcess(args,1,'','HTTP 404')
                if args[1:3]==['release','edit']:release['prerelease']=True
                return subprocess.CompletedProcess(args,0,'','')
            with patch.object(r,'github_json',side_effect=api),patch.object(r.subprocess,'run',side_effect=run):
                r.publish_draft(folder,'1.3.0-beta.18','a'*40)
            edits=[c for c in calls if c[1:3]==['release','edit']]
            self.assertIn('--prerelease=true',edits[0]);self.assertIn('--target',edits[0])
            self.assertFalse(any('--notes-file' in c or '--notes' in c or '--title' in c for c in calls))
            self.assertNotIn('SHA256SUMS.txt',(folder/'SHA256SUMS.txt').read_text())
            self.assertEqual(sum(c[1:3]==['release','upload'] for c in calls),7)
