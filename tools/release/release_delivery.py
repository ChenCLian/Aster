"""Prepare a reviewable GitHub draft and an explicitly limited Gitee upload plan."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from urllib.parse import quote
import zipfile
from download_metadata import PLATFORMS, generate, valid_version, digest
from document_links import release_body, mirror_release_body
from release_asset_names import asset_name, validate_names, legacy_asset_names, dependency_tag

ROOT = Path(__file__).resolve().parents[2]
REPO = 'ChenCLian/AstroBatch'


def package_names(version):
    if not valid_version(version):
        raise ValueError('Only stable and beta releases can be delivered; alpha is development-only')
    return {f'AstroBatch-{version}-{p}' + ('.tar.gz' if p == 'linux-x64' else '.zip') for p in PLATFORMS}


def mirror_plan(folder, version, limit=None):
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
        elif path.is_symlink() or (limit is not None and path.stat().st_size > limit):
            excluded.append(dict(file=str(rel), reason='size-limit-or-symlink'))
        else:
            chosen.append(dict(file=path.name, bytes=path.stat().st_size, sha256=digest(path)))
    return dict(schema=1, version=version, upload=chosen, excluded=excluded,
                dependency_materials='GitHub only; never copied to Gitee',
                note='Mirror all four original program archives; provider quota errors require a retry')


def download_section(version):
    base = f'https://github.com/{REPO}/releases'
    entries = []
    for platform in PLATFORMS:
        suffix = '.tar.gz' if platform == 'linux-x64' else '.zip'
        url = f'{base}/download/v{version}/AstroBatch-{version}-{platform}{suffix}'
        entries.append(f'[{platform}]({url})')
    return ('## 程序下载\n\n' + ' · '.join(entries) + '\n\n' +
            f'[依赖源码、许可证与构建记录]({base}/tag/{dependency_tag(version)})')


def check_material_receipts(artifacts, version):
    for platform in PLATFORMS:
        directory = artifacts / ('dependency-materials-' + platform)
        for suffix in ('dependency-lock.json', 'dependency-records.zip'):
            name = f'AstroBatch-{version}-{platform}-{suffix}'
            # Older wildcard uploads include <platform>/release-assets/ inside
            # the named artifact. New uploads contain the same files at its root.
            matches = sorted(directory.rglob(name))
            if len(matches) != 1 or not matches[0].is_file() or matches[0].is_symlink():
                found = ', '.join(str(p.relative_to(directory)) for p in matches) or 'none'
                raise ValueError(f'Expected one regular {name} in {directory.name}; found: {found}')


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
    tag = 'v' + version
    split = dependency_tag(version) != tag
    if split:
        check_material_receipts(artifacts, version)
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Release preparation output must be empty')
    destination.mkdir(parents=True,exist_ok=True)
    for name,path in found.items():shutil.copy2(path,destination/name)
    metadata=generate([destination/name for name in sorted(expected)],version)
    (destination/'AstroBatch-update.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    # Dependency sources go to GitHub only. Identical shared archives are deduped;
    # equal names with different bytes are an error, not silent overwrites.
    materials = destination / 'dependency-materials' if split else destination
    if split:
        materials.mkdir()
    for directory in artifacts.glob('dependency-materials-*'):
        for path in directory.rglob('*'):
            if not path.is_file():continue
            if path.name in expected or path.name in ('AstroBatch-update.json','SHA256SUMS.txt'):
                raise ValueError('Dependency artifact collides with program metadata')
            if split and (path.is_symlink() or asset_name(path.name) in
                          {'AstroBatch-asset-filenames.json','DEPENDENCY_DOWNLOADS.md','SHA256SUMS.txt'}):
                raise ValueError('Invalid or reserved dependency asset: '+path.name)
            target=materials/path.name
            if target.exists():
                if digest(target)!=digest(path):raise ValueError('Conflicting dependency asset: '+path.name)
            else:shutil.copy2(path,target)
    notes=(source/'RELEASE_NOTES.md').read_text(encoding='utf-8')
    body = release_body(notes,host='github.com',ref=tag).rstrip()
    if split:
        body += '\n\n' + download_section(version)
    (destination/'RELEASE_GITHUB.md').write_text(body+'\n',encoding='utf-8')
    (destination/'RELEASE_GITEE.md').write_text(mirror_release_body(body,tag)+'\n',encoding='utf-8')
    return metadata


def github_json(arguments, payload=None, timeout=120):
    args=['gh','api',*arguments,'-H','Cache-Control: no-cache']
    if payload is not None:args+=['--input','-']
    result=subprocess.run(args, input=json.dumps(payload,ensure_ascii=False) if payload is not None else None,
                          check=True,stdout=subprocess.PIPE,text=True,encoding='utf-8',timeout=timeout)
    return json.loads(result.stdout) if result.stdout.strip() else None


def find_release(tag, commit=None, title=None):
    matches=[]
    for page in range(1,101):
        releases=github_json([f'repos/{REPO}/releases?per_page=100&page={page}'])
        for release in releases:
            if release['tag_name']==tag:
                matches.append(release)
            elif (release.get('draft') and release['tag_name'].startswith('untagged-')
                  and release.get('name')==(title or 'AstroBatch '+tag.removeprefix('v'))):
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


def verify_uploaded(asset,path,remote_name,sha):
    expected={'name':remote_name,'state':'uploaded','size':path.stat().st_size}
    if asset.get('digest'):expected['digest']=sha
    mismatches={key:{'expected':value,'actual':asset.get(key)}
                for key,value in expected.items() if asset.get(key)!=value}
    if mismatches:
        raise ValueError('Uploaded asset verification failed: '+path.name+'; '+
                         json.dumps(mismatches,ensure_ascii=True,sort_keys=True))


def list_assets(release_id):
    endpoint=f'repos/{REPO}/releases/{release_id}/assets'
    rows=[]
    for page in range(1,101):
        batch=github_json([endpoint+f'?per_page=100&page={page}'])
        rows.extend(batch)
        if len(batch)<100:break
    else:raise ValueError('Asset listing is incomplete')
    return rows


def upload_asset(release_id,tag,path,beta,protected_names=()):
    current=require_draft(release_id,tag)
    if current.get('prerelease')!=beta:raise ValueError('Draft channel flag changed; stop uploading')
    remote_name=asset_name(path.name)
    sha='sha256:'+digest(path)
    rows=list_assets(release_id)
    assets=[a for a in rows if a['name']==remote_name]
    def same_content(a):
        return a.get('state')=='uploaded' and a.get('digest')==sha and a.get('size')==path.stat().st_size
    if len(assets)==1 and same_content(assets[0]):
        print('Verified existing '+remote_name,flush=True)
        return
    # Older upload code may have left the right bytes under GitHub's changed
    # name. Reuse only a unique, digest-verified match for a normalized filename.
    renamed=[a for a in rows if same_content(a) and a['name'] in legacy_asset_names(path.name)
             and a['name'] not in protected_names] if remote_name!=path.name and not assets else []
    if len(renamed)==1:
        current=require_draft(release_id,tag)
        if current.get('prerelease')!=beta:raise ValueError('Draft channel flag changed; stop uploading')
        updated=github_json([f'repos/{REPO}/releases/assets/{renamed[0]["id"]}','--method','PATCH'],
                            payload={'name':remote_name})
        verify_uploaded(updated,path,remote_name,sha)
        print('Reused existing bytes as '+remote_name,flush=True)
        return
    for asset in assets:
        current=require_draft(release_id,tag)
        if current.get('prerelease')!=beta:raise ValueError('Draft channel flag changed; stop uploading')
        github_json([f'repos/{REPO}/releases/assets/{asset["id"]}','--method','DELETE'])
    current=require_draft(release_id,tag)
    if current.get('prerelease')!=beta:raise ValueError('Draft channel flag changed; stop uploading')
    upload_url=current.get('upload_url','').split('{',1)[0]
    if upload_url!=f'https://uploads.github.com/repos/{REPO}/releases/{release_id}/assets':
        raise ValueError('Unexpected release upload endpoint')
    print('Uploading '+path.name+(' as '+remote_name if remote_name!=path.name else ''),flush=True)
    uploaded=github_json([upload_url+'?name='+quote(remote_name,safe=''),'--method','POST',
                          '-H','Content-Type: application/octet-stream','--input',str(path)],timeout=1800)
    verify_uploaded(uploaded,path,remote_name,sha)


def write_asset_index(folder,version,files):
    entries=[dict(source_name=p.name,asset_name=asset_name(p.name),bytes=p.stat().st_size,sha256=digest(p)) for p in files]
    mapping=folder/'AstroBatch-asset-filenames.json'
    mapping.write_text(json.dumps(dict(schema=1,version=version,assets=entries),indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    guide=folder/'DEPENDENCY_DOWNLOADS.md'
    lines=['# Release files and dependency source filenames','',
           'Download links use the published names. Source archives retain their original bytes.',
           'Before rebuilding Debian sources, restore each downloaded source archive to its original filename',
           'shown below, so that .dsc references resolve. Verify SHA256SUMS.txt before renaming.',
           'AstroBatch-asset-filenames.json records both names and SHA-256. Files inside program packages are unchanged.',
           'For older program packages whose dependency links contain special characters, use this index.',
           '', '| Original filename | Download |', '| --- | --- |']
    for entry in entries:
        original=entry['source_name'].replace('|','&#124;').replace('<','&lt;').replace('>','&gt;')
        name=entry['asset_name']
        lines.append(f'| {original} | [{name}](https://github.com/{REPO}/releases/download/{dependency_tag(version)}/{name}) |')
    guide.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return [mapping,guide]


def prepared_files(folder,version):
    required=package_names(version)|{'AstroBatch-update.json','RELEASE_GITHUB.md',f'AstroBatch-{version}-source.zip'}
    if any(not (folder/name).is_file() or (folder/name).is_symlink() for name in required):
        raise ValueError('Incomplete release preparation: require four packages, metadata, notes and source ZIP')
    expected=generate([folder/name for name in sorted(package_names(version))],version)
    if json.loads((folder/'AstroBatch-update.json').read_text(encoding='utf-8'))!=expected:
        raise ValueError('Prepared metadata differs from the actual program packages')
    if not zipfile.is_zipfile(folder/f'AstroBatch-{version}-source.zip'):
        raise ValueError('Invalid source ZIP')
    files=[p for p in folder.iterdir() if p.is_file() and p.name not in ('RELEASE_GITHUB.md','RELEASE_GITEE.md','mirror-plan.json','SHA256SUMS.txt','AstroBatch-asset-filenames.json','DEPENDENCY_DOWNLOADS.md')]
    if any(p.is_symlink() for p in files):raise ValueError('Release assets must be regular files')
    if dependency_tag(version) != 'v' + version:
        allowed = package_names(version) | {'AstroBatch-update.json', f'AstroBatch-{version}-source.zip'}
        if {p.name for p in files} != allowed:
            raise ValueError('Unexpected file in the program release directory')
    validate_names([p.name for p in files]+['SHA256SUMS.txt','AstroBatch-asset-filenames.json','DEPENDENCY_DOWNLOADS.md'])
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
    split = dependency_tag(version) != tag
    notes = (folder/'RELEASE_GITHUB.md').read_text(encoding='utf-8')
    footer = ''
    if split:
        directory = folder/'dependency-materials'
        if not directory.is_dir() or directory.is_symlink():
            raise ValueError('Missing separate dependency materials directory')
        material_files = [p for p in sorted(directory.iterdir())
                          if p.name not in ('SHA256SUMS.txt', 'AstroBatch-asset-filenames.json', 'DEPENDENCY_DOWNLOADS.md')]
        if any(not p.is_file() or p.is_symlink() for p in material_files):
            raise ValueError('Expected regular dependency assets')
        required = {f'AstroBatch-{version}-{platform}-{suffix}' for platform in PLATFORMS
                    for suffix in ('dependency-lock.json', 'dependency-records.zip')}
        if not required <= {p.name for p in material_files}:
            raise ValueError('Missing platform dependency locks or records')
        validate_names([p.name for p in material_files] + ['AstroBatch-asset-filenames.json','DEPENDENCY_DOWNLOADS.md','SHA256SUMS.txt'])
        material_files += write_asset_index(directory, version, material_files)
        material_tag = dependency_tag(version)
        title = 'AstroBatch ' + version + ' dependency materials'
        material_release = find_release(material_tag, commit, title)
        base = f'https://github.com/{REPO}/releases'
        material_notes = (f'[AstroBatch {version} 程序下载与版本说明]({base}/tag/{tag})\n\n'
                          f'[依赖源码文件索引]({base}/download/{material_tag}/DEPENDENCY_DOWNLOADS.md) · '
                          '各平台的 dependency-records.zip 包含许可证、配方和依赖记录。')
        deliver_files(directory, material_files, material_tag, commit, title, material_notes,
                      True, material_release, materials=True)
        footer = download_section(version)
    else:
        files += write_asset_index(folder, version, files)
    deliver_files(folder, files, tag, commit, 'AstroBatch '+version, notes,
                  '-beta.' in version, existing, footer=footer)
    print('Draft ready for review: '+tag)


def deliver_files(folder, files, tag, commit, title, notes, beta, existing, *, materials=False, footer=''):
    sums = ''.join(digest(p)+'  '+asset_name(p.name)+'\n' for p in files)
    checksum = folder/'SHA256SUMS.txt'
    checksum.write_text(sums, encoding='utf-8')
    files = [*files, checksum]
    ref=subprocess.run(['gh','api',f'repos/{REPO}/git/ref/tags/{tag}'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    if ref.returncode==0:
        obj=json.loads(ref.stdout)['object']
        if obj['type']=='tag':obj=github_json([f'repos/{REPO}/git/tags/{obj["sha"]}'])['object']
        if obj.get('type')!='commit' or obj['sha']!=commit:raise ValueError('Existing tag does not identify this tested commit')
    elif '404' not in ref.stderr:
        raise ValueError('Could not verify the release tag')
    if existing and not existing.get('draft'):
        # The maintainer may already have published the companion before retrying
        # the program draft. Only reuse its immutable, verified content.
        if not materials or ref.returncode != 0:
            raise ValueError('Refusing to change a published release')
        rows = list_assets(existing['id'])
        by_name = {a['name']: a for a in rows}
        if len(rows) != len(files) or by_name.keys() != {asset_name(p.name) for p in files}:
            raise ValueError('Published material inventory differs')
        for path in files:
            remote = by_name[asset_name(path.name)]
            if not remote.get('digest'):
                raise ValueError('Missing published material checksum')
            verify_uploaded(remote, path, asset_name(path.name), 'sha256:'+digest(path))
        return
    if existing is None:
        existing=github_json([f'repos/{REPO}/releases','--method','POST'],payload={
            'tag_name':tag,'target_commitish':commit,'draft':True,
            'prerelease':beta, 'name':title, 'body':notes+('\n\n'+footer if footer and footer not in notes else ''),
            **({'make_latest':'false'} if materials else {})})
        # Creation returns the identity directly. Do not rediscover a new draft
        # through a separately cached/eventually updated release listing.
        if not isinstance(existing,dict) or not isinstance(existing.get('id'),int):
            raise ValueError('Create response did not contain a release ID; inspect Releases before retrying')
        print('Created draft ID: '+str(existing['id']),flush=True)
    release_id=existing['id']
    current = require_draft(release_id,existing['tag_name'])
    payload={'tag_name':tag,'prerelease':beta}
    if footer and footer not in (current.get('body') or ''):
        payload['body'] = (current.get('body') or '').rstrip()+'\n\n'+footer
    if materials:
        payload['make_latest'] = 'false'
    if ref.returncode!=0:payload['target_commitish']=commit
    github_json([f'repos/{REPO}/releases/{release_id}','--method','PATCH'],payload=payload)
    require_draft(release_id,tag)
    protected_names={asset_name(p.name) for p in files}
    if footer:
        unexpected = {a['name'] for a in list_assets(release_id)} - protected_names
        if unexpected:
            raise ValueError('Unexpected files already attached to the program draft: '+', '.join(sorted(unexpected)))
    for path in files:
        upload_asset(release_id,tag,path,beta,protected_names=protected_names)
    require_draft(release_id,tag)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['prepare','mirror-plan','draft'])
    parser.add_argument('--version',default=(ROOT/'VERSION').read_text().strip())
    parser.add_argument('--artifacts',type=Path)
    parser.add_argument('--source-root', type=Path, help='Tested source checkout for prepare')
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--commit')
    args=parser.parse_args()
    if args.command=='prepare':
        if args.source_root is not None and (args.source_root/'VERSION').read_text(encoding='utf-8').strip() != args.version:
            parser.error('Selected version differs from the tested source VERSION')
        prepare(args.artifacts,args.directory,args.version,source=args.source_root or ROOT)
    elif args.command=='draft':
        publish_draft(args.directory,args.version,args.commit)
        if dependency_tag(args.version) != 'v'+args.version and os.environ.get('GITHUB_STEP_SUMMARY'):
            with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as summary:
                summary.write(f'## AstroBatch {args.version}\n\n核验完成。打开 [Releases](https://github.com/{REPO}/releases)，依次检查并发布：\n\n'
                              f'1. `{dependency_tag(args.version)}` — 依赖源码、许可证、配方和文件索引。\n'
                              f'2. `v{args.version}` — 四个平台程序包、项目源码、更新清单和 SHA-256。\n')
    else:
        (args.directory/'mirror-plan.json').write_text(json.dumps(mirror_plan(args.directory,args.version),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
