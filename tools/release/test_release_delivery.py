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


    def run_delivery(self, folder, existing=True, publish_after_upload=False, wrong_digest=False):
        import copy
        import subprocess
        from urllib.parse import unquote
        version='1.3.0-beta.18';tag='v'+version
        release={'id':42,'tag_name':tag,'name':'Aster '+version,'target_commitish':'a'*40,
                 'draft':True,'prerelease':False,
                 'upload_url':f'https://uploads.github.com/repos/{r.REPO}/releases/42/assets{{?name,label}}'}
        calls=[];uploads=[]
        def api(args,payload=None,timeout=120):
            calls.append((list(args),copy.deepcopy(payload)))
            endpoint=args[0]
            if '/releases?' in endpoint:return [release.copy()] if existing else []
            if args[-2:]==['--method','PATCH']:
                release.update(payload);return release.copy()
            if endpoint==f'repos/{r.REPO}/releases' and '--method' in args:
                release.update(payload);return release.copy()
            if endpoint.endswith('/assets?per_page=100&page=1'):return []
            if endpoint.startswith('https://uploads.github.com/'):
                path=Path(args[args.index('--input')+1]);uploads.append(path)
                self.assertEqual(unquote(endpoint.split('?name=')[1]),path.name)
                self.assertEqual(args[args.index('-H')+1],'Content-Type: application/octet-stream')
                if publish_after_upload:release['draft']=False
                return dict(name=path.name,state='uploaded',size=path.stat().st_size,
                            digest='sha256:'+('0'*64 if wrong_digest else r.digest(path)))
            if endpoint==f'repos/{r.REPO}/releases/42':return release.copy()
            raise AssertionError('Unexpected API call: '+repr(args))
        def run(args,**kw):
            self.assertEqual(args[:2],['gh','api'])
            self.assertIn('/git/ref/tags/',args[2])
            return subprocess.CompletedProcess(args,1,'','HTTP 404')
        with patch.object(r,'github_json',side_effect=api),patch.object(r.subprocess,'run',side_effect=run):
            if publish_after_upload or wrong_digest:
                with self.assertRaisesRegex(ValueError,'no longer|verification failed'):
                    r.publish_draft(folder,version,'a'*40)
                self.assertEqual(len(uploads),1)
            else:r.publish_draft(folder,version,'a'*40)
        return calls,uploads

    def test_created_draft_uses_returned_id_even_when_list_stays_empty(self):
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            calls,uploads=self.run_delivery(folder,existing=False)
            self.assertEqual(sum('/releases?' in a[0] for a,p in calls),1)
            creates=[p for a,p in calls if a==[f'repos/{r.REPO}/releases','--method','POST']]
            self.assertEqual(len(creates),1)
            self.assertEqual(creates[0]['target_commitish'],'a'*40)
            self.assertEqual(creates[0]['body'],'Prepared notes')
            self.assertTrue(creates[0]['draft']);self.assertTrue(creates[0]['prerelease'])
            self.assertEqual(len(uploads),9)

    def test_existing_draft_channel_fixed_notes_preserved_and_retry_sums_clean(self):
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder);(folder/'SHA256SUMS.txt').write_text('old stale checksum')
            calls,uploads=self.run_delivery(folder)
            edits=[p for a,p in calls if a[-2:]==['--method','PATCH']]
            self.assertEqual(edits,[{'tag_name':'v1.3.0-beta.18','prerelease':True,'target_commitish':'a'*40}])
            self.assertNotIn('SHA256SUMS.txt',(folder/'SHA256SUMS.txt').read_text())
            self.assertEqual(len(uploads),9)

    def test_publication_during_upload_stops_further_overwrites(self):
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            self.run_delivery(folder,publish_after_upload=True)

    def test_bad_upload_hash_stops_delivery(self):
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            self.run_delivery(folder,wrong_digest=True)

    def test_untagged_draft_recovered_only_for_exact_title_and_commit(self):
        draft={'id':42,'tag_name':'untagged-abcdef','draft':True,'name':'Aster 1.3.1-beta.1','target_commitish':'a'*40}
        with patch.object(r,'github_json',return_value=[draft]):
            self.assertEqual(r.find_release('v1.3.1-beta.1','a'*40),draft)
            with self.assertRaisesRegex(ValueError,'different target'):r.find_release('v1.3.1-beta.1','b'*40)
            self.assertIsNone(r.find_release('v1.3.1-beta.2','a'*40))

    def test_duplicate_drafts_blocked_including_later_pages(self):
        draft={'id':42,'tag_name':'v1.3.1-beta.1','draft':True}
        other=[{'tag_name':'v0.0.'+str(n)} for n in range(99)]
        with patch.object(r,'github_json',side_effect=[[draft]+other,[dict(draft,id=43)]]):
            with self.assertRaisesRegex(ValueError,'Multiple'):r.find_release('v1.3.1-beta.1','a'*40)

    def test_draft_tag_changed_blocks_mutation(self):
        with patch.object(r,'github_json',return_value={'draft':True,'tag_name':'v1.3.1-beta.2'}):
            with self.assertRaisesRegex(ValueError,'no longer'):r.require_draft(42,'v1.3.1-beta.1')

    def test_matching_asset_hash_skips_reupload(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'fixture.zip';path.write_bytes(b'fixture')
            asset={'id':7,'name':path.name,'state':'uploaded','size':7,'digest':'sha256:'+r.digest(path)}
            draft={'draft':True,'tag_name':'v1.3.1-beta.1','prerelease':True}
            with patch.object(r,'github_json',side_effect=[draft,[asset]]) as api:
                r.upload_asset(42,'v1.3.1-beta.1',path,True)
            self.assertEqual(api.call_count,2)

    def test_upload_host_is_checked(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'fixture.zip';path.write_bytes(b'fixture')
            draft={'draft':True,'tag_name':'v1.3.1-beta.1','prerelease':True,'upload_url':'https://example.org/upload'}
            with patch.object(r,'github_json',side_effect=[draft,[],draft]):
                with self.assertRaisesRegex(ValueError,'endpoint'):r.upload_asset(42,'v1.3.1-beta.1',path,True)

    def test_existing_tag_wrong_commit_stops_before_patch(self):
        import json,subprocess
        draft={'id':42,'tag_name':'v1.3.0-beta.18','draft':True}
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            with patch.object(r,'find_release',return_value=draft),patch.object(r,'github_json') as api,patch.object(r.subprocess,'run',return_value=subprocess.CompletedProcess([],0,json.dumps({'object':{'type':'commit','sha':'b'*40}}),' ')):
                with self.assertRaisesRegex(ValueError,'tested commit'):r.publish_draft(folder,'1.3.0-beta.18','a'*40)
            api.assert_not_called()

    def test_api_json_encoding_and_cache_control(self):
        import json,subprocess
        with patch.object(r.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'{"id":42}')) as run:
            self.assertEqual(r.github_json(['repos/test/releases','--method','POST'],payload={'body':'中文说明'}),{'id':42})
            args=run.call_args.args[0];kw=run.call_args.kwargs
            self.assertIn('Cache-Control: no-cache',args)
            self.assertEqual(args[-2:],['--input','-'])
            self.assertEqual(json.loads(kw['input']),{'body':'中文说明'})
            self.assertEqual(kw['encoding'],'utf-8')

    def test_problem_filename_normalized_before_upload_and_all_integrity_checks_remain(self):
        from urllib.parse import unquote
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'gcc-12_12.3.0-1ubuntu1~22.04.3.debian.tar.xz';path.write_bytes(b'bytes')
            draft={'draft':True,'tag_name':'v1.3.1-beta.1','prerelease':True,
                   'upload_url':f'https://uploads.github.com/repos/{r.REPO}/releases/42/assets{{?name,label}}'}
            calls=[]
            def api(args,**kwargs):
                calls.append(args)
                if '/assets?' in args[0] and not args[0].startswith('https:'):return []
                if args[0].startswith('https:'):
                    requested=unquote(args[0].split('?name=')[1])
                    self.assertEqual(requested,'gcc-12_12.3.0-1ubuntu1_22.04.3.debian.tar.xz')
                    return dict(name=requested,state='uploaded',size=5,digest='sha256:'+r.digest(path))
                return draft
            with patch.object(r,'github_json',side_effect=api):r.upload_asset(42,'v1.3.1-beta.1',path,True)
            self.assertEqual(path.read_bytes(),b'bytes')

    def test_existing_github_renamed_file_reused_by_digest_without_upload(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'gcc_1~2.tar.xz';path.write_bytes(b'bytes')
            draft={'draft':True,'tag_name':'v1.3.1-beta.1','prerelease':True}
            old=dict(id=12,name='gcc_1.2.tar.xz',state='uploaded',size=5,digest='sha256:'+r.digest(path))
            with patch.object(r,'github_json',side_effect=[draft,[old],draft,dict(old,name='gcc_1_2.tar.xz')]) as api:
                r.upload_asset(42,'v1.3.1-beta.1',path,True)
            self.assertEqual(api.call_args.args[0], [f'repos/{r.REPO}/releases/assets/12','--method','PATCH'])
            self.assertEqual(api.call_args.kwargs['payload'],{'name':'gcc_1_2.tar.xz'})

    def test_verification_errors_report_each_field(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'file.zip';path.write_bytes(b'bytes')
            sha='sha256:'+r.digest(path)
            good=dict(name=path.name,state='uploaded',size=5,digest=sha)
            for key,value in [('name','renamed.zip'),('state','starter'),('size',4),('digest','sha256:'+'0'*64)]:
                with self.subTest(key=key):
                    with self.assertRaisesRegex(ValueError,key) as error:
                        r.verify_uploaded(dict(good,**{key:value}),path,path.name,sha)
                    self.assertIn('expected',str(error.exception));self.assertIn('actual',str(error.exception))

    def test_names_and_collision_preflight(self):
        for name in ['file.tar.xz','Aster-1.3.1-beta.1-windows-x64.zip','SHA256SUMS.txt']:
            self.assertEqual(r.asset_name(name),name)
        for name in ['a~b.tar.xz','a+b.orig.tar.gz','a b.zip','.hidden.','中文.tar.xz']:
            self.assertRegex(r.asset_name(name),r'^[A-Za-z0-9_-][A-Za-z0-9._-]*$')
            self.assertEqual(r.asset_name(r.asset_name(name)),r.asset_name(name))
        for name in ['../bad','a/b','a\\b','..','x\nfile']:
            with self.assertRaises(ValueError):r.asset_name(name)
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            (folder/'gcc~1.tar.xz').write_bytes(b'a');(folder/'gcc_1.tar.xz').write_bytes(b'b')
            with self.assertRaisesRegex(ValueError,'collision'):r.prepared_files(folder,'1.3.0-beta.18')

    def test_mapping_and_checksum_names_match_uploaded_names(self):
        import json
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td);self.prepared(folder)
            source=folder/'gcc_1~2.tar.xz';source.write_bytes(b'original archive')
            # Exercise publication with actual metadata generation; uploaded names
            # are checked separately against the transport tests above.
            release={'id':42,'tag_name':'v1.3.0-beta.18','draft':True}
            import subprocess
            with patch.object(r,'find_release',return_value=release),patch.object(r,'require_draft',return_value=release),patch.object(r,'github_json'),patch.object(r.subprocess,'run',return_value=subprocess.CompletedProcess([],1,'','HTTP 404')),patch.object(r,'upload_asset') as upload:
                r.publish_draft(folder,'1.3.0-beta.18','a'*40)
            sums=(folder/'SHA256SUMS.txt').read_text()
            self.assertIn(r.digest(source)+'  gcc_1_2.tar.xz',sums)
            self.assertNotIn('~',sums)
            mapping=json.loads((folder/'Aster-asset-filenames.json').read_text())
            item=next(a for a in mapping['assets'] if a['source_name']==source.name)
            self.assertEqual(item['asset_name'],'gcc_1_2.tar.xz');self.assertEqual(item['sha256'],r.digest(source))
            self.assertIn('/gcc_1_2.tar.xz',(folder/'DEPENDENCY_DOWNLOADS.md').read_text())
            self.assertEqual(len(r.prepared_files(folder,'1.3.0-beta.18')),7)

    def test_same_bytes_cannot_steal_another_expected_or_unrelated_asset(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'gcc_1~2.tar.xz';path.write_bytes(b'bytes')
            draft={'draft':True,'tag_name':'v1.3.1-beta.1','prerelease':True,
                   'upload_url':f'https://uploads.github.com/repos/{r.REPO}/releases/42/assets{{?name,label}}'}
            for remote,protected in [('gcc_1.2.tar.xz',{'gcc_1.2.tar.xz'}),('another-source.tar.xz',set())]:
                old=dict(id=12,name=remote,state='uploaded',size=5,digest='sha256:'+r.digest(path))
                with patch.object(r,'github_json',side_effect=[draft,[old],draft,dict(old,name='gcc_1_2.tar.xz')]) as api:
                    r.upload_asset(42,'v1.3.1-beta.1',path,True,protected_names=protected)
                self.assertEqual(api.call_args.args[0][1:3],['--method','POST'])
