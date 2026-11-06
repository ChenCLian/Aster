"""Verified downloads and source/notice records for release preparation.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import hashlib
import http.client
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import tarfile
import time
import urllib.parse
import urllib.request
import zipfile

from dependency_mirrors import candidate_urls


class MaterialError(RuntimeError):
    pass


def run(args, cwd=None):
    env = dict(os.environ, LC_ALL='C', LANG='C', HOMEBREW_NO_AUTO_UPDATE='1')
    result = subprocess.run([str(x) for x in args], cwd=cwd, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            encoding='utf-8', errors='replace', timeout=1800)
    if result.returncode:
        # Detailed native output stays in the local console, never in public manifests.
        print(result.stderr.strip(), flush=True)
        raise MaterialError(f'{Path(str(args[0])).name} failed (exit {result.returncode})')
    return result.stdout.strip()


def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm.lower().replace('-', ''))
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def safe_name(value):
    value = re.sub(r'[^A-Za-z0-9._+-]', '_', str(value))
    if not value or value in {'.', '..'}:
        raise MaterialError('Invalid component name')
    return value


def member_path(name):
    p = PurePosixPath(name.replace('\\', '/'))
    if p.is_absolute() or '..' in p.parts or any(':' in x for x in p.parts):
        raise MaterialError('Unsafe archive member')
    return p


def check_url(url):
    p = urllib.parse.urlsplit(url)
    if p.scheme != 'https' or not p.hostname or p.username or p.password:
        raise MaterialError('Source downloads require HTTPS URLs without credentials')
    if '${' in url or p.fragment:
        raise MaterialError('Unresolved source URL in build metadata')


class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def open_url(url):
    check_url(url)
    return urllib.request.build_opener(HTTPSRedirect()).open(
        urllib.request.Request(url, headers={'User-Agent': 'Aster-release-materials/1'}), timeout=20)


def network_error(error):
    if isinstance(error, urllib.error.HTTPError):
        return f'HTTP {error.code}'
    if isinstance(error, urllib.error.URLError):
        error = error.reason
    if isinstance(error, TimeoutError):
        return 'connection/read timed out'
    # Keep native exception details in the local log, not paths/proxy credentials
    # in a public report. HTTP status and exception type remain actionable.
    return type(error).__name__


def read_url(url, limit=8 * 1024 * 1024, validator=None):
    check_url(url)
    failures = []
    for candidate in candidate_urls(url):
        try:
            with open_url(candidate) as response:
                data = response.read(limit + 1)
            if len(data) > limit:
                raise MaterialError('Metadata exceeds the size limit')
            if data.lstrip().lower().startswith((b'<!doctype html', b'<html')):
                raise MaterialError('Received an HTML page instead of source metadata')
            if validator:
                validator(data)
            return data
        except (OSError, urllib.error.URLError, http.client.HTTPException, MaterialError) as error:
            reason = str(error) if isinstance(error, MaterialError) else network_error(error)
            failures.append(f'{urllib.parse.urlsplit(candidate).hostname}: {reason}')
            print(f'  metadata unavailable: {candidate} ({reason})', flush=True)
    raise MaterialError('Metadata download failed: ' + '; '.join(failures))


def checksum_value(checksums):
    for algorithm in ('SHA512', 'SHA256', 'SHA1'):
        for item in checksums:
            if item.get('algorithm', '').replace('-', '').upper() == algorithm:
                value = item.get('checksumValue', '').lower()
                if re.fullmatch('[0-9a-f]{%d}' % {'SHA512':128, 'SHA256':64, 'SHA1':40}[algorithm], value):
                    return algorithm.lower(), value
    raise MaterialError('Build metadata has no supported checksum')


def download(url, cache, algorithm, expected, *, local_dirs=(), retrieval=None):
    check_url(url)
    if algorithm not in {'sha256', 'sha512'} or not re.fullmatch(
            '[0-9a-f]{%d}' % {'sha256':64, 'sha512':128}[algorithm], expected):
        raise MaterialError('Source archive requires a SHA-256 or SHA-512 checksum')
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / (algorithm + '-' + expected)
    if target.is_file() and digest(target, algorithm) == expected:
        if retrieval is not None:
            retrieval['retrieved_via'] = 'verified-download-cache'
        return target
    partial = target.with_suffix('.partial')
    # A vcpkg build often already downloaded the exact source. Never trust its
    # filename alone; validate against the installed SPDX digest before reuse.
    for directory in local_dirs:
        directory = Path(directory)
        if not directory.is_dir():
            continue
        try:
            local_files = sorted(directory.iterdir())
        except OSError:
            continue
        for local in local_files:
            if (not local.is_file() or local.is_symlink() or
                    not local.name.lower().endswith(('.tar.gz', '.tar.xz', '.tar.bz2', '.tgz', '.zip', '.patch', '.diff'))):
                continue
            try:
                if digest(local, algorithm) != expected:
                    continue
                shutil.copyfile(local, partial)
                if digest(partial, algorithm) != expected:
                    partial.unlink(missing_ok=True)
                    continue
                partial.replace(target)
                print('  reused verified local dependency source', flush=True)
                if retrieval is not None:
                    retrieval['retrieved_via'] = 'verified-vcpkg-download-cache'
                return target
            except OSError:
                partial.unlink(missing_ok=True)
    candidates = candidate_urls(url)
    attempts = candidates if len(candidates) > 1 else candidates * 3
    failures = []
    for candidate in attempts:
        try:
            print(f'  downloading: {candidate}', flush=True)
            with open_url(candidate) as response, partial.open('wb') as stream:
                total = 0
                last = time.monotonic()
                while True:
                    block = response.read(1024 * 1024)
                    if not block:
                        break
                    stream.write(block)
                    total += len(block)
                    if total > 4 * 1024**3:
                        raise MaterialError('Archive exceeds the 4 GiB download limit')
                    if time.monotonic() - last > 15:
                        print(f'  downloaded {total // (1024**2)} MiB', flush=True)
                        last = time.monotonic()
            if digest(partial, algorithm) != expected:
                raise MaterialError('Source checksum mismatch; no files were installed')
            partial.replace(target)
            if retrieval is not None:
                retrieval.update(retrieved_via='https', download_url=candidate)
            return target
        except (MaterialError, OSError, urllib.error.URLError, http.client.HTTPException) as error:
            partial.unlink(missing_ok=True)
            reason = str(error) if isinstance(error, MaterialError) else network_error(error)
            failures.append(f'{urllib.parse.urlsplit(candidate).hostname}: {reason}')
            print(f'  download rejected/unavailable: {reason}', flush=True)
            if isinstance(error, MaterialError) and len(candidates) == 1:
                raise
    raise MaterialError('Download failed: ' + '; '.join(failures))


def iter_archive(path):
    """Yield regular members only. Never extract or execute archived code."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for item in archive.infolist():
                member_path(item.filename)
                if not item.is_dir() and (item.external_attr >> 16) & 0o170000 != 0o120000:
                    yield item.filename, item.file_size, lambda item=item: archive.open(item)
    else:
        with tarfile.open(path, 'r|*') as archive:
            for item in archive:
                member_path(item.name)
                if item.isfile():
                    yield item.name, item.size, lambda item=item: archive.extractfile(item)


