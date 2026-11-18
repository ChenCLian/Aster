"""Reuse verified sources from published official releases; never trust a pass flag.

Only small records archives are downloaded. Current build recipes and binary
ownership are always rediscovered. Missing history falls back to collection.
"""
import argparse
import hashlib
import io
import json
import re
import subprocess
import zipfile
from pathlib import Path, PurePosixPath
from urllib.parse import quote, unquote, urlsplit
from release_asset_names import asset_name

REPO = 'ChenCLian/Aster'
MAX_RECORDS = 64 * 1024**2
MAX_EXPANDED = 256 * 1024**2
VERSION = re.compile(r'\d+\.\d+\.\d+(?:-beta\.\d+)?')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def github(endpoint, binary=False):
    args = ['gh', 'api', endpoint]
    if binary:
        args += ['-H', 'Accept: application/octet-stream']
    try:
        result = subprocess.run(args, check=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=120)
    except OSError as error:
        raise ValueError('GitHub CLI is unavailable') from error
    if len(result.stdout) > MAX_RECORDS:
        raise ValueError('Dependency history exceeds the size limit')
    return result.stdout if binary else json.loads(result.stdout)


def asset_url(version, name):
    return f'https://github.com/{REPO}/releases/download/v{version}/{quote(asset_name(name), safe="")}'


def source_location(source, version):
    url = source.get('published_url') or asset_url(version, source['asset'])
    parsed = urlsplit(url)
    prefix = f'/{REPO}/releases/download/v'
    if parsed.scheme != 'https' or parsed.netloc != 'github.com' or parsed.query or parsed.fragment or not parsed.path.startswith(prefix):
        raise ValueError('Dependency history must reference official GitHub assets')
    tag, sep, name = parsed.path[len(prefix):].partition('/')
    if not sep or not VERSION.fullmatch(tag) or unquote(name) not in (source['asset'], asset_name(source['asset'])) or '/' in unquote(name):
        raise ValueError('Invalid published dependency location')
    return tag, url


