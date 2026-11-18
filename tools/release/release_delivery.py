"""Prepare a reviewable GitHub draft and an explicitly limited Gitee upload plan."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from urllib.parse import quote
import zipfile
from download_metadata import PLATFORMS, generate, valid_version, digest
from document_links import release_body

ROOT = Path(__file__).resolve().parents[2]
GITEE_FILE_LIMIT = 100_000_000  # conservative interpretation of the advertised 100M
REPO = 'ChenCLian/Aster'


def package_names(version):
    if not valid_version(version):
        raise ValueError('Only stable and beta releases can be delivered; alpha is development-only')
    return {f'Aster-{version}-{p}' + ('.tar.gz' if p == 'linux-x64' else '.zip') for p in PLATFORMS}


def mirror_plan(folder, version, limit=GITEE_FILE_LIMIT):
    allowed = package_names(version)
    chosen, excluded = [], []
    for path in sorted(folder.rglob('*')):
        if not path.is_file():
            continue
        rel = path.relative_to(folder)
        # Positive allow-list, plus a path veto. Materials never enter the upload
        # loop even if someone gives a nested material an executable-package name.
        if any('dependency-material' in part.lower() or part.lower() in ('release-assets','dependency-cache') for part in rel.parts[:-1]):
            excluded.append(dict(file=str(rel), reason='dependency-materials-never-mirrored'))
        elif path.name not in allowed or len(rel.parts) != 1:
            excluded.append(dict(file=str(rel), reason='not-a-program-package'))
        elif path.is_symlink() or path.stat().st_size > limit:
            excluded.append(dict(file=str(rel), reason='size-limit-or-symlink'))
        else:
            chosen.append(dict(file=path.name, bytes=path.stat().st_size, sha256=digest(path)))
    return dict(schema=1, version=version, upload=chosen, excluded=excluded,
                dependency_materials='GitHub only; never copied to Gitee',
                note='Oversized programs are skipped, never split or re-compressed')


def prepare(artifacts, destination, version, source=ROOT):
    expected=package_names(version)
    found={}
    for path in artifacts.rglob('*'):
        if path.is_file() and path.name in expected and not any('dependency-material' in p.lower() for p in path.relative_to(artifacts).parts[:-1]):
            if path.name in found:
                raise ValueError('Duplicate program artifact: '+path.name)
            found[path.name]=path
    if set(found) != expected:
        raise ValueError('All four tested program packages are required; missing: '+', '.join(sorted(expected-set(found))))
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Release preparation output must be empty')
    destination.mkdir(parents=True,exist_ok=True)
    for name,path in found.items():shutil.copy2(path,destination/name)
    metadata=generate([destination/name for name in sorted(expected)],version)
    (destination/'Aster-update.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    # Dependency sources go to GitHub only. Identical shared archives are deduped;
    # equal names with different bytes are an error, not silent overwrites.
    for directory in artifacts.glob('dependency-materials-*'):
        for path in directory.rglob('*'):
            if not path.is_file():continue
            if path.name in expected or path.name in ('Aster-update.json','SHA256SUMS.txt'):
                raise ValueError('Dependency artifact collides with program metadata')
            target=destination/path.name
            if target.exists():
                if digest(target)!=digest(path):raise ValueError('Conflicting dependency asset: '+path.name)
            else:shutil.copy2(path,target)
    tag='v'+version
    notes=(source/'RELEASE_NOTES.md').read_text(encoding='utf-8')
    (destination/'RELEASE_GITHUB.md').write_text(release_body(notes,host='github.com',ref=tag),encoding='utf-8')
    (destination/'RELEASE_GITEE.md').write_text(release_body(notes,host='gitee.com',ref=tag)+
        '\n\n依赖源码与许可材料附件仅保存在 [GitHub 对应发行版](https://github.com/'+REPO+'/releases/tag/'+tag+')，不在 Gitee 重复上传。程序包中的许可文件保留。\n',encoding='utf-8')
    return metadata


def github_json(arguments, payload=None, timeout=120):
    args=['gh','api',*arguments,'-H','Cache-Control: no-cache']
    if payload is not None:args+=['--input','-']
    result=subprocess.run(args, input=json.dumps(payload,ensure_ascii=False) if payload is not None else None,
                          check=True,stdout=subprocess.PIPE,text=True,encoding='utf-8',timeout=timeout)
    return json.loads(result.stdout) if result.stdout.strip() else None


def find_release(tag, commit=None):
    matches=[]
    for page in range(1,101):
        releases=github_json([f'repos/{REPO}/releases?per_page=100&page={page}'])
        for release in releases:
            if release['tag_name']==tag:
                matches.append(release)
            elif (release.get('draft') and release['tag_name'].startswith('untagged-')
                  and release.get('name')=='Aster '+tag.removeprefix('v')):
                # Recover only an unambiguously matching draft left by older tooling.
                if not commit or release.get('target_commitish')!=commit:
                    raise ValueError('An untagged draft has this version title but a different target; '
                                     'inspect its tag and target before retrying')
                matches.append(release)
        if len(releases)<100:
            if len(matches)>1:raise ValueError('Multiple releases match this version; inspect drafts before retrying')
            return matches[0] if matches else None
    raise ValueError('Release listing is incomplete; refusing to create a duplicate draft')


def require_draft(release_id,tag):
    release=github_json([f'repos/{REPO}/releases/{release_id}'])
    if not release.get('draft') or release.get('tag_name')!=tag:
        raise ValueError('Release is no longer the selected draft; stop uploading and inspect it')
    return release


def upload_asset(release_id,tag,path,beta):
    current=require_draft(release_id,tag)
    if current.get('prerelease')!=beta:raise ValueError('Draft channel flag changed; stop uploading')
    # Use the stable release ID throughout; a draft's browser URL may be untagged.
    endpoint=f'repos/{REPO}/releases/{release_id}/assets'
    assets=[]
    for page in range(1,101):
        batch=github_json([endpoint+f'?per_page=100&page={page}'])
        assets.extend(a for a in batch if a['name']==path.name)
        if len(batch)<100:break
    else:raise ValueError('Asset listing is incomplete')
    sha='sha256:'+digest(path)
    if len(assets)==1 and assets[0].get('state')=='uploaded' and assets[0].get('digest')==sha and assets[0].get('size')==path.stat().st_size:
        return
    for asset in assets:
        require_draft(release_id,tag)
        github_json([f'repos/{REPO}/releases/assets/{asset["id"]}','--method','DELETE'])
    current=require_draft(release_id,tag)
    if current.get('prerelease')!=beta:raise ValueError('Draft channel flag changed; stop uploading')
    upload_url=current.get('upload_url','').split('{',1)[0]
    if upload_url!=f'https://uploads.github.com/repos/{REPO}/releases/{release_id}/assets':
        raise ValueError('Unexpected release upload endpoint')
    print('Uploading '+path.name,flush=True)
    uploaded=github_json([upload_url+'?name='+quote(path.name,safe=''),'--method','POST',
                          '-H','Content-Type: application/octet-stream','--input',str(path)],timeout=1800)
    if (uploaded.get('name')!=path.name or uploaded.get('state')!='uploaded'
            or uploaded.get('size')!=path.stat().st_size
            or (uploaded.get('digest') and uploaded['digest']!=sha)):
        raise ValueError('Uploaded asset verification failed: '+path.name)


def prepared_files(folder,version):
    required=package_names(version)|{'Aster-update.json','RELEASE_GITHUB.md',f'Aster-{version}-source.zip'}
    if any(not (folder/name).is_file() or (folder/name).is_symlink() for name in required):
        raise ValueError('Incomplete release preparation: require four packages, metadata, notes and source ZIP')
    expected=generate([folder/name for name in sorted(package_names(version))],version)
    if json.loads((folder/'Aster-update.json').read_text(encoding='utf-8'))!=expected:
        raise ValueError('Prepared metadata differs from the actual program packages')
    if not zipfile.is_zipfile(folder/f'Aster-{version}-source.zip'):
        raise ValueError('Invalid source ZIP')
    files=[p for p in folder.iterdir() if p.is_file() and p.name not in ('RELEASE_GITHUB.md','RELEASE_GITEE.md','mirror-plan.json','SHA256SUMS.txt')]
    if any(p.is_symlink() for p in files):raise ValueError('Release assets must be regular files')
    return sorted(files)


def publish_draft(folder, version, commit):
    # Every mutation is preceded by a fresh draft check. GitHub does not offer
    # atomic draft+asset transactions; publish only after this job completes.
    tag='v'+version
    package_names(version)
    import re
    if not re.fullmatch('[a-fA-F0-9]{40}',commit):raise ValueError('Full source commit required')
    existing=find_release(tag,commit)
    if existing and not existing['draft']:raise ValueError('Refusing to overwrite a published release')
    files=prepared_files(folder,version)
    ref=subprocess.run(['gh','api',f'repos/{REPO}/git/ref/tags/{tag}'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    if ref.returncode==0:
        obj=json.loads(ref.stdout)['object']
        if obj['type']=='tag':obj=github_json([f'repos/{REPO}/git/tags/{obj["sha"]}'])['object']
        if obj.get('type')!='commit' or obj['sha']!=commit:raise ValueError('Existing tag does not identify this tested commit')
    elif '404' not in ref.stderr:
        raise ValueError('Could not verify the release tag')
    if existing is None:
        existing=github_json([f'repos/{REPO}/releases','--method','POST'],payload={
            'tag_name':tag,'target_commitish':commit,'draft':True,
            'prerelease':'-beta.' in version,'name':'Aster '+version,
            'body':(folder/'RELEASE_GITHUB.md').read_text(encoding='utf-8')})
        # Creation returns the identity directly. Do not rediscover a new draft
        # through a separately cached/eventually updated release listing.
        if not isinstance(existing,dict) or not isinstance(existing.get('id'),int):
            raise ValueError('Create response did not contain a release ID; inspect Releases before retrying')
        print('Created draft ID: '+str(existing['id']),flush=True)
    release_id=existing['id']
    require_draft(release_id,existing['tag_name'])
    payload={'tag_name':tag,'prerelease':'-beta.' in version}
    if ref.returncode!=0:payload['target_commitish']=commit
    github_json([f'repos/{REPO}/releases/{release_id}','--method','PATCH'],payload=payload)
    require_draft(release_id,tag)
    # Do not alter an existing draft's notes/title. Refresh checksums on retry,
    # excluding the previous checksum file itself.
    sums=''.join(digest(p)+'  '+p.name+'\n' for p in files)
    (folder/'SHA256SUMS.txt').write_text(sums,encoding='utf-8')
    for p in files+[folder/'SHA256SUMS.txt']:
        upload_asset(release_id,tag,p,'-beta.' in version)
    require_draft(release_id,tag)
    print('Draft ready for review: '+tag)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['prepare','mirror-plan','draft'])
    parser.add_argument('--version',default=(ROOT/'VERSION').read_text().strip())
    parser.add_argument('--artifacts',type=Path)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--commit')
    args=parser.parse_args()
    if args.command=='prepare':prepare(args.artifacts,args.directory,args.version)
    elif args.command=='draft':publish_draft(args.directory,args.version,args.commit)
    else:
        (args.directory/'mirror-plan.json').write_text(json.dumps(mirror_plan(args.directory,args.version),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