def notice_name(name):
    p = PurePosixPath(name)
    n = p.name.lower()
    return (n.startswith(('license', 'licence', 'copying', 'copyright', 'notice', 'authors'))
            or n in {'qt_attribution.json', 'qt_attributions.json'}
            or any(x.lower() in {'licenses', 'licences'} for x in p.parts[:-1]))


def extract_notices(archive_path, destination):
    destination = Path(destination)
    requested = set()
    count = 0
    # Qt's attribution records sometimes point to unusually named license files.
    for name, size, opener in iter_archive(archive_path):
        if not notice_name(name):
            continue
        if size > 16 * 1024**2:
            raise MaterialError('License member exceeds the size limit')
        with opener() as stream:
            data = stream.read()
        target = destination.joinpath(*member_path(name).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        count += 1
        if PurePosixPath(name).name in {'qt_attribution.json', 'qt_attributions.json'}:
            # Official Qt 6 archives contain literal newlines in attribution
            # descriptions. Preserve the original bytes and tolerate only these
            # string control characters while parsing the license references.
            records = json.loads(data, strict=False)
            for item in records if isinstance(records, list) else [records]:
                values = item.get('LicenseFile', [])
                if isinstance(values, str):
                    values = [values]
                for value in values:
                    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), value))
                    member_path(resolved)
                    requested.add(resolved)
    if requested:
        for name, size, opener in iter_archive(archive_path):
            if name in requested:
                if size > 16 * 1024**2:
                    raise MaterialError('License member exceeds the size limit')
                target = destination.joinpath(*member_path(name).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                with opener() as stream:
                    target.write_bytes(stream.read())
                requested.remove(name)
                count += 1
        # Some Qt records describe optional sources omitted from upstream archives.
        # Keep the records; do not silently claim those referenced licenses exist.
        if requested:
            write_json(destination / 'unresolved-attribution-files.json', sorted(requested))
            raise MaterialError('Qt attribution references missing license files; see unresolved-attribution-files.json')
    return count


def is_binary(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        magic = stream.read(4)
    return magic.startswith(b'MZ') or magic in {
        b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
        b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf'}


class Collection:
    def __init__(self, output, cache, platform, version, plan_only=False):
        self.output, self.cache = Path(output), Path(cache)
        self.platform, self.version, self.plan_only = platform, version, plan_only
        self.assets = self.output / 'release-assets'
        self.records = self.output / 'records'
        self.licenses = self.records / 'licenses'
        self.assets.mkdir(parents=True, exist_ok=True)
        self.licenses.mkdir(parents=True, exist_ok=True)
        self.components, self.inventory, self.errors = [], [], []
        self.source_caches = []

    def component(self, name, version, provider):
        if any(c['id'] == safe_name(name) and c['version'] != str(version) for c in self.components):
            raise MaterialError('Multiple installed versions share one component identity; resolve the runtime dependency set')
        item = {'id':safe_name(name), 'version':str(version), 'provider':provider,
                'sources':[], 'notices':[], 'recipes':[]}
        self.components.append(item)
        return item

    def error(self, context, message):
        self.errors.append({'component':context, 'message':str(message)})
        print(f'INCOMPLETE [{context}]: {message}', flush=True)

    def save_record(self, component, relative, data, kind='recipes'):
        target = self.records / kind / component['id']
        target = target.joinpath(*member_path(relative).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        component[kind if kind != 'licenses' else 'notices'].append(target.relative_to(self.records).as_posix())

    def source(self, component, url, algorithm, expected, filename=None, extract=True):
        check_url(url)
        filename = filename or urllib.parse.unquote(PurePosixPath(urllib.parse.urlsplit(url).path).name)
        filename = safe_name(filename)
        record = {'url':url, 'checksum_algorithm':algorithm, 'expected_checksum':expected,
                  'asset':filename, 'status':'planned'}
        component['sources'].append(record)
        if self.plan_only:
            return None
        print(f"Collecting {component['id']} {component['version']}: {filename}", flush=True)
        cached = download(url, self.cache, algorithm, expected,
                          local_dirs=self.source_caches, retrieval=record)
        target = self.assets / filename
        if target.exists() and digest(target, algorithm) != expected:
            raise MaterialError('Two source archives have the same filename and different contents')
        if not target.exists():
            shutil.copy2(cached, target)
        record.update(status='verified', sha256=digest(target), bytes=target.stat().st_size)
        if extract:
            try:
                count = extract_notices(target, self.licenses / component['id'])
            except (tarfile.TarError, zipfile.BadZipFile, ValueError, TypeError) as error:
                raise MaterialError('Cannot read source archive or attribution metadata') from error
            if count:
                component['notices'].append('licenses/' + component['id'])
        return target
