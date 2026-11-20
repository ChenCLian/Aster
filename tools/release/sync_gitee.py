"""Mirror the four tested program archives and record their verified metadata in the release description."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import secrets
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from download_metadata import digest, generate, inspect_package, valid_version
from release_asset_names import dependency_tag
from document_links import mirror_release_body
from release_delivery import package_names, mirror_plan, REPO




class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def allowed_public_url(url, redirected=False):
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
            response=opener.open(urllib.request.Request(url,headers={'User-Agent':'AstroBatch-release-mirror','Accept-Encoding':'identity'}),timeout=60)
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
    def api(self,path='',method='GET',data=None,file=None,missing=False,anonymous=False):
        headers={'User-Agent':'AstroBatch-release-mirror'}
        if not anonymous:
            headers['Authorization']='Bearer '+self.token
        payload=None
        if file is not None:
            boundary = 'AstroBatch-' + secrets.token_hex(24)
            opening = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
                       f'filename="{file.name}"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
            closing = f'\r\n--{boundary}--\r\n'.encode()
            def multipart():
                yield opening
                with file.open('rb') as stream:
                    while chunk := stream.read(1024*1024):
                        yield chunk
                yield closing
            payload = multipart()
            headers['Content-Length'] = str(len(opening) + file.stat().st_size + len(closing))
            headers['Content-Type']='multipart/form-data; boundary='+boundary
        elif data is not None:
            payload=json.dumps(data).encode();headers['Content-Type']='application/json'
        request=urllib.request.Request('https://gitee.com/api/v5/repos/'+REPO+('/'+path if path else ''),headers=headers,data=payload,method=method)
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


def mirror_packages(api,rid,folder,metadata,version,verify=public_download):
    plan=mirror_plan(folder,version);assets=api.assets(rid)
    byname={a['name']:a for a in assets}
    if len(byname)!=len(assets):raise ValueError('Duplicate Gitee attachment names; resolve them before retrying')
    selected={p['file']:p for p in plan['upload']}
    expected = package_names(version)
    if set(byname) - expected:
        raise ValueError('Unexpected existing Gitee attachments: '+', '.join(sorted(set(byname) - expected)))
    if set(selected) != expected:
        raise ValueError('All four original packages are required for a Gitee mirror')
    output=json.loads(json.dumps(metadata))
    for item in output['packages']:
        # Never carry an earlier unverified Gitee declaration into this run.
        item['sources']=[s for s in item['sources'] if s['name']=='GitHub']
        original=folder/item['file']
        if digest(original)!=item['sha256'] or original.stat().st_size!=item['bytes']:
            raise ValueError('GitHub archive disagrees with metadata')
        print('Mirror package: '+original.name,flush=True)
        if original.name not in byname:api.upload(rid,original)
        # Deterministic public URL, not an arbitrary API-supplied download link.
        url=f'https://gitee.com/{REPO}/releases/download/v{version}/{original.name}'
        with tempfile.TemporaryDirectory(prefix='astrobatch-verify-') as temp:
            verify(url,Path(temp)/'package',item['bytes'],item['sha256'],item['bytes'])
        item['sources'].append(dict(name='Gitee',url=url))
    return output,plan


UPDATE_MARKER = '<!-- astrobatch-update-v1\n'


def with_update_record(body, metadata):
    """Replace only our own metadata block, preserving the maintainer's notes."""
    body = body.replace('\r\n','\n')
    if UPDATE_MARKER in body:
        before, block = body.split(UPDATE_MARKER, 1)
        _, end, after = block.partition('\n-->')
        if not end or UPDATE_MARKER in after:
            raise ValueError('Malformed existing Gitee update record')
        body = before.rstrip() + ('\n\n' + after.strip() if after.strip() else '')
    return body.rstrip() + '\n\n' + UPDATE_MARKER + json.dumps(metadata, separators=(',', ':')) + '\n-->'


