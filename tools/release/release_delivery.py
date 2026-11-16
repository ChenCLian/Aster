"""Prepare a reviewable GitHub draft and an explicitly limited Gitee upload plan."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
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


def github_json(arguments):
    result=subprocess.run(['gh','api',*arguments],check=True,stdout=subprocess.PIPE,text=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def find_release(tag):
    for page in range(1,101):
        releases=github_json([f'repos/{REPO}/releases?per_page=100&page={page}'])
        existing=next((r for r in releases if r['tag_name']==tag),None)
        if existing:return existing
        if len(releases)<100:return None
    raise ValueError('Release listing is incomplete; refusing to create a duplicate draft')


def require_draft(release_id,tag):
    release=github_json([f'repos/{REPO}/releases/{release_id}'])
    if not release.get('draft') or release.get('tag_name')!=tag:
        raise ValueError('Release is no longer the selected draft; stop uploading and inspect it')
    return release


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
    existing=find_release(tag)
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
        args=['gh','release','create',tag,'--repo',REPO,'--draft','--target',commit,'--title','Aster '+version,'--notes-file',str(folder/'RELEASE_GITHUB.md')]
        if '-beta.' in version:args.append('--prerelease')
        subprocess.run(args,check=True)
        existing=find_release(tag)
        if not existing:raise ValueError('Created draft could not be verified')
    release_id=existing['id']
    require_draft(release_id,tag)
    args=['gh','release','edit',tag,'--repo',REPO,'--prerelease='+str('-beta.' in version).lower()]
    if ref.returncode!=0:args+=['--target',commit]
    subprocess.run(args,check=True)
    # Do not alter an existing draft's notes/title. Refresh checksums on retry,
    # excluding the previous checksum file itself.
    sums=''.join(digest(p)+'  '+p.name+'\n' for p in files)
    (folder/'SHA256SUMS.txt').write_text(sums,encoding='utf-8')
    for p in files+[folder/'SHA256SUMS.txt']:
        current=require_draft(release_id,tag)
        if current.get('prerelease')!=('-beta.' in version):raise ValueError('Draft channel flag changed; stop uploading')
        subprocess.run(['gh','release','upload',tag,str(p),'--repo',REPO,'--clobber'],check=True)
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
