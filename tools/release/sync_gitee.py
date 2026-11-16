"""Mirror published program archives and a clean source snapshot to Gitee.

Only four exact program filenames are downloaded from GitHub. Dependency
materials never enter staging, source snapshots, upload loops or metadata.
Requires GH_TOKEN and GITEE_TOKEN in the environment; never put tokens in URLs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from download_metadata import digest, inspect_package, PLATFORMS, valid_version
from document_links import mirror_release_body
from release_delivery import package_names, mirror_plan, REPO
import package_source

ROOT = Path(__file__).resolve().parents[2]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def allowed_public_url(url, redirected=False):
    import re
    u=urllib.parse.urlsplit(url)
    if u.scheme!='https' or u.username or u.password or u.port not in (None,443) or u.fragment:return False
    if u.hostname=='github.com':return u.path.startswith('/'+REPO+'/releases/download/')
    if u.hostname=='gitee.com':
        return u.path.startswith('/'+REPO+'/releases/download/') or (redirected and bool(re.fullmatch('/'+re.escape(REPO)+r'/attach_files/[0-9]+/download(?:/[^/]+)?',u.path)))
    return redirected and (u.hostname in ('release-assets.githubusercontent.com','objects.githubusercontent.com') or (u.hostname=='foruda.gitee.com' and u.path.startswith('/attach_file/')))


def public_download(url, path, limit, expected_hash=None, expected_size=None):
    """Anonymous HTTPS only. No API credentials or cookies forwarded to redirects."""
    opener=urllib.request.build_opener(NoRedirect)
    for hop in range(6):
        if not allowed_public_url(url,hop>0): raise ValueError('Unapproved public download redirect')
        try:
            response=opener.open(urllib.request.Request(url,headers={'User-Agent':'Aster-release-mirror','Accept-Encoding':'identity'}),timeout=60)
        except urllib.error.HTTPError as error:
            if error.code in (301,302,303,307,308) and error.headers.get('Location'):
                url=urllib.parse.urljoin(url,error.headers['Location']); continue
            raise ValueError('Public package verification failed: HTTP '+str(error.code)) from None
        count=0; sha=hashlib.sha256()
        with response, path.open('wb') as stream:
            while chunk:=response.read(1024*1024):
                count+=len(chunk)
                if count>limit:raise ValueError('Public download exceeds size limit')
                stream.write(chunk);sha.update(chunk)
        if expected_size is not None and count!=expected_size:raise ValueError('Mirror size mismatch')
        if expected_hash is not None and sha.hexdigest()!=expected_hash:raise ValueError('Mirror SHA-256 mismatch')
        return
    raise ValueError('Too many public download redirects')


class Gitee:
    def __init__(self,token):
        if not token:raise ValueError('Configure the GITEE_TOKEN Actions secret')
        self.token=token;self.opener=urllib.request.build_opener(NoRedirect)
    def api(self,path,method='GET',data=None,file=None,missing=False):
        headers={'Authorization':'Bearer '+self.token,'User-Agent':'Aster-release-mirror'}
        payload=None
        if file is not None:
            boundary='AsterUploadBoundary0d9f88df'
            payload=(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{file.name}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()+file.read_bytes()+f'\r\n--{boundary}--\r\n'.encode())
            headers['Content-Type']='multipart/form-data; boundary='+boundary
        elif data is not None:
            payload=json.dumps(data).encode();headers['Content-Type']='application/json'
        request=urllib.request.Request('https://gitee.com/api/v5/repos/'+REPO+'/'+path,headers=headers,data=payload,method=method)
        try:
            with self.opener.open(request,timeout=180) as response:raw=response.read(8*1024*1024+1)
        except urllib.error.HTTPError as error:
            if missing and error.code==404:return None
            raise ValueError('Gitee API '+method+' failed (HTTP '+str(error.code)+'); check token permissions, quota and repository access') from None
        if len(raw)>8*1024*1024:raise ValueError('Gitee response too large')
        return json.loads(raw) if raw.strip() else None
    def assets(self,rid):
        result=[]
        for page in range(1,101):
            items=self.api(f'releases/{rid}/attach_files?per_page=100&page={page}')
            if not isinstance(items,list):raise ValueError('Unexpected Gitee attachment list')
            result+=items
            if len(items)<100:return result
        raise ValueError('Gitee attachment pagination limit')
    def upload(self,rid,path):return self.api(f'releases/{rid}/attach_files','POST',file=path)


def rank(version):
    if not valid_version(version):raise ValueError('Only stable/beta versions can be mirrored')
    core,*beta=version.split('-beta.')
    return (*map(int,core.split('.')),0 if beta else 1,int(beta[0]) if beta else 0)


def snapshot_source(version,token,parent):
    """Fast-forward only dedicated mirror branches; preserve main and remote tags."""
    branch='release-beta' if '-beta.' in version else 'release-stable'
    root=parent/'source';root.mkdir()
    ask=parent/'askpass.py'
    ask.write_text('#!/usr/bin/env python3\nimport os,sys\nprint("oauth2" if "username" in sys.argv[1].lower() else os.environ["GITEE_TOKEN"])\n')
    ask.chmod(0o700)
    env={**os.environ,'GIT_ASKPASS':str(ask),'GIT_TERMINAL_PROMPT':'0','GITEE_TOKEN':token}
    def git(*args,check=True):
        try:
            return subprocess.run(['git','-C',str(root),*args],env=env,check=check,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',timeout=300)
        except subprocess.TimeoutExpired:
            raise ValueError('Gitee source '+args[0]+' timed out after 300 seconds; retry the mirror job') from None
        except subprocess.CalledProcessError as error:
            detail=(error.stderr or '').replace(token,'[redacted]')[-2000:]
            raise ValueError('Gitee source '+args[0]+' failed: '+detail) from None
    git('init','-q');git('config','user.name','Aster release mirror');git('config','user.email','release-mirror@users.noreply.github.com')
    git('remote','add','origin','https://gitee.com/'+REPO+'.git')
    refs=git('ls-remote','--heads','origin',branch).stdout
    if refs.strip():
        git('fetch','--depth=1','origin',branch);git('checkout','-b',branch,'FETCH_HEAD')
        previous=(root/'VERSION').read_text().strip()
        if rank(previous)>rank(version):raise ValueError('Refusing to move Gitee channel branch backwards')
        git('rm','-r','-f','--ignore-unmatch','.')
    else:git('checkout','--orphan',branch)
    for file in ROOT.rglob('*'):
        if file.is_file() and package_source.included(file):
            rel=file.relative_to(ROOT)
            if any('dependency-material' in p.lower() or p.lower() in ('dependency-cache','release-assets') for p in rel.parts):continue
            dest=root/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)
    git('add','-A')
    if git('diff','--cached','--quiet',check=False).returncode or not refs.strip():
        git('commit','-m','Aster '+version+' source snapshot')
    tag='v'+version
    tagged=git('ls-remote','--tags','origin','refs/tags/'+tag).stdout.strip()
    if tagged:
        git('fetch','--depth=1','origin','refs/tags/'+tag)
        if git('rev-parse','FETCH_HEAD^{tree}').stdout!=git('rev-parse','HEAD^{tree}').stdout:
            raise ValueError('Existing Gitee tag has different source content; refusing replacement')
    else:git('tag',tag)
    git('push','origin','HEAD:refs/heads/'+branch)
    if not tagged:git('push','origin','refs/tags/'+tag)
    return branch


def mirror_packages(api,rid,folder,metadata,version,verify=public_download):
    plan=mirror_plan(folder,version);assets=api.assets(rid)
    byname={a['name']:a for a in assets}
    if len(byname)!=len(assets):raise ValueError('Duplicate Gitee attachment names; resolve them before retrying')
    selected={p['file']:p for p in plan['upload']}
    output=json.loads(json.dumps(metadata))
    for item in output['packages']:
        # Never carry an earlier unverified Gitee declaration into this run.
        item['sources']=[s for s in item['sources'] if s['name']=='GitHub']
        if item['file'] not in selected:continue
        original=folder/item['file']
        if digest(original)!=item['sha256'] or original.stat().st_size!=item['bytes']:
            raise ValueError('GitHub archive disagrees with metadata')
        print('Mirror package: '+original.name,flush=True)
        if original.name not in byname:api.upload(rid,original)
        # Deterministic public URL, not an arbitrary API-supplied download link.
        url=f'https://gitee.com/{REPO}/releases/download/v{version}/{original.name}'
        with tempfile.TemporaryDirectory(prefix='aster-verify-') as temp:
            verify(url,Path(temp)/'package',item['bytes'],item['sha256'],item['bytes'])
        item['sources'].append(dict(name='Gitee',url=url))
    return output,plan


def publish_metadata(api,rid,path,verify=public_download):
    existing=next((a for a in api.assets(rid) if a['name']==path.name),None)
    if existing:
        # Immutable published metadata: retries succeed only with identical bytes.
        # If a partial previous mirror needs extension, remove ONLY this metadata
        # attachment in the maintainer UI and rerun. Program archives stay intact.
        version=json.loads(path.read_text())['version']
        with tempfile.TemporaryDirectory() as temp:
            verify(f'https://gitee.com/{REPO}/releases/download/v{version}/{path.name}',Path(temp)/'metadata',1024*1024,digest(path),path.stat().st_size)
    else:api.upload(rid,path)


def execute(args):
    version=args.tag.removeprefix('v')
    if args.tag!='v'+version or not valid_version(version):raise ValueError('Alpha/development releases are never mirrored')
    if (ROOT/'VERSION').read_text().strip()!=version:raise ValueError('Checkout must match the selected release version')
    token=os.environ.get('GITEE_TOKEN','');api=Gitee(token)
    gh=lambda *a:subprocess.run(['gh',*a],check=True,stdout=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',timeout=600).stdout
    release=json.loads(gh('release','view',args.tag,'--repo',REPO,'--json','isDraft,tagName,assets,body'))
    if release['isDraft']:raise ValueError('Publish/review the GitHub release before mirroring')
    required=package_names(version)|{'Aster-update.json'}
    actual={a['name']:a for a in release['assets']}
    if not required<=actual.keys():raise ValueError('GitHub release is missing program packages or metadata')
    with tempfile.TemporaryDirectory(prefix='aster-gitee-') as temp:
        work=Path(temp);packages=work/'packages';packages.mkdir()
        # Never download dependency-materials, even for a dry check.
        for name in sorted(required):
            if name!='Aster-update.json' and actual[name]['size']>100_000_000:continue
            if name=='Aster-update.json' and actual[name]['size']>1024*1024:raise ValueError('Metadata exceeds size limit')
            print('Fetching GitHub asset: '+name,flush=True)
            gh('release','download',args.tag,'--repo',REPO,'--pattern',name,'--dir',str(packages))
        metadata=json.loads((packages/'Aster-update.json').read_text())
        if metadata.get('schema')!=1 or metadata.get('version')!=version:raise ValueError('Invalid GitHub metadata version')
        if {p['file'] for p in metadata['packages']}!=package_names(version) or len(metadata['packages'])!=4:raise ValueError('Expected exactly four platform entries')
        for item in metadata['packages']:
            path=packages/item['file']
            if path.exists():
                inspected=inspect_package(path,version,item['platform'])
                if any(item[k]!=inspected[k] for k in inspected):raise ValueError('Archive differs from GitHub metadata')
        print('Syncing source snapshot',flush=True)
        branch=snapshot_source(version,token,work)
        remote=api.api('releases/tags/'+args.tag,missing=True)
        if remote is None:
            notes=mirror_release_body(release['body'],args.tag)
            notes+='\n\n依赖源码与许可材料附件仅保存在 [GitHub 对应发行版](https://github.com/'+REPO+'/releases/tag/'+args.tag+')；程序包内的许可文件保留。\n'
            remote=api.api('releases','POST',dict(tag_name=args.tag,target_commitish=branch,name='Aster '+version,body=notes,prerelease='-beta.' in version))
        result,plan=mirror_packages(api,remote['id'],packages,metadata,version)
        plan['oversized_not_downloaded']=[name for name in sorted(required) if name!='Aster-update.json' and actual[name]['size']>100_000_000]
        path=packages/'Aster-update.json';path.write_text(json.dumps(result,indent=2)+'\n')
        publish_metadata(api,remote['id'],path)
        public_download(f'https://gitee.com/{REPO}/releases/download/{args.tag}/Aster-update.json',work/'verified.json',1024*1024,digest(path),path.stat().st_size)
        # Only now expose mirror URLs on GitHub. Original program bytes untouched.
        gh('release','upload',args.tag,str(path),'--repo',REPO,'--clobber')
        # Download checksums include metadata; refresh its line to avoid a stale sum.
        if 'SHA256SUMS.txt' in actual:
            gh('release','download',args.tag,'--repo',REPO,'--pattern','SHA256SUMS.txt','--dir',str(work))
            sums=work/'SHA256SUMS.txt'
            lines=[line for line in sums.read_text().splitlines() if line.split()[-1:]!=['Aster-update.json']]
            sums.write_text('\n'.join(lines+[digest(path)+'  Aster-update.json'])+'\n')
            gh('release','upload',args.tag,str(sums),'--repo',REPO,'--clobber')
        plan['status']='verified';args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n')
    print('Gitee program/source mirror verified; dependency materials remain on GitHub only.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag',required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    try:
        execute(args)
    except Exception as error:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(dict(status='failed',tag=args.tag,error=str(error),dependency_materials='never downloaded or mirrored'),ensure_ascii=False,indent=2)+'\n')
        raise SystemExit('Gitee mirror failed; see the report and job log. '+str(error)) from None


if __name__=='__main__':
    main()
