"""Source provenance adapters for Qt, vcpkg, Debian/Ubuntu and Homebrew.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import urllib.parse

from dependency_common import (MaterialError, checksum_value, digest, is_binary,
                               member_path, read_url, run, safe_name, write_json)
from dependency_mirrors import QT_SOURCE_SHA256


def qt_module(name):
    if name in {'Core', 'Gui', 'Widgets', 'Network', 'Sql', 'PrintSupport', 'DBus',
                'OpenGL', 'OpenGLWidgets', 'Concurrent', 'Test', 'Xml', 'XcbQpa',
                'EglFSDeviceIntegration', 'EglFsKmsSupport', 'InputSupport',
                'XkbCommonSupport', 'DeviceDiscoverySupport', 'FbSupport', 'KmsSupport',
                'EdidSupport', 'LinuxAccessibilitySupport', 'ServiceSupport', 'ThemeSupport',
                'FontDatabaseSupport', 'VulkanSupport', 'EglSupport', 'GlxSupport'}:
        return 'qtbase'
    if name in {'Svg', 'SvgWidgets'}:
        return 'qtsvg'
    if name in {'Pdf', 'PdfWidgets', 'WebEngineCore', 'WebEngineWidgets', 'WebEngineQuick'}:
        return 'qtwebengine'
    if name.startswith(('Qml', 'Quick', 'Labs')):
        return 'qtdeclarative'
    if name == 'ShaderTools':
        return 'qtshadertools'
    if name == 'Core5Compat':
        return 'qt5compat'
    if name in {'WaylandClient', 'WaylandCompositor', 'WaylandEglClientHwIntegration', 'WaylandEglCompositorHwIntegration'}:
        return 'qtwayland'
    raise MaterialError(f'Qt module mapping is unknown: {name}; add its exact source through --supplement')


def qt_checksum(data):
    try:
        fields = data.decode('ascii').split()
    except UnicodeError as error:
        raise MaterialError('Invalid Qt SHA-256 response') from error
    if not fields or not re.fullmatch('[a-fA-F0-9]{64}', fields[0]):
        raise MaterialError('Invalid Qt SHA-256 response')
    return fields[0].lower()


def qt_sources(collection, qmake, binaries):
    version = run([qmake, '-query', 'QT_VERSION'])
    if not re.fullmatch(r'6\.\d+\.\d+', version):
        raise MaterialError('Only stable Qt 6 source archives are detected automatically')
    prefix = Path(run([qmake, '-query', 'QT_INSTALL_PREFIX'])).resolve()
    component = collection.component('qt', version, 'official-qt-desktop-kit')
    if prefix == Path('/usr') or 'Cellar' in prefix.parts or 'vcpkg_installed' in prefix.parts:
        raise MaterialError('This Qt installation is managed/patched by a package manager; use its exact source and recipes, not official Qt module archives')
    # Official desktop kit layout is supported. A patched/custom Qt build must
    # supply its actual sources and recipes explicitly, not a guessed version.
    modules = {'qtbase', 'qtsvg', 'qtimageformats', 'qttranslations'}
    qt_binaries = set()
    for binary in binaries:
        match = re.match(r'(?:lib)?Qt6([A-Za-z0-9]+)(?:\.dll|\.so|\.dylib)', binary.name)
        framework = next((p[:-10] for p in binary.parts if p.startswith('Qt') and p.endswith('.framework')), None)
        if match or framework:
            modules.add(qt_module(match[1] if match else framework[2:]))
            qt_binaries.add(binary)
    component['modules'] = sorted(modules)
    component['build_scope'] = 'Official unmodified Qt desktop kit; custom Qt requires an explicit supplement.'
    for rel in ('mkspecs/qconfig.pri', 'mkspecs/qmodule.pri'):
        path = prefix / rel
        if path.is_file():
            text = path.read_text(encoding='utf-8', errors='replace')
            # Keep feature/compiler switches without leaking builder path values.
            lines = [line for line in text.splitlines() if not re.search(r'(?:[A-Za-z]:[/\\]|/(?:home|Users|workspace|opt|usr|Applications)/)', line)]
            collection.save_record(component, rel, ('\n'.join(lines)+'\n').encode())
    for module in sorted(modules):
        filename = f'{module}-everywhere-src-{version}.tar.xz'
        url = f'https://download.qt.io/archive/qt/{version.rsplit(".",1)[0]}/{version}/submodules/{filename}'
        try:
            checksum = QT_SOURCE_SHA256.get(version, {}).get(module)
            checksum_origin = 'bundled upstream SHA-256 snapshot (2026-11-06)'
            if checksum is None:
                checksum = qt_checksum(read_url(url + '.sha256', validator=qt_checksum))
                checksum_origin = 'upstream HTTPS SHA-256 response'
            collection.save_record(component, filename + '.sha256',
                                   (checksum + '  ' + filename + '\n').encode())
            component.setdefault('checksum_metadata', []).append(
                {'url': url + '.sha256', 'source': checksum_origin})
            collection.source(component, url, 'sha256', checksum, filename)
        except (OSError, ValueError, MaterialError) as error:
            collection.error('qt/' + module, str(error) if isinstance(error, MaterialError) else 'Qt source metadata/download unavailable')
    return prefix, qt_binaries


def file_index(root):
    index = {}
    for path in root.rglob('*'):
        if path.is_file():
            index.setdefault(path.name.lower(), []).append(path)
    return index


def binary_id(path, platform):
    if platform.startswith('linux'):
        text = run(['readelf', '-n', path])
        match = re.search(r'Build ID: ([a-fA-F0-9]+)', text)
        return match[1].lower() if match else digest(path)
    if platform.startswith('macos'):
        text = run(['dwarfdump', '--uuid', path])
        values = sorted(re.findall(r'UUID: ([A-Fa-f0-9-]+)', text))
        return ','.join(values) if values else digest(path)
    # windeployqt patches QtCore's installation path in .rdata. Its CodeView
    # identity and executable code section remain stable across that operation.
    data = path.read_bytes()
    if data[:2] == b'MZ' and len(data) >= 64:
        try:
            pe = struct.unpack_from('<I', data, 60)[0]
            if data[pe:pe+4] == b'PE\0\0':
                count = struct.unpack_from('<H', data, pe+6)[0]
                optional_size = struct.unpack_from('<H', data, pe+20)[0]
                optional = pe + 24
                magic = struct.unpack_from('<H', data, optional)[0]
                directory = optional + (112 if magic == 0x20b else 96)
                debug_rva, debug_size = struct.unpack_from('<II', data, directory+6*8)
                sections = []
                text_hash = ''
                for i in range(count):
                    pos = optional + optional_size + i*40
                    name = data[pos:pos+8].rstrip(b'\0')
                    virtual_size, rva, size, offset = struct.unpack_from('<IIII', data, pos+8)
                    sections.append((rva,max(virtual_size,size),offset))
                    if name == b'.text':
                        import hashlib
                        text_hash = hashlib.sha256(data[offset:offset+size]).hexdigest()
                for rva,size,offset in sections:
                    if rva <= debug_rva < rva+size:
                        start = offset + debug_rva-rva
                        for pos in range(start,start+debug_size,28):
                            kind, length, _, pointer = struct.unpack_from('<IIII', data, pos+12)
                            if kind == 2 and length >= 24 and data[pointer:pointer+4] == b'RSDS' and text_hash:
                                return data[pointer+4:pointer+24].hex() + ':' + text_hash
        except (struct.error, IndexError):
            pass
    return digest(path)


def same_binary(left, right, platform):
    return binary_id(left, platform) == binary_id(right, platform)


def qt_owners(binaries, prefix, platform):
    index = file_index(prefix)
    owners = {}
    for binary in binaries:
        candidates = index.get(binary.name.lower(), [])
        for candidate in candidates:
            relative = candidate.relative_to(prefix)
            is_qt = (re.match(r'(?:lib)?Qt6', candidate.name) or
                     any(p.startswith('Qt') and p.endswith('.framework') for p in relative.parts) or
                     (relative.parts and relative.parts[0] == 'plugins'))
            if not is_qt:
                continue
            if same_binary(binary, candidate, platform):
                owners[binary] = 'qt'
                break
    return owners


def qt_vendor_sources(collection, prefix, binaries, owners):
    """ICU may be supplied by the official Linux Qt kit, outside system dpkg."""
    index = file_index(prefix / 'lib')
    identified = {}
    for binary in binaries:
        if binary in owners or not binary.name.startswith('libicu'):
            continue
        for candidate in index.get(binary.name.lower(), []):
            match = re.search(r'\.so\.(\d+)\.(\d+)$', candidate.resolve().name)
            if match and same_binary(binary, candidate, collection.platform):
                version = match[1] + '.' + match[2]
                identified.setdefault(version, []).append(binary)
                break
    for version, paths in identified.items():
        component = collection.component('qt-kit-icu-' + version, version, 'official-qt-kit-icu')
        try:
            major = int(version.split('.')[0])
            tag = 'release-' + (version.replace('.', '-') if major < 78 else version)
            filename = 'icu4c-' + (version.replace('.', '_') if major < 78 else version) + '-src.tgz'
            base = 'https://github.com/unicode-org/icu/releases/download/' + tag + '/'
            sums = read_url(base + 'SHASUM512.txt').decode()
            expected = next((line.split()[0] for line in sums.splitlines()
                             if len(line.split()) == 2 and line.split()[1].lstrip('*') == filename), None)
            if not expected:
                raise MaterialError('ICU release checksum missing')
            collection.save_record(component, 'SHASUM512.txt', sums.encode())
            collection.source(component, base+filename, 'sha512', expected.lower(), filename)
            for binary in paths:
                owners[binary] = component['id']
        except (OSError, ValueError, MaterialError) as error:
            collection.error(component['id'], str(error) if isinstance(error,MaterialError) else 'ICU metadata/download unavailable')


def parse_control(text):
    records = []
    for paragraph in re.split(r'\n\s*\n', text.strip()):
        record, current = {}, None
        for line in paragraph.splitlines():
            if line.startswith((' ', '\t')) and current:
                record[current] += '\n' + line[1:]
            elif ': ' in line:
                current, value = line.split(': ', 1)
                record[current] = value
            elif line.endswith(':'):
                current = line[:-1]
                record[current] = ''
        if record:
            records.append(record)
    return records


def spdx_url(resource, version):
    url = resource.get('downloadLocation', '').replace('${VERSION}', version)
    if url.startswith('git+https://github.com/'):
        match = re.fullmatch(r'git\+(https://github.com/[^/@]+/[^/@]+)@([^#]+)', url)
        if not match:
            raise MaterialError('SPDX Git source has no fixed revision')
        repo = match[1].removesuffix('.git')
        url = repo + '/archive/' + urllib.parse.quote(match[2], safe='') + '.tar.gz'
    # Other Git/SVN sources need an explicit, complete source archive, including
    # submodules when applicable; never treat a branch URL as a source archive.
    if not url.startswith('https://') or '${' in url:
        raise MaterialError('SPDX source cannot be resolved automatically; supply a pinned archive')
    if url.startswith('https://ftpmirror.gnu.org/'):
        url = url.replace('https://ftpmirror.gnu.org/', 'https://ftp.gnu.org/gnu/', 1)
    return url


def vcpkg_recipe(collection, component, sbom, root, port):
    port_ids = {r.get('relatedSpdxElement') for r in sbom.get('relationships', [])
                if r.get('spdxElementId') == 'SPDXRef-port' and r.get('relationshipType') == 'CONTAINS'}
    files = [f for f in sbom.get('files', []) if
             (f.get('SPDXID') in port_ids if port_ids else not f.get('SPDXID','').startswith('SPDXRef-binary-file-'))]
    if not files:
        raise MaterialError('Installed SPDX has no recipe-file hashes')
    desired = []
    for item in files:
        rel = item['fileName'].replace('\\', '/').removeprefix('./')
        marker = f'ports/{port}/'
        if marker in rel:
            rel = rel.split(marker, 1)[1]
        member_path(rel)
        algorithm, value = checksum_value(item.get('checksums', []))
        desired.append((rel, algorithm, value))
    # Current checkout first; old installed ports may still be in local Git history.
    candidates = [None]
    if (root / '.git').exists():
        candidates += run(['git', '-C', root, 'log', '-n', '100', '--format=%H', '--', f'ports/{port}']).splitlines()
    for commit in candidates:
        payload = {}
        for rel, algorithm, expected in desired:
            try:
                if commit:
                    import subprocess
                    result = subprocess.run(['git', '-C', str(root), 'show', f'{commit}:ports/{port}/{rel}'],
                                            capture_output=True, timeout=30)
                    if result.returncode:
                        break
                    data = result.stdout
                else:
                    data = (root / 'ports' / port / rel).read_bytes()
                import hashlib
                if hashlib.new(algorithm, data).hexdigest() != expected:
                    break
                payload[rel] = data
            except OSError:
                break
        else:
            for rel, data in payload.items():
                collection.save_record(component, rel, data)
            component['recipe_revision'] = commit or 'installed SPDX hashes match local port'
            return payload
    raise MaterialError('Cannot recover recipe/patches matching installed SPDX; restore the original vcpkg checkout or supply exact recipes')


def vcpkg_blas_metadata(prefix, db, records, port, resources, recipe):
    """Recognize the verified BLAS routing port, never a missing binary library.

    The official port has no independent upstream archive or installed copyright.
    Its CMake/pkg-config files remain covered by the vcpkg repository license.
    """
    if port != 'blas' or resources:
        return False
    manifest = json.loads(recipe.get('vcpkg.json', b'{}'))
    cmake = re.sub(r'#[^\n]*', '', recipe.get('portfile.cmake', b'').decode('utf-8'))
    if (manifest.get('name') != 'blas' or 'license' not in manifest or manifest['license'] is not None or
            not re.search(r'\bset\s*\(\s*VCPKG_POLICY_EMPTY_PACKAGE\s+enabled\s*\)', cmake, re.I)):
        return False
    dependencies = {v.strip().split(':', 1)[0].split(' ', 1)[0]
                    for v in records[port].get('Depends', '').split(',')}
    if 'openblas' not in dependencies or 'openblas' not in records:
        raise MaterialError('BLAS metadata port has no installed OpenBLAS provider for this triplet')
    listings = list((db / 'info').glob(f'{port}_*_{prefix.name}.list'))
    if len(listings) != 1:
        raise MaterialError('BLAS metadata port requires one unambiguous installed file list')
    files = []
    share_files = {'vcpkg.spdx.json', 'vcpkg_abi_info.txt', 'vcpkg-cmake-wrapper.cmake', 'usage', 'copyright'}
    for line in listings[0].read_text().splitlines():
        if not line or line.endswith('/'):
            continue
        rel = member_path(line)
        if not rel.parts or rel.parts[0] != prefix.name:
            raise MaterialError('BLAS installed file list contains another triplet')
        local = PurePosixPath(*rel.parts[1:])
        allowed = (local.parent == PurePosixPath('share/blas') and local.name in share_files) or str(local) in {
            'lib/pkgconfig/blas.pc', 'debug/lib/pkgconfig/blas.pc'}
        path = prefix.joinpath(*local.parts)
        if not allowed or not path.is_file() or path.is_symlink():
            raise MaterialError('BLAS port contains missing or non-metadata files; cannot treat it as a routing port')
        files.append((local.as_posix(), path.read_bytes()))
    if not files:
        raise MaterialError('BLAS installed file list is empty')
    return files


def vcpkg_sources(collection, prefix, root, binaries, owners):
    prefix, root = Path(prefix).resolve(), Path(root).resolve()
    db = prefix.parent / 'vcpkg'
    status = db / 'status'
    if not status.is_file():
        raise MaterialError('vcpkg installed database not found next to the triplet directory')
    collection.source_caches = [root / 'downloads']
    if os.environ.get('VCPKG_DOWNLOADS'):
        collection.source_caches.insert(0, Path(os.environ['VCPKG_DOWNLOADS']))
    all_records = parse_control(status.read_text())
    records = {r['Package']:r for r in all_records
               if r.get('Architecture') == prefix.name and r.get('Status') == 'install ok installed' and 'Feature' not in r}
    selected = {p for p in ('gsl', 'openblas', 'lapack', 'lapack-reference') if p in records}
    if not {'gsl', 'openblas'} <= selected:
        raise MaterialError('Installed gsl/openblas records are missing for this triplet')
    # Ownership comes from vcpkg's installed file lists, not a guessed DLL name.
    index = {}
    for port in records:
        for listing in (db / 'info').glob(f'{port}_*_{prefix.name}.list'):
            for line in listing.read_text().splitlines():
                if line.endswith('/'):
                    continue
                rel = member_path(line)
                path = prefix.parent.joinpath(*rel.parts)
                if path.suffix.lower() == '.dll' and path.is_file():
                    index.setdefault(path.name.lower(), []).append((port, path))
    for binary in binaries:
        if binary in owners:
            continue
        for port, candidate in index.get(binary.name.lower(), []):
            if digest(binary) == digest(candidate):
                owners[binary] = 'vcpkg-' + port
                selected.add(port)
                break
    # Include the installed dependency closure, including statically linked ports.
    pending = list(selected)
    while pending:
        port = pending.pop()
        depends = ','.join(r.get('Depends','') for r in all_records
                           if r.get('Package') == port and r.get('Architecture') == prefix.name
                           and r.get('Status') == 'install ok installed')
        for value in depends.split(','):
            dependency = value.strip().split(':',1)[0].split(' ',1)[0]
            if dependency and dependency in records and dependency not in selected:
                selected.add(dependency)
                pending.append(dependency)
    for port in sorted(selected):
        component = collection.component('vcpkg-' + port, records[port].get('Version', ''), 'vcpkg')
        share = prefix / 'share' / port
        try:
            sbom = json.loads((share / 'vcpkg.spdx.json').read_text(encoding='utf-8'))
            port_metadata = next((x for x in sbom.get('packages', []) if x.get('SPDXID') == 'SPDXRef-port'), {})
            if port_metadata.get('versionInfo','').split('#')[0] != component['version'].split('#')[0]:
                raise MaterialError('Installed status and SPDX versions disagree')
            collection.save_record(component, 'vcpkg.spdx.json', json.dumps(sbom, indent=2).encode())
            installed_records = [r for r in all_records if r.get('Package') == port
                                 and r.get('Architecture') == prefix.name
                                 and r.get('Status') == 'install ok installed']
            collection.save_record(component, 'installed.json', json.dumps(installed_records, indent=2).encode())
            component['features'] = sorted(r['Feature'] for r in installed_records if 'Feature' in r)
            triplet = root / 'triplets' / (prefix.name + '.cmake')
            if not triplet.is_file():
                triplet = root / 'triplets/community' / (prefix.name + '.cmake')
            if triplet.is_file():
                collection.save_record(component, 'triplet-' + prefix.name + '.cmake', triplet.read_bytes())
            else:
                collection.error(component['id'], 'Triplet recipe missing; supply the build triplet in the supplement')
            recipe = vcpkg_recipe(collection, component, sbom, root, port)
            resources = [x for x in sbom.get('packages', []) if x.get('SPDXID','').startswith('SPDXRef-resource')]
            metadata = vcpkg_blas_metadata(prefix, db, records, port, resources, recipe)
            if metadata:
                if component['id'] in owners.values():
                    raise MaterialError('BLAS routing port unexpectedly owns a deployed binary')
                collection.save_record(component, 'vcpkg-LICENSE.txt', (root / 'LICENSE.txt').read_bytes(), 'licenses')
                component.update(kind='metadata-only-routing-port', implementation=['vcpkg-openblas'],
                                 license_scope='vcpkg recipe and installed CMake/pkg-config metadata; numerical implementation is OpenBLAS')
                for relative, data in metadata:
                    collection.save_record(component, 'installed/' + relative, data)
                if (share / 'copyright').is_file():
                    collection.save_record(component, 'copyright', (share / 'copyright').read_bytes(), 'licenses')
                continue
            collection.save_record(component, 'copyright', (share / 'copyright').read_bytes(), 'licenses')
            # Header-only vcpkg helper ports have their complete source in the port.
            if not resources and not port.startswith('vcpkg-'):
                raise MaterialError('SPDX contains no source resources; add exact source archives through --supplement')
            for resource in resources:
                algorithm, expected = checksum_value(resource.get('checksums', []))
                url = spdx_url(resource, component['version'].split('#')[0])
                suffix = ''.join(PurePosixPath(urllib.parse.urlsplit(url).path).suffixes)
                # Retain only archive extensions; version dots are not suffixes.
                ext = next((e for e in ('.tar.gz','.tar.xz','.tar.bz2','.tgz','.zip','.patch','.diff') if suffix.endswith(e)), '.source')
                filename = f'{safe_name(port)}-{expected[:16]}{ext}'
                collection.source(component, url, algorithm, expected, filename,
                                  extract=ext not in {'.patch','.diff','.source'})
        except FileNotFoundError as error:
            collection.error(component['id'], 'Required installed metadata file is missing: ' + Path(error.filename or 'unknown').name)
        except (OSError, ValueError, KeyError, MaterialError) as error:
            collection.error(component['id'], str(error) if isinstance(error, MaterialError) else 'Installed SPDX/license metadata is incomplete')
    return owners


def dpkg_owner(path):
    for candidate in (path, path.resolve()):
        try:
            text = run(['dpkg-query', '-S', candidate])
            for line in text.splitlines():
                if ': ' in line and not line.startswith('diversion'):
                    return line.split(': ', 1)[0].split(', ')[0]
        except MaterialError:
            pass
    raise MaterialError('No installed Debian package owns this runtime library')


def debian_sources(collection, binaries, owners):
    # linuxdeploy changes RPATH but preserves GNU build IDs.
    library_index = {}
    text = run(['ldconfig', '-p'])
    for line in text.splitlines():
        if ' => ' in line:
            candidate = Path(line.split(' => ', 1)[1].strip())
            if candidate.is_file():
                library_index.setdefault(candidate.name, []).append(candidate)
                library_index.setdefault(candidate.resolve().name, []).append(candidate)
    packages = set()
    for binary in binaries:
        if binary in owners:
            continue
        for candidate in library_index.get(binary.name, []):
            if same_binary(binary, candidate, collection.platform):
                package = dpkg_owner(candidate)
                packages.add(package)
                owners[binary] = 'deb-' + package.split(':')[0]
                break
    grouped = {}
    for package in sorted(packages):
        metadata = run(['dpkg-query', '-W', '-f=${Package}\t${Version}\t${source:Package}\t${source:Version}\t${Architecture}', package]).split('\t')
        name, version, source, source_version, arch = metadata
        source, source_version = source or name, source_version or version
        component = collection.component('deb-' + name, version, 'dpkg')
        component.update(source_package=source, source_version=source_version, architecture=arch)
        grouped.setdefault((source, source_version), []).append(component)
        try:
            collection.save_record(component, 'copyright', (Path('/usr/share/doc') / name / 'copyright').read_bytes(), 'licenses')
        except OSError:
            collection.error(component['id'], 'Installed copyright file is missing')
    for (source, version), components in grouped.items():
        component = components[0]
        try:
            args = ['apt-get', '-qq', '--print-uris', '--download-only', '--only-source', 'source', source + '=' + version]
            uris = run(args)
            locations = {}
            for line in uris.splitlines():
                match = re.match(r"'([^']+)'\s+(\S+)\s+(\d+)\s+(\S+)", line)
                if match:
                    locations[match[2]] = match[1]
            if not locations:
                raise MaterialError('No exact Debian source package available; enable matching deb-src repositories and apt-get update')
            if collection.plan_only:
                component['sources'] = [{'url':url, 'asset':name, 'status':'planned-apt-verified-download'} for name,url in locations.items()]
                continue
            destination = collection.cache / 'debian' / safe_name(source + '-' + version)
            destination.mkdir(parents=True, exist_ok=True)
            run(['apt-get', '-qq', '--download-only', '--only-source', 'source', source + '=' + version], cwd=destination)
            dsc_files = list(destination.glob('*.dsc'))
            if len(dsc_files) != 1:
                raise MaterialError('Expected one .dsc file for the exact source version')
            control = parse_control(dsc_files[0].read_text())
            record = next((x for x in control if 'Checksums-Sha256' in x), {})
            if record.get('Source') != source or record.get('Version') != version:
                raise MaterialError('Downloaded .dsc does not match installed source version')
            required = {dsc_files[0].name:digest(dsc_files[0])}
            for line in record.get('Checksums-Sha256', '').splitlines():
                parts = line.split()
                if len(parts) == 3:
                    required[parts[2]] = parts[0]
            if len(required) < 2:
                raise MaterialError('.dsc contains no source checksums')
            for name, expected in required.items():
                member_path(name)
                path = destination / name
                if digest(path) != expected:
                    raise MaterialError('Debian source checksum mismatch')
                target = collection.assets / name
                if target.exists() and digest(target) != expected:
                    raise MaterialError('Source filename conflict')
                shutil.copy2(path, target)
                component['sources'].append({'url':locations.get(name, ''), 'asset':name,
                    'sha256':expected, 'bytes':path.stat().st_size, 'status':'verified-by-apt-and-dsc'})
            for other in components[1:]:
                other['sources'] = list(component['sources'])
        except (OSError, ValueError, MaterialError) as error:
            collection.error(component['id'], str(error) if isinstance(error, MaterialError) else 'Debian source collection failed')
    return owners


BREW_QUERY = r'''
require "json"
require "formula"
require "formulary"
f = Formulary.factory(ARGV.fetch(0))
s = f.stable
raise "No stable source" unless s
sources = [{url: s.url, sha256: s.checksum.to_s, kind: "source"}]
s.resources.each_value { |r| sources << {url: r.url, sha256: r.checksum.to_s, kind: "resource"} }
s.patches.each do |p|
  if p.respond_to?(:resource)
    r = p.resource
    sources << {url: r.url, sha256: r.checksum.to_s, kind: "patch"}
  elsif !p.class.name.match?(/DATA|String/)
    raise "Unsupported patch: #{p.class.name}"
  end
end
puts JSON.generate({name: f.name, version: f.pkg_version.to_s, sources: sources})
'''


def brew_sources(collection, binaries, owners):
    cellar = Path(run(['brew', '--cellar'])).resolve()
    index = {}
    for candidate in cellar.rglob('*.dylib'):
        if candidate.is_file():
            index.setdefault(candidate.name, []).append(candidate)
    kegs = set()
    for binary in binaries:
        if binary in owners:
            continue
        for candidate in index.get(binary.name, []):
            if same_binary(binary, candidate, collection.platform):
                rel = candidate.resolve().relative_to(cellar)
                keg = cellar / rel.parts[0] / rel.parts[1]
                kegs.add(keg)
                owners[binary] = 'brew-' + rel.parts[0]
                break
    # Follow the installed receipt rather than today's formula dependency list.
    pending = list(kegs)
    while pending:
        keg = pending.pop()
        receipt = json.loads((keg / 'INSTALL_RECEIPT.json').read_text())
        for item in receipt.get('runtime_dependencies', []):
            name = item['full_name'].split('/')[-1]
            dependency_version = item.get('pkg_version') or (str(item['version']) +
                ('_' + str(item['revision']) if item.get('revision') else ''))
            dep = cellar / name / dependency_version
            if dep not in kegs:
                if not dep.is_dir():
                    raise MaterialError('An exact Homebrew receipt dependency was removed; restore it before collecting sources')
                kegs.add(dep)
                pending.append(dep)
    for keg in sorted(kegs):
        name = keg.parent.name
        component = collection.component('brew-' + name, keg.name, 'homebrew-installed-keg')
        try:
            recipe = keg / '.brew' / (name + '.rb')
            if not recipe.is_file():
                raise MaterialError('Installed Homebrew formula is missing')
            receipt = json.loads((keg / 'INSTALL_RECEIPT.json').read_text())
            # Omit local source paths; retain versions, bottle identity and build options.
            receipt = {k:v for k,v in receipt.items() if k in {'used_options','unused_options','built_as_bottle',
                       'poured_from_bottle','compiler','stdlib','runtime_dependencies','built_on','time'}}
            collection.save_record(component, 'INSTALL_RECEIPT.json', json.dumps(receipt, indent=2).encode())
            collection.save_record(component, name + '.rb', recipe.read_bytes())
            metadata = json.loads(run(['brew', 'ruby', '-e', BREW_QUERY, '--', recipe]))
            if metadata['version'] != keg.name:
                raise MaterialError('Formula version differs from the installed keg')
            collection.save_record(component, 'source-metadata.json', json.dumps(metadata, indent=2).encode())
            for source in metadata['sources']:
                url, expected = source['url'], source['sha256']
                ext = next((e for e in ('.tar.gz','.tar.xz','.tar.bz2','.tgz','.zip') if urllib.parse.urlsplit(url).path.endswith(e)), '.source')
                filename = f'{safe_name(name)}-{expected[:16]}{ext}'
                collection.source(component, url, 'sha256', expected, filename, extract=ext != '.source')
            if not component['notices'] and not collection.plan_only:
                raise MaterialError('No license files found in the Homebrew source archives')
        except (OSError, ValueError, KeyError, MaterialError) as error:
            collection.error(component['id'], str(error) if isinstance(error, MaterialError) else 'Homebrew source/receipt metadata is incomplete')
    return owners
