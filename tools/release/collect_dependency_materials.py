"""Collect exact dependency sources, recipes and notices beside a staged release.

Run on the build host before packaging/signing. See docs/dependency_materials_zh.md.
No source is compiled, no downloaded program is executed, and nothing is published.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import zipfile

from dependency_common import (Collection, MaterialError, digest, is_binary,
                               member_path, run, safe_name, write_json)
from dependency_providers import (brew_sources, debian_sources, qt_owners,
                                  qt_sources, qt_vendor_sources, vcpkg_sources)

ROOT = Path(__file__).resolve().parents[2]


def dependency_index(version, platform, names):
    urls = '\n'.join(f'- [{name}](https://github.com/ChenCLian/Aster/releases/download/v{version}/{name})'
                     for name in sorted(names))
    return (f'# Aster {version} dependency sources ({platform})\n\n'
            f'Source archives and build records for this package:\n\n{urls}\n\n'
            'Project source: see SOURCE_CODE.md. Third-party notices: licenses/dependencies/.\n'
            'La2004/La2010 redistribution is a separate, unresolved data review.\n')


def supplement(collection, path, package, owners):
    """Explicit exact-version additions for nonstandard installations.

    A replacement must explicitly name the same component/version and provide
    sources, notices and recipes. Discovery errors remain recorded for review.
    """
    data = json.loads(path.read_text(encoding='utf-8'))
    for item in data.get('components', []):
        previous = next((c for c in collection.components if c['id'] == item['id']), None)
        if previous and (not item.get('replace') or previous['version'] != item['version']):
            raise MaterialError('Replacing discovery requires replace=true and the same installed version')
        component = collection.component(item['id'], item['version'], 'maintainer-supplied')
        component['provenance'] = item['provenance']
        if not item.get('sources'):
            raise MaterialError('A supplement must include complete, checksummed source archives')
        for kind in ('licenses', 'recipes'):
            if not item.get(kind):
                raise MaterialError('Supplement requires both license files and build recipes')
            for value in item[kind]:
                local = path.parent / value
                collection.save_record(component, local.name, local.read_bytes(), kind)
        for source in item['sources']:
            collection.source(component, source['url'], 'sha256', source['sha256'], source['filename'],
                              extract=source.get('archive', True))
        for value in item.get('binaries', []):
            binary = package.joinpath(*member_path(value['path']).parts)
            if not binary.is_file() or digest(binary) != value['sha256']:
                raise MaterialError('Supplement binary hash does not match the staged package')
            owners[binary] = component['id']
        if previous:
            resolved = [e for e in collection.errors if e['component'] == item['id']
                        or e['component'].startswith(item['id'] + '/')]
            component['replaced_discovery_errors'] = resolved
            component['installed_metadata'] = {k:v for k,v in previous.items() if k in
                {'id','version','provider','recipe_revision','source_package','source_version','architecture'}}
            collection.errors = [e for e in collection.errors if e not in resolved]
            collection.components.remove(previous)


def finish(collection, package, owners, binaries, install=True):
    for binary in binaries:
        relative = binary.relative_to(package).as_posix()
        owner = owners.get(binary)
        if not owner:
            collection.error(relative, 'Runtime binary has no verified source owner; use the matching build environment or a supplement')
        collection.inventory.append({'path':relative, 'sha256_before_signing':digest(binary),
                                     'component':owner or 'UNRESOLVED'})
    if not collection.plan_only:
        for component in collection.components:
            if not component['notices']:
                collection.error(component['id'], 'No license/attribution materials were collected')
            if not component['sources'] and not component['recipes']:
                collection.error(component['id'], 'No corresponding source or source recipe was collected')
    report = {'schema':1, 'project':'Aster', 'version':collection.version,
              'platform':collection.platform,
              'status':'plan-only' if collection.plan_only else ('incomplete' if collection.errors else 'collected'),
              'scope':'Dependency material collection; not a legal clearance or a reproducible-build certificate.',
              'deferred':['La2004/La2010 redistribution basis: see docs/third_party.md'],
              'components':collection.components, 'binaries':collection.inventory, 'errors':collection.errors}
    write_json(collection.records / 'DEPENDENCIES.json', report)
    write_json(collection.output / 'report.json', report)
    lines = ['# Aster dependency materials', '', f'Version: {collection.version}',
             f'Platform: {collection.platform}', f'Status: {report["status"]}', '',
             'This records retrieved dependency sources and notices. It does not certify all redistribution rights.',
             'La2004/La2010 data redistribution remains outside this collection.', '',
             '## Components', '', '| Component | Version | Source assets |', '| --- | --- | --- |']
    for item in collection.components:
        assets = ', '.join(x['asset'] for x in item['sources']) or 'See recipes/'
        lines.append(f'| {item["id"]} | {item["version"]} | {assets} |')
    if collection.errors:
        lines += ['', '## Unresolved items', ''] + [f'- {e["component"]}: {e["message"]}' for e in collection.errors]
    lines += ['', '## Publication', '',
              'Upload every file in release-assets/ next to the program download; link the matching Aster source from the release tag.',
              'Do not upload cache/ or the local build environment. Sources are not compiled into the application.',
              'Keep the original dependency source filenames, especially the files referenced by Debian .dsc records.',
              'The records ZIP contains licenses, source URLs/checksums, build recipes and the staged binary inventory.',
              'Use the build receipt/recipe to apply patches before rebuilding a dependency.',
              'Binary fingerprints are captured before macOS signing; source checksums remain in DEPENDENCIES.json.', '']
    (collection.records / 'README.md').write_text('\n'.join(lines), encoding='utf-8')
    if collection.errors or collection.plan_only:
        # Never write publishable manifests/notices into the application on failure.
        return 2 if collection.errors else 0
    names = sorted({s['asset'] for c in collection.components for s in c['sources']})
    for name in names:
        asset = collection.assets / name
        if not asset.is_file() or asset.stat().st_size >= 2 * 1024**3:
            collection.error(name, 'Missing source asset or GitHub per-asset size limit exceeded')
    if collection.errors:
        write_json(collection.output / 'report.json', dict(report, status='incomplete', errors=collection.errors))
        return 2
    records_name = f'Aster-{collection.version}-{collection.platform}-dependency-records.zip'
    temporary = collection.output / (records_name + '.partial')
    with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(collection.records.rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(collection.records).as_posix())
    temporary.replace(collection.assets / records_name)
    names.append(records_name)
    index = dependency_index(collection.version, collection.platform, names)
    (collection.output / 'SOURCE_DEPENDENCIES.md').write_text(index, encoding='utf-8')
    if install:
        # Called before macOS signing. Never patch an already signed .app here.
        target = package / 'licenses' / 'dependencies'
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(collection.licenses, target)
        shutil.copy2(collection.records / 'DEPENDENCIES.json', target / 'DEPENDENCIES.json')
        shutil.copy2(collection.output / 'SOURCE_DEPENDENCIES.md', package / 'SOURCE_DEPENDENCIES.md')
        if collection.platform == 'windows-x64':
            (package / 'DEPENDENCY_MATERIALS_PENDING.txt').unlink(missing_ok=True)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-dir', type=Path, required=True, help='Uncompressed staging folder from the matching build')
    parser.add_argument('--qmake', type=Path, required=True)
    parser.add_argument('--platform', required=True, choices=['windows-x64','linux-x64','macos-arm64','macos-x86_64'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--vcpkg-prefix', type=Path)
    parser.add_argument('--vcpkg-root', type=Path)
    parser.add_argument('--supplement', type=Path)
    parser.add_argument('--plan-only', action='store_true', help='Inspect metadata without downloading source archives')
    parser.add_argument('--no-install', action='store_true', help='Do not modify the staged package')
    args = parser.parse_args(argv)
    version = (ROOT / 'VERSION').read_text().strip()
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?', version):
        parser.error('Invalid VERSION')
    package = args.package_dir.resolve()
    output = args.output.resolve()
    if not package.is_dir():
        parser.error('--package-dir must be an existing uncompressed staging folder')
    if output == package or package in output.parents or output in package.parents:
        parser.error('Keep dependency output separate from the application package')
    if output == ROOT or (ROOT in output.parents and ROOT / 'dist' not in output.parents):
        parser.error('Use an output outside the source tree or below dist/')
    if args.platform.startswith('windows') and not args.vcpkg_prefix:
        parser.error('--vcpkg-prefix is required for the Windows provider')
    if args.platform == 'windows-x64' and not args.no_install and not args.plan_only:
        (package / 'DEPENDENCY_MATERIALS_PENDING.txt').write_text(
            'Dependency collection is incomplete. Do not publish this folder.\n'
            'Complete collect_dependency_materials.py successfully before publishing.\n', encoding='utf-8')
    # Source caches survive retries, but generated records/assets must not retain
    # stale components after a dependency was removed or downgraded.
    output.mkdir(parents=True, exist_ok=True)
    marker = output / '.aster-dependency-materials'
    if any(output.iterdir()) and not marker.is_file():
        parser.error('Output must be empty or a previous collector output')
    marker.write_text('Aster dependency collector output\n', encoding='utf-8')
    for name in ('records', 'release-assets'):
        if (output / name).exists():
            shutil.rmtree(output / name)
    for name in ('report.json', 'SOURCE_DEPENDENCIES.md'):
        (output / name).unlink(missing_ok=True)
    cache = args.cache.resolve() if args.cache else output / 'cache'
    collection = Collection(output, cache, args.platform, version, args.plan_only)
    binaries = sorted(p for p in package.rglob('*')
                      if 'licenses' not in p.relative_to(package).parts and is_binary(p))
    if not binaries:
        parser.error('No native binaries were found in the staging folder')
    owners = {p:'Aster-project-source' for p in binaries
              if p.relative_to(package).as_posix() in {'Aster.exe','usr/bin/Aster','Aster.app/Contents/MacOS/Aster'}}
    try:
        prefix, _ = qt_sources(collection, args.qmake.absolute(), binaries)
        owners.update(qt_owners(binaries, prefix, args.platform))
        qt_vendor_sources(collection, prefix, binaries, owners)
    except (OSError, ValueError, MaterialError) as error:
        collection.error('qt', str(error) if isinstance(error, MaterialError) else 'Qt kit metadata could not be read')
    try:
        if args.platform.startswith('windows'):
            root = args.vcpkg_root
            if root is None:
                nearby = args.vcpkg_prefix.resolve().parent.parent
                environment = Path(os.environ.get('VCPKG_ROOT', ''))
                root = nearby if (nearby / 'ports').is_dir() else environment
                if not (root / 'ports').is_dir():
                    raise MaterialError('Pass --vcpkg-root pointing to the original vcpkg repository containing ports/')
            vcpkg_sources(collection, args.vcpkg_prefix, root, binaries, owners)
        elif args.platform.startswith('linux'):
            debian_sources(collection, binaries, owners)
        else:
            brew_sources(collection, binaries, owners)
        if args.supplement:
            supplement(collection, args.supplement.resolve(), package, owners)
    except (OSError, ValueError, KeyError, MaterialError) as error:
        collection.error('native-dependencies', str(error) if isinstance(error, MaterialError) else 'Native dependency metadata could not be read')
    result = finish(collection, package, owners, binaries, install=not args.no_install)
    print(f'Result: {collection.output / "report.json"}', flush=True)
    if result:
        print('Collection incomplete. Resolve report.json entries before publishing.', flush=True)
    elif args.plan_only:
        print('Plan only; source archives and final notices have not been collected.', flush=True)
    else:
        print(f'Upload all files from: {collection.assets}', flush=True)
    return result


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, MaterialError) as error:
        print(f'Collection failed: {error}', file=sys.stderr)
        raise SystemExit(2)