def read_records(data, platform):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        infos = archive.infolist()
        if len(infos) > 20000 or sum(i.file_size for i in infos) > MAX_EXPANDED:
            raise ValueError('Dependency records exceed extraction limits')
        names = [i.filename for i in infos]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate dependency record names')
        for info in infos:
            path = PurePosixPath(info.filename)
            if (path.is_absolute() or '..' in path.parts or '\\' in info.filename or ':' in info.filename or
                (info.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError('Unsafe dependency record path')
        report = json.loads(archive.read('DEPENDENCIES.json'))
        if (report.get('schema') != 1 or report.get('project') != 'Aster' or
            report.get('platform') != platform or report.get('status') != 'collected' or
            report.get('errors') or not VERSION.fullmatch(report.get('version', ''))):
            raise ValueError('Incomplete or mismatched published dependency records')
    return report


def fetch(directory, platform, releases_limit=3):
    directory.mkdir(parents=True, exist_ok=True)
    # Clear the index first; a failed network attempt must not reuse stale claims
    # that an asset still exists. Records ZIPs can remain as checksummed cache.
    index = directory / 'index.json'
    index.write_text(json.dumps({'schema': 1, 'platform': platform, 'records': []})+'\n')
    releases = github(f'repos/{REPO}/releases?per_page=100')
    by_version = {}
    def release(version):
        if version not in by_version:
            item = github(f'repos/{REPO}/releases/tags/v{version}')
            if item.get('draft') or item.get('tag_name') != 'v'+version:
                raise ValueError('Dependency source is not a published release')
            # API release responses can truncate large asset arrays. Always page.
            assets = []
            for page in range(1, 101):
                rows = github(f'repos/{REPO}/releases/{item["id"]}/assets?per_page=100&page={page}')
                assets += rows
                if len(rows) < 100: break
            else: raise ValueError('Incomplete dependency asset listing')
            by_version[version] = {a['name']: a for a in assets if a.get('state') == 'uploaded'}
        return by_version[version]
    entries = []
    candidates = [r for r in releases if not r.get('draft') and VERSION.fullmatch(r.get('tag_name', '')[1:]) and r.get('tag_name','').startswith('v')]
    for item in candidates:
        if len(entries) >= releases_limit: break
        version = item['tag_name'][1:]
        try:
            assets = release(version)
            name = f'Aster-{version}-{platform}-dependency-records.zip'
            asset = assets.get(name)
            if not asset: continue
            expected = asset.get('digest', '')
            if not re.fullmatch(r'sha256:[0-9a-f]{64}', expected or ''):
                # Older assets without a platform digest have no reusable receipt.
                continue
            if asset.get('size', MAX_RECORDS+1) > MAX_RECORDS: continue
            path = directory / name
            data = path.read_bytes() if path.is_file() else b''
            if sha(data) != expected[7:]:
                data = github(f'repos/{REPO}/releases/assets/{asset["id"]}', binary=True)
            if sha(data) != expected[7:]: raise ValueError('Dependency record checksum mismatch')
            report = read_records(data, platform)
            if report['version'] != version: raise ValueError('Dependency version differs from release tag')
            available = {}
            for component in report['components']:
                for source in component['sources']:
                    source_version, url = source_location(source, version)
                    published = release(source_version).get(unquote(urlsplit(url).path.rsplit('/', 1)[-1]))
                    fingerprint = source.get('sha256', '')
                    if (published and re.fullmatch('[0-9a-f]{64}', fingerprint) and
                        published.get('size') == source.get('bytes') and
                        published.get('digest') == 'sha256:'+fingerprint):
                        available[url] = fingerprint
            path.write_bytes(data)
            entries.append({'file': name, 'sha256': sha(data), 'available': available})
        except (OSError, ValueError, TypeError, KeyError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
            print(f'History unavailable for {version}: {type(error).__name__}; collect unmatched sources.', flush=True)
    index.write_text(json.dumps({'schema': 1, 'platform': platform, 'records': entries}, indent=2)+'\n')
    print(f'Published dependency receipts: {len(entries)} ({platform})', flush=True)


class PublishedHistory:
    def __bool__(self):
        # Actions configures the directory before fetching receipts. Missing,
        # empty or rejected receipts must behave like history being disabled.
        return bool(self.entries)

    def __init__(self, directory, platform):
        self.entries = []
        directory = Path(directory)
        try:
            index = json.loads((directory/'index.json').read_text())
            if index.get('schema') != 1 or index.get('platform') != platform: return
            for entry in index.get('records', []):
                name = entry['file']
                if Path(name).name != name: continue
                data = (directory/name).read_bytes()
                if sha(data) != entry['sha256']: continue
                self.entries.append((data, read_records(data, platform), entry['available']))
        except (OSError, ValueError, TypeError, KeyError, zipfile.BadZipFile):
            self.entries = []

    def matching(self, component):
        for data, report, available in self.entries:
            for old in report['components']:
                if all(old.get(k) == component.get(k) for k in ('id','version','provider')):
                    yield data, report, available, old

    @staticmethod
    def restore_notices(collection, component, data, old):
        # Only extract regular license files for this component. Never recipes,
        # executable content, symlinks, or a ZIP's arbitrary target paths.
        prefix = 'licenses/'+component['id']+'/'
        restored = 0
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for info in archive.infolist():
                if info.is_dir() or not info.filename.startswith(prefix): continue
                relative = Path(info.filename)
                if ('..' in relative.parts or '\\' in info.filename or ':' in info.filename or
                    (info.external_attr >> 16) & 0o170000 == 0o120000):
                    raise ValueError('Unsafe published license path')
                target = collection.records/relative
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists(): target.write_bytes(archive.read(info))
                restored += 1
        if restored:
            notice = prefix.rstrip('/')
            if notice not in component['notices']: component['notices'].append(notice)
        return restored

    def source(self, collection, component, record, extract):
        for data, report, available, old in self.matching(component):
            for source in old['sources']:
                if any(source.get(k) != record.get(k) for k in ('asset','checksum_algorithm','expected_checksum')): continue
                _, url = source_location(source, report['version'])
                if available.get(url) != source.get('sha256'): continue
                if extract and not self.restore_notices(collection, component, data, old): continue
                record.update(status='reused-published', sha256=source['sha256'], bytes=source['bytes'], published_url=url)
                print(f'Reusing published source: {component["id"]} {record["asset"]}', flush=True)
                return True
        return False

    def debian(self, collection, component, expected):
        for data, report, available, old in self.matching(component):
            if any(old.get(k) != component.get(k) for k in ('source_package','source_version','architecture')): continue
            sources = old['sources']
            if not sources or {s['asset']:s.get('sha256') for s in sources} != expected: continue
            selected = []
            for source in sources:
                _, url = source_location(source, report['version'])
                if available.get(url) != source.get('sha256'): break
                selected.append(dict(source, status='reused-published', published_url=url))
            else:
                component['sources'] = selected
                return True
        return False


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--platform',choices=['windows-x64','linux-x64','macos-arm64','macos-x86_64'],required=True)
    args=parser.parse_args()
    try: fetch(args.directory,args.platform)
    except (ValueError,TypeError,KeyError,subprocess.SubprocessError) as error:
        print(f'Published history unavailable ({type(error).__name__}); using full collection.',flush=True)

if __name__=='__main__': main()
