"""Offline regression tests for mirror fallback and verified metadata-only ports.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import hashlib
import http.client
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependency_common as common
import dependency_providers as providers
from dependency_mirrors import candidate_urls, QT_SOURCE_SHA256
from test_dependency_materials import sha, tar


class MirrorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.url = 'https://github.com/owner/repo/archive/v1.0.tar.gz'

    def tearDown(self):
        self.temp.cleanup()

    def test_routes_preserve_versions_and_original_origin(self):
        for url, first in [
            (self.url, 'https://ghfast.top/'),
            ('https://ftp.gnu.org/gnu/gsl/gsl-2.8.tar.gz', 'https://mirrors.tuna.tsinghua.edu.cn/gnu/gsl/'),
            ('https://ftpmirror.gnu.org/gsl/gsl-2.8.tar.gz', 'https://mirrors.tuna.tsinghua.edu.cn/gnu/gsl/'),
            ('https://download.qt.io/archive/qt/6.12/6.12.0/submodules/qtbase-everywhere-src-6.12.0.tar.xz', 'https://mirrors.tuna.tsinghua.edu.cn/qt/')]:
            candidates = candidate_urls(url)
            self.assertTrue(candidates[0].startswith(first))
            self.assertEqual(candidates[-1], url)
            self.assertEqual(len(candidates), len(set(candidates)))
            self.assertTrue(all(u.endswith(url.rsplit('/', 1)[-1]) for u in candidates))

    def test_private_or_unrelated_urls_are_not_forwarded(self):
        for url in [self.url+'?token=private', self.url+'#fragment',
                    self.url.replace('github.com', 'github.com.evil.example'),
                    self.url.replace('github.com', 'github.com:444'),
                    self.url.replace('github.com', 'user:secret@github.com'),
                    'https://github.com/owner/repo/issues/1', 'https://example.org/source.tar.gz']:
            self.assertEqual(candidate_urls(url), [url])

    def test_gnu_prefixed_mirror_urls_do_not_duplicate_gnu(self):
        for package in ('gcc/gcc-16.2.0/gcc-16.2.0.tar.xz', 'gmp/gmp-6.3.0.tar.xz',
                        'mpc/mpc-1.4.1.tar.gz'):
            for host in ('ftp.gnu.org', 'ftpmirror.gnu.org'):
                url = 'https://' + host + '/gnu/' + package
                candidates = candidate_urls(url)
                self.assertEqual(candidates[:2], [
                    'https://mirrors.tuna.tsinghua.edu.cn/gnu/' + package,
                    'https://mirrors.ustc.edu.cn/gnu/' + package])
                self.assertEqual(candidates[-1], url)
                self.assertFalse(any('/gnu/gnu/' in item for item in candidates))

    def test_gnu_prefix_does_not_rewrite_nonstandard_endpoints(self):
        for url in ('https://ftpmirror.gnu.org:444/gnu/gcc/gcc.tar.xz',
                    'https://ftpmirror.gnu.org.evil.example/gnu/gcc/gcc.tar.xz',
                    'https://ftpmirror.gnu.org/gnu/gcc/gcc.tar.xz?token=private'):
            self.assertEqual(candidate_urls(url), [url])

    def test_404_falls_back_to_next_mirror(self):
        data = b'original archive'
        with patch.object(common, 'open_url', side_effect=[urllib.error.HTTPError('url', 404, 'Not Found', {}, None), io.BytesIO(data)]) as opener:
            record = {}
            target = common.download(self.url, self.base, 'sha256', sha(data), retrieval=record)
        self.assertEqual(target.read_bytes(), data)
        self.assertEqual(opener.call_count, 2)
        self.assertEqual(record['download_url'], candidate_urls(self.url)[1])

    def test_bad_mirror_checksum_never_replaces_expected_hash(self):
        data = b'exact expected archive'
        with patch.object(common, 'open_url', side_effect=[io.BytesIO(b'changed'), io.BytesIO(b'<html>error'), io.BytesIO(data)]) as opener:
            target = common.download(self.url, self.base, 'sha256', sha(data))
        self.assertEqual(opener.call_count, 3)
        self.assertEqual(target.read_bytes(), data)
        self.assertFalse(list(self.base.glob('*.partial')))

    def test_all_mirrors_fail_without_publishing(self):
        with patch.object(common, 'open_url', side_effect=lambda _: io.BytesIO(b'incorrect')):
            with self.assertRaisesRegex(common.MaterialError, 'checksum mismatch'):
                common.download(self.url, self.base, 'sha256', sha(b'correct'))
        self.assertFalse(list(self.base.iterdir()))

    def test_truncated_response_falls_back(self):
        data = b'archive'
        with patch.object(common, 'open_url', side_effect=[http.client.IncompleteRead(b'partial'), io.BytesIO(data)]):
            self.assertEqual(common.download(self.url, self.base, 'sha256', sha(data)).read_bytes(), data)

    def test_vcpkg_download_cache_is_verified_and_used_offline(self):
        local = self.base/'vcpkg-downloads'; local.mkdir()
        data = b'cached archive'; (local/'arbitrary-name.tar.gz').write_bytes(data)
        expected = hashlib.sha512(data).hexdigest()
        record = {}
        with patch.object(common, 'open_url', side_effect=AssertionError('no network expected')):
            target = common.download(self.url, self.base/'cache', 'sha512', expected, local_dirs=[local], retrieval=record)
        self.assertEqual(target.read_bytes(), data)
        self.assertEqual(record['retrieved_via'], 'verified-vcpkg-download-cache')
        self.assertNotIn(str(self.base), json.dumps(record))

    def test_unmatched_local_source_does_not_override_spdx(self):
        local = self.base/'downloads'; local.mkdir()
        (local/'same-version.tar.gz').write_bytes(b'wrong archive')
        data = b'correct archive'
        with patch.object(common, 'open_url', return_value=io.BytesIO(data)) as opener:
            target = common.download(self.url, self.base/'cache', 'sha256', sha(data), local_dirs=[local])
        self.assertEqual(target.read_bytes(), data)
        self.assertEqual(opener.call_count, 1)

    def test_metadata_html_and_invalid_checksum_fall_back(self):
        url = 'https://github.com/owner/repo/releases/download/v1/SHA256SUMS'
        valid = (sha(b'data')+'  source.tar.gz').encode()
        with patch.object(common, 'open_url', side_effect=[io.BytesIO(b'<html>challenge</html>'), io.BytesIO(b'bad'), io.BytesIO(valid)]):
            self.assertEqual(common.read_url(url, validator=providers.qt_checksum), valid)

    def test_network_errors_retain_http_status(self):
        with patch.object(common, 'open_url', side_effect=urllib.error.HTTPError('url', 403, 'Forbidden', {}, None)):
            with self.assertRaisesRegex(common.MaterialError, 'HTTP 403'):
                common.download(self.url, self.base, 'sha256', sha(b'data'))

    def test_supported_qt_kits_do_not_need_online_checksum_endpoint(self):
        for version in ('6.12.0', '6.8.3'):
            collection = common.Collection(self.base/version, self.base/'cache', 'windows-x64', '1.1.1')
            def query(args):
                return version if args[-1] == 'QT_VERSION' else str(self.base/'Qt')
            with patch.object(providers, 'run', side_effect=query), patch.object(providers, 'read_url', side_effect=AssertionError('offline metadata expected')), patch.object(collection, 'source') as source:
                providers.qt_sources(collection, Path('qmake.exe'), [])
            self.assertEqual(collection.errors, [])
            self.assertEqual(source.call_count, 4)
            self.assertEqual({call.args[3] for call in source.call_args_list}, set(QT_SOURCE_SHA256[version].values()))

    def test_unlisted_qt_version_requires_its_own_checksum(self):
        collection = common.Collection(self.base/'materials', self.base/'cache', 'windows-x64', '1.1.1')
        def query(args):
            return '6.12.1' if args[-1] == 'QT_VERSION' else str(self.base/'Qt')
        with patch.object(providers, 'run', side_effect=query), patch.object(providers, 'read_url', side_effect=common.MaterialError('offline')), patch.object(collection, 'source') as source:
            providers.qt_sources(collection, Path('qmake.exe'), [])
        self.assertEqual(len(collection.errors), 4)
        source.assert_not_called()


class BlasMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.root = self.base/'vcpkg'
        self.prefix = self.root/'installed/x64-windows'
        self.db = self.root/'installed/vcpkg'
        (self.db/'info').mkdir(parents=True)
        (self.root/'triplets').mkdir()
        (self.root/'triplets/x64-windows.cmake').write_text('set(VCPKG_TARGET_ARCHITECTURE x64)')
        (self.root/'LICENSE.txt').write_bytes(b'vcpkg recipe license fixture')
        self.source = self.base/'source.tar.gz'
        tar(self.source, {'lib/LICENSE': b'upstream license', 'lib/code.c': b'source'})
        records = []
        for name in ('gsl', 'openblas', 'blas'):
            record = f'Package: {name}\nVersion: 1.0\nArchitecture: x64-windows\nStatus: install ok installed\n'
            record += 'Depends: blas\n' if name == 'gsl' else ('Depends: openblas\n' if name == 'blas' else '')
            records.append(record)
            port = self.root/'ports'/name; port.mkdir(parents=True)
            recipe = {'portfile.cmake': b'set(VCPKG_POLICY_EMPTY_PACKAGE enabled)\n' if name == 'blas' else b'recipe'}
            if name == 'blas':
                recipe['vcpkg.json'] = b'{"name":"blas","version":"1.0","license":null}'
            for filename, data in recipe.items(): (port/filename).write_bytes(data)
            share = self.prefix/'share'/name; share.mkdir(parents=True)
            packages = [{'SPDXID': 'SPDXRef-port', 'versionInfo': '1.0'}]
            if name != 'blas':
                (share/'copyright').write_bytes(b'upstream license')
                packages.append({'SPDXID':'SPDXRef-resource-0','downloadLocation':'https://example.org/src.tar.gz',
                                 'checksums':[{'algorithm':'SHA256','checksumValue':common.digest(self.source)}]})
            sbom = {'packages': packages, 'files': [{'fileName': filename, 'checksums': [{'algorithm':'SHA256','checksumValue':sha(data)}]} for filename,data in recipe.items()]}
            (share/'vcpkg.spdx.json').write_text(json.dumps(sbom))
            (self.db/'info'/f'{name}_1.0_x64-windows.list').write_text(f'x64-windows/share/{name}/vcpkg.spdx.json\n')
        (self.db/'status').write_text('\n'.join(records))
        self.collection = common.Collection(self.base/'materials', self.base/'cache', 'windows-x64', '1.1.1')

    def tearDown(self):
        self.temp.cleanup()

    def collect(self):
        with patch.object(common, 'download', return_value=self.source):
            providers.vcpkg_sources(self.collection, self.prefix, self.root, [], {})

    def test_verified_blas_routes_to_openblas_and_keeps_recipe_license(self):
        self.collect()
        self.assertEqual(self.collection.errors, [])
        item = next(c for c in self.collection.components if c['id'] == 'vcpkg-blas')
        self.assertEqual(item['kind'], 'metadata-only-routing-port')
        self.assertEqual(item['implementation'], ['vcpkg-openblas'])
        self.assertEqual(item['sources'], [])
        self.assertTrue(item['notices'] and item['recipes'])
        self.assertEqual((self.collection.records/'licenses/vcpkg-blas/vcpkg-LICENSE.txt').read_bytes(), b'vcpkg recipe license fixture')

    def test_missing_spdx_is_not_waived_for_blas(self):
        (self.prefix/'share/blas/vcpkg.spdx.json').unlink()
        self.collect()
        self.assertTrue(any('vcpkg.spdx.json' in e['message'] for e in self.collection.errors))

    def test_modified_recipe_is_not_accepted_as_metadata(self):
        (self.root/'ports/blas/portfile.cmake').write_text('changed recipe')
        self.collect()
        self.assertTrue(any('recipe/patches' in e['message'] for e in self.collection.errors))

    def test_blas_with_binary_payload_is_not_exempt(self):
        (self.prefix/'bin').mkdir()
        (self.prefix/'bin/blas.dll').write_bytes(b'MZbinary')
        with (self.db/'info/blas_1.0_x64-windows.list').open('a') as out: out.write('x64-windows/bin/blas.dll\n')
        self.collect()
        self.assertTrue(any('non-metadata files' in e['message'] for e in self.collection.errors))

    def test_missing_vcpkg_recipe_license_is_reported(self):
        (self.root/'LICENSE.txt').unlink()
        self.collect()
        self.assertTrue(any('LICENSE.txt' in e['message'] for e in self.collection.errors))

    def test_regular_library_missing_copyright_is_still_rejected(self):
        (self.prefix/'share/gsl/copyright').unlink()
        self.collect()
        self.assertTrue(any(e['component']=='vcpkg-gsl' and 'copyright' in e['message'] for e in self.collection.errors))


if __name__ == '__main__':
    unittest.main()