def execute(args):
    version=args.tag.removeprefix('v')
    if args.tag!='v'+version or not valid_version(version):raise ValueError('Alpha/development releases are never mirrored')
    token=os.environ.get('GITEE_TOKEN','');api=Gitee(token)
    gh=lambda *a:subprocess.run(['gh',*a],check=True,stdout=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',timeout=600).stdout
    release=json.loads(gh('release','view',args.tag,'--repo',REPO,'--json','isDraft,tagName,assets,body'))
    if release['isDraft'] or release['tagName'] != args.tag:raise ValueError('Publish/review the GitHub release before mirroring')
    names = package_names(version)
    actual = {a['name']:a for a in release['assets']}
    split = dependency_tag(version) != args.tag
    expected_inputs = names | ({f'AstroBatch-{version}-source.zip'} if split else {'AstroBatch-update.json','SHA256SUMS.txt'})
    if not expected_inputs <= actual.keys():
        raise ValueError('GitHub release is missing required assets: '+', '.join(sorted(expected_inputs-actual.keys())))
    with tempfile.TemporaryDirectory(prefix='astrobatch-gitee-') as temp:
        work=Path(temp);packages=work/'packages';packages.mkdir()
        # Never download dependency-materials, even for a dry check.
        for name in sorted(names | ({'AstroBatch-update.json'} & actual.keys())):
            if actual[name]['size'] > 2 * 1024**3:
                raise ValueError('Program exceeds the supported update package size: '+name)
            if name=='AstroBatch-update.json' and actual[name]['size']>1024*1024:raise ValueError('Metadata exceeds size limit')
            print('Fetching GitHub asset: '+name,flush=True)
            gh('release','download',args.tag,'--repo',REPO,'--pattern',name,'--dir',str(packages))
        path = packages/'AstroBatch-update.json'
        if path.exists():
            metadata = json.loads(path.read_text(encoding='utf-8'))
        else:
            # A failed --clobber can remove the old asset before upload completes.
            # Recover from the four original archives, checking GitHub's digests.
            for name in names:
                if actual[name].get('digest') != 'sha256:' + digest(packages/name):
                    raise ValueError('Cannot reconstruct update metadata without verified program bytes: '+name)
            metadata = generate([packages/name for name in sorted(names)],version)
        if metadata.get('schema')!=1 or metadata.get('version')!=version:raise ValueError('Invalid GitHub metadata version')
        if {p['file'] for p in metadata['packages']}!=package_names(version) or len(metadata['packages'])!=4:raise ValueError('Expected exactly four platform entries')
        for item in metadata['packages']:
            path=packages/item['file']
            inspected=inspect_package(path,version,item['platform'])
            if any(item[k]!=inspected[k] for k in inspected):raise ValueError('Archive differs from GitHub metadata')
            github_url = f'https://github.com/{REPO}/releases/download/{args.tag}/{path.name}'
            if [s for s in item['sources'] if s['name']=='GitHub'] != [dict(name='GitHub',url=github_url)]:
                raise ValueError('Unexpected primary package URL')
        tag_endpoint = 'releases/tags/'+args.tag
        remote=api.api(tag_endpoint,missing=True)
        if remote is None:
            # The Gitee release needs a tag target. It identifies the mirror page;
            # the GitHub release tag is authoritative for the program source.
            branch = api.api()['default_branch']
            notes = mirror_release_body(release['body'], args.tag)
            remote=api.api('releases','POST',dict(tag_name=args.tag,target_commitish=branch,name='AstroBatch '+version,body=notes,prerelease='-beta.' in version))
        if remote.get('tag_name') != args.tag:
            raise ValueError('Gitee release tag mismatch')
        rid = remote['id']
        result,plan=mirror_packages(api,rid,packages,metadata,version,verify=public_download)
        assets = api.assets(rid)
        if len(assets) != 4 or {a['name'] for a in assets} != package_names(version):
            raise ValueError('Gitee attachment list does not contain exactly four program archives')
        current = api.api(tag_endpoint)
        if current['id'] != rid or current['tag_name'] != args.tag:
            raise ValueError('Gitee release changed during mirror')
        body = with_update_record(current['body'], result)
        if current['body'] != body:
            api.api(f'releases/{rid}', 'PATCH', dict(tag_name=args.tag, name=current['name'],
                     body=body, prerelease='-beta.' in version))
        public = api.api(tag_endpoint, anonymous=True)
        if public['id'] != rid or public.get('tag_name') != args.tag or public.get('body','').replace('\r\n','\n') != body.replace('\r\n','\n'):
            raise ValueError('Public Gitee release description verification failed')
        # Retain the original endpoint for installed clients that read GitHub metadata.
        path=packages/'AstroBatch-update.json'
        if not path.is_file() or metadata != result:
            path.write_text(json.dumps(result,indent=2)+'\n', encoding='utf-8')
            gh('release','upload',args.tag,str(path),'--repo',REPO,'--clobber')
        # Independent recovery if a previous job stopped after updating the JSON.
        sums=work/'SHA256SUMS.txt'
        original = None
        if 'SHA256SUMS.txt' in actual:
            gh('release','download',args.tag,'--repo',REPO,'--pattern','SHA256SUMS.txt','--dir',str(work))
            original=sums.read_text(encoding='utf-8')
            lines=[line for line in original.splitlines() if line.split()[-1:]!=['AstroBatch-update.json']]
        else:
            lines = []
            for name in sorted(expected_inputs):
                fingerprint = actual[name].get('digest') or ''
                if not re.fullmatch('sha256:[0-9a-f]{64}', fingerprint):
                    raise ValueError('Cannot reconstruct checksums without a published SHA-256: '+name)
                lines.append(fingerprint.removeprefix('sha256:')+'  '+name)
        updated='\n'.join(lines+[digest(path)+'  AstroBatch-update.json'])+'\n'
        if updated != original:
            sums.write_text(updated, encoding='utf-8')
            gh('release','upload',args.tag,str(sums),'--repo',REPO,'--clobber')
        plan['status']='verified';args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(plan,ensure_ascii=False,indent=2)+'\n')
    print('All four Gitee program packages and the public update record verified.')


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
