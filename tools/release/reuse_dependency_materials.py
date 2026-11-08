"""Install verified, previously collected Windows dependency notices offline.

The dependency material directory is read-only. Only Aster.exe may change;
all other native binaries must match the collected inventory byte for byte.
Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import copy
import json
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
import zipfile

from collect_dependency_materials import dependency_index
from dependency_common import MaterialError, digest, is_binary, member_path, run, write_json

ROOT = Path(__file__).resolve().parents[2]
PENDING = 'DEPENDENCY_MATERIALS_PENDING.txt'


def relative_path(value):
    path = member_path(value)
    if not path.parts or path.as_posix() != value or any(p.endswith(('.', ' ')) for p in path.parts):
        raise MaterialError('Invalid relative path in dependency records')
    return path


def checked_file(root, relative):
    path = root.joinpath(*relative_path(relative).parts)
    if not path.is_file() or root.resolve() not in path.resolve().parents:
        raise MaterialError(f'Missing or external material: {relative}')
    return path


def reuse(package, materials, version, qmake):
    package, materials = package.resolve(), materials.resolve()
    if package == materials or package in materials.parents or materials in package.parents:
        raise MaterialError('Keep dependency materials separate from the application package')
    if not package.is_dir():
        raise MaterialError('The staged application directory does not exist')
    # A failed verification must never leave a package marked ready to publish.
    (package / PENDING).write_text(
        'Dependency materials have not been verified for this package. Do not publish.\n'
        'Run tools/release/collect_dependency_materials.py for this staging folder,\n'
        'or resolve the offline verification error and run reuse_dependency_materials.py.\n',
        encoding='utf-8')
    report_path = checked_file(materials, 'report.json')
    report = json.loads(report_path.read_text(encoding='utf-8-sig'))
    if (report.get('schema') != 1 or report.get('project') != 'Aster'
            or report.get('platform') != 'windows-x64' or report.get('version') != version
            or report.get('status') != 'collected' or report.get('errors')):
        raise MaterialError('Materials must be successfully collected for this Aster version and windows-x64')
    components = {c['id']: c for c in report['components']}
    if not components or len(components) != len(report['components']):
        raise MaterialError('Missing or duplicate dependency components')
    if components.get('qt', {}).get('version') != run([qmake, '-query', 'QT_VERSION']):
        raise MaterialError('Qt version changed; collect dependency materials again')

    recorded = {}
    for item in report['binaries']:
        name = relative_path(item['path']).as_posix()
        key = name.casefold()
        if key in recorded or not re.fullmatch(r'[0-9a-f]{64}', item['sha256_before_signing']):
            raise MaterialError('Invalid or duplicate native binary inventory')
        if name == 'Aster.exe':
            if item['component'] != 'Aster-project-source':
                raise MaterialError('Aster.exe has an invalid source owner')
        elif item['component'] not in components:
            raise MaterialError(f'Unknown dependency source owner: {name}')
        recorded[key] = item
    if 'aster.exe' not in recorded:
        raise MaterialError('Materials do not contain the Aster.exe inventory')

    actual = {}
    for path in sorted(package.rglob('*')):
        relative = path.relative_to(package)
        if relative.parts[0].casefold() == 'licenses':
            continue
        if path.is_symlink():
            raise MaterialError(f'Unexpected symlink in Windows package: {relative.as_posix()}')
        if not is_binary(path):
            continue
        key = relative.as_posix().casefold()
        if key in actual:
            raise MaterialError('Duplicate Windows native binary path')
        actual[key] = path
    if actual.keys() != recorded.keys():
        changed = sorted(actual.keys() ^ recorded.keys())
        raise MaterialError('Runtime dependency set changed: ' + ', '.join(changed))
    inventory = []
    for key, path in actual.items():
        item = copy.deepcopy(recorded[key])
        current = digest(path)
        if key != 'aster.exe' and current != item['sha256_before_signing']:
            raise MaterialError(f'Dependency binary changed: {item["path"]}; collect materials again')
        item.update(path=path.relative_to(package).as_posix(), sha256_before_signing=current)
        inventory.append(item)

    assets = materials / 'release-assets'
    source_names = set()
    for component in components.values():
        if not component['notices'] or not (component['sources'] or component['recipes']):
            raise MaterialError(f'Incomplete component: {component["id"]}')
        for source in component['sources']:
            name = source['asset']
            if len(relative_path(name).parts) != 1 or source.get('status') != 'verified':
                raise MaterialError('Source asset is not verified')
            path = checked_file(assets, name)
            if path.stat().st_size != source['bytes'] or digest(path) != source['sha256']:
                raise MaterialError(f'Source asset checksum mismatch: {name}')
            source_names.add(name)

    records_name = f'Aster-{version}-windows-x64-dependency-records.zip'
    records_zip = checked_file(assets, records_name)
    # Read the small records ZIP, never unpack or reprocess dependency source
    # archives. Old collector output is accepted without an extra migration.
    with zipfile.ZipFile(records_zip) as archive:
        members, folded = {}, set()
        for info in archive.infolist():
            if info.is_dir():
                continue
            name = relative_path(info.filename).as_posix()
            if name.casefold() in folded or stat.S_ISLNK(info.external_attr >> 16):
                raise MaterialError('Duplicate or linked entry in dependency records')
            folded.add(name.casefold())
            members[name] = info
        if json.loads(archive.read('DEPENDENCIES.json')) != report:
            raise MaterialError('Report and published dependency records disagree')
        for component in components.values():
            for name in component['notices'] + component['recipes']:
                relative_path(name)
                if name not in members and not any(p.startswith(name + '/') for p in members):
                    raise MaterialError(f'Missing license or recipe in records: {name}')
            if any(not name.startswith('licenses/') for name in component['notices']):
                raise MaterialError('Invalid license location in records')
        bad = archive.testzip()
        if bad:
            raise MaterialError(f'Damaged dependency record: {bad}')
        # Verify everything before changing the installed notices. Temporary
        # extraction also protects existing notices from malformed ZIP entries.
        with tempfile.TemporaryDirectory(prefix='aster-reuse-') as directory:
            notices = Path(directory) / 'dependencies'
            notices.mkdir()
            for name, info in members.items():
                if not name.startswith('licenses/'):
                    continue
                target = notices.joinpath(*relative_path(name).parts[1:])
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.read(info))
            refreshed = copy.deepcopy(report)
            refreshed['binaries'] = inventory
            refreshed['reuse'] = {
                'mode': 'offline-verified', 'material_report_sha256': digest(report_path),
                'records_asset': records_name, 'records_sha256': digest(records_zip),
                'note': 'Dependency materials are unchanged; this package records the current Aster executable.'}
            write_json(notices / 'DEPENDENCIES.json', refreshed)
            target = package / 'licenses' / 'dependencies'
            if target.exists():
                if target.is_symlink() or target.resolve() != target.absolute():
                    raise MaterialError('Refusing to replace linked notice directory')
                shutil.rmtree(target)
            if (package / 'licenses').is_symlink():
                raise MaterialError('Refusing to install into linked licenses directory')
            shutil.copytree(notices, target)
    (package / 'SOURCE_DEPENDENCIES.md').write_text(
        dependency_index(version, 'windows-x64', source_names | {records_name}), encoding='utf-8')
    (package / PENDING).unlink()
    print(f'Reused verified dependency materials offline: {materials}', flush=True)
    print(f'Upload all files from: {assets}', flush=True)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-dir', type=Path, required=True)
    parser.add_argument('--materials', type=Path, required=True)
    parser.add_argument('--qmake', type=Path, required=True)
    args = parser.parse_args(argv)
    version = (ROOT / 'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', version):
        parser.error('Invalid VERSION')
    try:
        return reuse(args.package_dir, args.materials, version, args.qmake)
    except (OSError, ValueError, KeyError, TypeError, AttributeError, MaterialError, zipfile.BadZipFile) as error:
        print(f'Dependency reuse failed: {error}', file=sys.stderr)
        print('No downloads were attempted. Run collect_dependency_materials.py manually for this staged package.', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
