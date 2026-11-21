import json
import hashlib
import io
from types import SimpleNamespace
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from sync_gitee import Gitee, http_failure, mirror_packages, public_download, with_update_record, execute, allowed_public_url
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
            self.assertNotIn('AstroBatch-update.json',api.uploaded)
    def test_existing_programs_are_verified_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);m=self.fixtures(folder);api=API([{'name':p['file']} for p in m['packages']])
            mirror_packages(api,1,folder,m,'1.3.0-beta.18',lambda *args:None)
            self.assertEqual(api.uploaded,[])
    def test_streamed_upload_matches_declared_length_and_original_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / 'program.zip'
            payload = bytes(range(256)) * 8223
            file.write_bytes(payload)
            api = Gitee('fixture')
            def receive(request, timeout):
                self.assertEqual(timeout, 300)
                self.assertEqual(request.method, 'POST')
                chunks = list(request.data)
                self.assertGreater(len(chunks), 3)
                self.assertEqual(int(request.get_header('Content-length')), sum(map(len, chunks)))
                self.assertIn(b'filename="program.zip"', chunks[0])
                self.assertEqual(b''.join(chunks[1:-1]), payload)
                return io.BytesIO(b'{"id":1}')
            with patch.object(api.opener, 'open', side_effect=receive):
                self.assertEqual(api.upload(1, file), {'id':1})

    @patch('sync_gitee.time.sleep')
    def test_uncertain_upload_rechecks_server_before_retry(self, sleep):
        for accepted in (True, False):
            with self.subTest(accepted=accepted), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp)
                meta = self.fixtures(folder)
                api = API()
                first = meta['packages'][0]['file']
                attempts = []
                def upload(rid, path):
                    attempts.append(path.name)
                    if len(attempts) == 1:
                        if accepted:
                            api.existing.append({'name': path.name})
                        raise TimeoutError('socket timed out')
                api.upload = upload
                verified = []
                mirror_packages(api, 1, folder, meta, meta['version'], lambda *args: verified.append(args[0]))
                self.assertEqual(attempts.count(first), 1 if accepted else 2)
                self.assertEqual(len(verified), 4)

    @patch('sync_gitee.time.sleep')
    def test_transient_public_read_retries_without_reupload(self, sleep):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            meta = self.fixtures(folder)
            api = API()
            calls = []
            def read(url, *args):
                calls.append(url)
                if len(calls) == 1:
                    raise http_failure('Read', 503)
            mirror_packages(api, 1, folder, meta, meta['version'], read)
            self.assertEqual(len(api.uploaded), 4)
            self.assertEqual(len(calls), 5)
            self.assertEqual(calls[0], calls[1])

    @patch('sync_gitee.time.sleep')
    def test_upload_retries_are_bounded_and_permissions_fail_immediately(self, sleep):
        for error, limit in [(TimeoutError(), 3), (http_failure('Upload', 403), 1)]:
            with self.subTest(error=error), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp)
                meta = self.fixtures(folder)
                api = API()
                with patch.object(api, 'upload', side_effect=error) as upload:
                    with self.assertRaises(type(error)):
                        mirror_packages(api, 1, folder, meta, meta['version'])
                    self.assertEqual(upload.call_count, limit)

    def test_extra_remote_asset_is_reported_without_deletion(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp); metadata=self.fixtures(folder); api=API([{'name':'AstroBatch-update.json'}])
            with self.assertRaisesRegex(ValueError,'Unexpected existing'):
                mirror_packages(api,1,folder,metadata,'1.3.0-beta.18')
            self.assertFalse(api.uploaded)

    def test_update_record_preserves_notes_and_survives_crlf_roundtrip(self):
        body=with_update_record('Manual notes',{'version':'old'})
        expected=with_update_record('Manual notes',{'version':'new'})
        self.assertEqual(with_update_record(body.replace('\n','\r\n'),{'version':'new'}), expected)
        self.assertTrue(expected.startswith('Manual notes\n'))
        self.assertEqual(with_update_record(expected,{'version':'new'}),expected)
        with self.assertRaisesRegex(ValueError,'Malformed'):
            with_update_record('<!-- astrobatch-update-v1\n{}',{})
    def test_unapproved_redirect_never_requested(self):
        with tempfile.TemporaryDirectory() as tmp:
            for url in ('http://gitee.com/a','https://evil.test/asset','https://token@gitee.com/attachments/download/a','https://gitee.com/other/asset'):
                with self.assertRaises(ValueError):public_download(url,Path(tmp)/'x',100)
    def test_official_attachment_redirects_only(self):
        self.assertTrue(allowed_public_url('https://gitee.com/ChenCLian/AstroBatch/releases/download/v1.3.0/AstroBatch-update.json'))
        self.assertTrue(allowed_public_url('https://gitee.com/ChenCLian/AstroBatch/attach_files/123/download/AstroBatch-update.json',True))
        self.assertTrue(allowed_public_url('https://foruda.gitee.com/attach_file/123/archive.zip?token=temporary',True))
        self.assertFalse(allowed_public_url('https://foruda.gitee.com/attach_file/123/archive.zip'))
        for url in ('https://foruda.gitee.com.evil.test/attach_file/123/a','http://foruda.gitee.com/attach_file/123/a','https://gitee.com/other/repo/attach_files/123/download','https://foruda.gitee.com/images/123/a'):
            self.assertFalse(allowed_public_url(url,True))
    def test_real_mirror_entrypoint_writes_only_four_assets_and_resumes(self):
        from test_release_delivery import ReleaseDeliveryTests
        import sync_gitee
        version='1.3.1-beta.2'; tag='v'+version; known=package_names(version)
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            ReleaseDeliveryTests().prepared(folder,version)
            (folder/'SHA256SUMS.txt').write_text('old  AstroBatch-update.json\n')
            gh_files={p.name:p.read_bytes() for p in folder.iterdir()}
            remote={}; attachments={}; methods=[]; uploads=[]; verified=[]
            def gh(args,**kwargs):
                self.assertEqual(args[0],'gh')  # Cannot push a source branch.
                op=args[2]
                if op=='view':
                    data=dict(isDraft=False,tagName=tag,body='Release notes',assets=[
                        dict(name=k,size=len(v),digest='sha256:'+hashlib.sha256(v).hexdigest()) for k,v in gh_files.items()])
                    return subprocess.CompletedProcess(args,0,json.dumps(data))
                if op=='download':
                    name=args[args.index('--pattern')+1]
                    self.assertIn(name,known|{'AstroBatch-update.json','SHA256SUMS.txt'})
                    (Path(args[args.index('--dir')+1])/name).write_bytes(gh_files[name])
                elif op=='upload':
                    p=Path(args[4]);self.assertIn(p.name,{'AstroBatch-update.json','SHA256SUMS.txt'})
                    gh_files[p.name]=p.read_bytes()
                else:self.fail(op)
                return subprocess.CompletedProcess(args,0,'')
            class Mirror:
                def api(self,path='',method='GET',data=None,missing=False,anonymous=False):
                    methods.append((path,method,anonymous))
                    if not path:return {'default_branch':'main'}
                    if path=='releases' and method=='POST':
                        remote.update(data,id=42)
                    elif path=='releases/42' and method=='PATCH':
                        remote.update(data)
                    elif path!='releases/tags/'+tag:raise AssertionError(path)
                    return dict(remote) if remote else None
                def assets(self,rid):return [dict(name=name) for name in attachments]
                def upload(self,rid,path):
                    uploads.append(path.name);attachments[path.name]=path.read_bytes()
            def verify(url,path,limit,expected_hash,expected_size):
                name=url.rsplit('/',1)[1];path.write_bytes(attachments[name])
                self.assertEqual(digest(path),expected_hash)
                self.assertEqual(path.stat().st_size,expected_size)
                verified.append(name)
            opts=SimpleNamespace(tag=tag,report=folder/'mirror-report.json')
            with patch.object(sync_gitee,'Gitee',return_value=Mirror()),patch.object(sync_gitee.subprocess,'run',side_effect=gh),patch.object(sync_gitee,'public_download',side_effect=verify):
                execute(opts); report=json.loads(opts.report.read_text())
                first=dict(gh_files);execute(opts)
                self.assertEqual(gh_files,first)
                del gh_files['AstroBatch-update.json'],gh_files['SHA256SUMS.txt']
                execute(opts)
                self.assertEqual(json.loads(gh_files['AstroBatch-update.json']),json.loads(first['AstroBatch-update.json']))
                for line in gh_files['SHA256SUMS.txt'].decode().splitlines():
                    sha,name=line.split('  ')
                    self.assertEqual(sha,hashlib.sha256(gh_files[name]).hexdigest())
            self.assertEqual(set(uploads),known);self.assertEqual(len(uploads),4)
            self.assertEqual(len(verified),12)
            self.assertEqual(report['status'],'verified')
            self.assertIn(('releases/tags/'+tag,'GET',True),methods)
            self.assertEqual(remote['body'].count('astrobatch-update-v1'),1)
            record=json.loads(remote['body'].split('<!-- astrobatch-update-v1\n')[1].split('\n-->')[0])
            self.assertEqual(record,json.loads(gh_files['AstroBatch-update.json']))
            self.assertTrue(all(row['sources'][-1]['name']=='Gitee' for row in record['packages']))

if __name__=='__main__': unittest.main()
