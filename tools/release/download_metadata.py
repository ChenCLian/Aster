"""Generate full-package download information from final release archives.

No keys, server or expiry dates. Run once after packaging; never re-compress mirrors.
Stamp each staged folder before archiving, then generate Aster-update.json from
one or more final archives. A Gitee source is added only with a byte-identical
mirror copy supplied through --verified-mirror-dir (publication still needs an
anonymous HTTPS check in the publishing workflow).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORMS = ('windows-x64', 'macos-arm64', 'macos-x86_64', 'linux-x64')


def valid_version(version):
    return bool(re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-beta\.(0|[1-9][0-9]*))?', version))


def stamp(folder, version, platform):
    if platform not in PLATFORMS or not valid_version(version):
        raise ValueError('Only stable and beta packages are eligible for update downloads')
    if folder.name != f'Aster-{version}-{platform}' or not folder.is_dir():
        raise ValueError('Staged folder name does not match version/platform')
    entry = 'Aster.exe' if platform == 'windows-x64' else 'AppRun' if platform == 'linux-x64' else 'Aster.app/Contents/MacOS/Aster'
    if not (folder / entry).is_file():
        raise ValueError('Missing launch file')
    (folder / 'aster-package.json').write_text(json.dumps(dict(schema=1, version=version, platform=platform), indent=2)+'\n', encoding='utf-8')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def inspect_package(path, version, platform):
    name = f'Aster-{version}-{platform}'
    suffix = '.tar.gz' if platform == 'linux-x64' else '.zip'
    if path.name != name + suffix:
        raise ValueError('Archive name does not match version/platform')
    marker_path = name + '/aster-package.json'
    if suffix == '.zip':
        with zipfile.ZipFile(path) as archive:
            records = archive.infolist()
            if any(r.filename.endswith('/DEPENDENCY_MATERIALS_PENDING.txt') for r in records):
                raise ValueError('Dependency materials are still pending')
            marker = archive.getinfo(marker_path)
            if marker.file_size > 4096:
                raise ValueError('Package marker is too large')
            info = json.loads(archive.read(marker))
            expanded = sum(r.file_size for r in records)
    else:
        with tarfile.open(path, 'r:gz') as archive:
            records = archive.getmembers()
            if any(r.name.endswith('/DEPENDENCY_MATERIALS_PENDING.txt') for r in records):
                raise ValueError('Dependency materials are still pending')
            marker = archive.getmember(marker_path)
            if not marker.isfile() or marker.size > 4096:
                raise ValueError('Invalid package marker')
            info = json.load(archive.extractfile(marker))
            expanded = sum(r.size for r in records)
    if info != dict(schema=1, version=version, platform=platform):
        raise ValueError('Archive package marker does not match version/platform')
    if not 0 < path.stat().st_size <= 2 * 1024**3 or not 0 < expanded <= 4 * 1024**3:
        raise ValueError('Package size exceeds supported limits')
    return dict(platform=platform, file=path.name, bytes=path.stat().st_size,
                unpacked_bytes=expanded, sha256=digest(path))


def generate(paths, version, mirror_dir=None):
    if not valid_version(version):
        raise ValueError('Alpha/development versions are excluded')
    packages = []
    for path in paths:
        candidates = [p for p in PLATFORMS if path.name == f'Aster-{version}-{p}' + ('.tar.gz' if p == 'linux-x64' else '.zip')]
        if len(candidates) != 1 or any(p['platform'] == candidates[0] for p in packages):
            raise ValueError('Invalid or duplicate package platform')
        item = inspect_package(path, version, candidates[0])
        item['sources'] = [dict(name='GitHub', url=f'https://github.com/ChenCLian/Aster/releases/download/v{version}/{path.name}')]
        if mirror_dir is not None:
            copy = mirror_dir / path.name
            if not copy.is_file() or digest(copy) != item['sha256']:
                raise ValueError('Mirror copy is missing or differs from the original archive')
            item['sources'].append(dict(name='Gitee', url=f'https://gitee.com/ChenCLian/Aster/releases/download/v{version}/{path.name}'))
        packages.append(item)
    if not packages:
        raise ValueError('No package supplied')
    return dict(schema=1, version=version, packages=packages)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default=(ROOT/'VERSION').read_text().strip())
    commands = parser.add_subparsers(dest='command', required=True)
    mark = commands.add_parser('stamp')
    mark.add_argument('folder', type=Path)
    mark.add_argument('--platform', choices=PLATFORMS, required=True)
    build = commands.add_parser('generate')
    build.add_argument('archives', nargs='+', type=Path)
    build.add_argument('--output', type=Path, required=True)
    build.add_argument('--verified-mirror-dir', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'stamp':
            stamp(args.folder, args.version, args.platform)
        else:
            data = generate(args.archives, args.version, args.verified_mirror_dir)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, tarfile.TarError) as error:
        parser.exit(2, f'Download metadata failed: {error}\n')


if __name__ == '__main__':
    main()
