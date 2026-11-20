"""Exercise real JSON/binary subprocess transport against a local API emulator.

The emulator models GitHub filename rewriting and duplicate-name rejection;
no authenticated GitHub requests are made.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import release_delivery as delivery
import test_release_delivery as fixtures

EMULATOR = r'''
import hashlib,json,os,re,sys
from pathlib import Path
from urllib.parse import urlsplit,parse_qs
statefile=Path(os.environ['ASTER_FAKE_RELEASE_STATE'])
state=json.loads(statefile.read_text())
args=sys.argv[1:]
assert args.pop(0)=='api'
endpoint=args.pop(0)
method=args[args.index('--method')+1] if '--method' in args else 'GET'
body=None
if '--input' in args:
    source=args[args.index('--input')+1]
    body=sys.stdin.buffer.read() if source=='-' else Path(source).read_bytes()
result=None
if '/git/ref/tags/' in endpoint:
    print('HTTP 404',file=sys.stderr);sys.exit(1)
elif endpoint.startswith('https://uploads.github.com/'):
    assert method=='POST' and 'Content-Type: application/octet-stream' in args
    requested=parse_qs(urlsplit(endpoint).query)['name'][0]
    # Do not echo the local path's name: model server-side filename rewriting.
    name=re.sub(r'[^a-zA-Z0-9._-]','.',requested).strip('.')
    if any(a['name']==name for a in state['assets']):
        print('HTTP 422 already_exists',file=sys.stderr);sys.exit(1)
    result=dict(id=100+len(state['assets']),name=name,state='uploaded',size=len(body),digest='sha256:'+hashlib.sha256(body).hexdigest())
    state['assets'].append(result);state['uploads'].append(name)
elif '/releases/assets/' in endpoint:
    ident=int(endpoint.rsplit('/',1)[1]);asset=next(a for a in state['assets'] if a['id']==ident)
    if method=='PATCH':
        asset.update(json.loads(body));state['renames'].append(ident);result=asset
    elif method=='DELETE':state['assets'].remove(asset)
    else:raise AssertionError(endpoint)
elif '/releases/42/assets?' in endpoint:
    page=int(parse_qs(urlsplit(endpoint).query)['page'][0]);result=state['assets'][(page-1)*100:page*100]
elif '/releases?' in endpoint:result=[state['release']]
elif endpoint.endswith('/releases/42'):
    if method=='PATCH':state['release'].update(json.loads(body))
    result=state['release']
else:raise AssertionError((method,endpoint))
statefile.write_text(json.dumps(state))
if result is not None:print(json.dumps(result))
'''


class ReleaseTransportTests(unittest.TestCase):
    def test_resume_normalized_filename_and_second_run_skip_with_real_processes(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);folder=root/'ready';folder.mkdir()
            version='1.3.0-beta.18'
            fixtures.ReleaseDeliveryTests().prepared(folder,version)
            original='gcc-12_12.3.0-1ubuntu1~22.04.3.debian.tar.xz'
            raw=b'\x00\xffbinary source\r\n'
            (folder/original).write_bytes(raw)
            statefile=root/'state.json';fake=root/'api.py';fake.write_text(EMULATOR)
            old=dict(id=7,name=original.replace('~','.'),state='uploaded',size=len(raw),digest='sha256:'+hashlib.sha256(raw).hexdigest())
            state={'release':dict(id=42,tag_name='v'+version,draft=True,prerelease=True,target_commitish='a'*40,
                    upload_url=f'https://uploads.github.com/repos/{delivery.REPO}/releases/42/assets{{?name,label}}'),
                    'assets':[old],'uploads':[],'renames':[]}
            statefile.write_text(json.dumps(state))
            real_run=subprocess.run
            def gh(args,**kwargs):
                self.assertEqual(args[0],'gh')
                return real_run([sys.executable,str(fake),*args[1:]],**kwargs)
            argv=['release_delivery.py','draft','--version',version,'--directory',str(folder),'--commit','a'*40]
            with patch.dict(os.environ,{'ASTER_FAKE_RELEASE_STATE':str(statefile)}),patch.object(delivery.subprocess,'run',side_effect=gh),patch.object(sys,'argv',argv):
                delivery.main()
                first=json.loads(statefile.read_text())
                delivery.main()
                second=json.loads(statefile.read_text())
            self.assertEqual(first['uploads'],second['uploads'])
            self.assertEqual(second['renames'],[7])
            remote={a['name']:a for a in second['assets']}
            self.assertIn(original.replace('~','_'),remote)
            self.assertNotIn(original.replace('~','_'),second['uploads'])
            for line in (folder/'SHA256SUMS.txt').read_text().splitlines():
                sha,name=line.split('  ',1)
                self.assertEqual(remote[name]['digest'],'sha256:'+sha)
            self.assertEqual((folder/original).read_bytes(),raw)
            for entry in json.loads((folder/'AstroBatch-asset-filenames.json').read_text())['assets']:
                self.assertEqual(remote[entry['asset_name']]['digest'],'sha256:'+entry['sha256'])
