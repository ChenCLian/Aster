"""Missing Homebrew API receipt revision: verified patch recovery regressions.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependency_common as common
import dependency_providers as providers


def sha(data):
    return hashlib.sha256(data).hexdigest()


class HomebrewPatchRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.keg = self.base / 'Cellar/gcc/16.2.0'
        self.recipe = self.keg / '.brew/gcc.rb'
        self.recipe.parent.mkdir(parents=True)
        self.recipe.write_bytes(b'class Gcc < Formula\n  url "https://example.org/gcc.tar.xz"\nend\n')
        self.remote = self.recipe.read_bytes().replace(
            b'  url ', b'  bottle do\n    sha256 "bottle"\n  end\n\n  url ')
        self.relative = 'Patches/gcc/gcc-16.2.0.diff'
        self.patch_data = b'diff --git a/a b/a\r\n-old\r\n+new\r\n'
        self.receipt = {'source': {'tap': 'homebrew/core', 'tap_git_head': None,
                                  'path': '/missing/api/formula/gcc.rb'},
                        'runtime_dependencies': [], 'poured_from_bottle': True}
        self.api = {'name': 'gcc', 'tap': 'homebrew/core', 'versions': {'stable': '16.2.0'},
                    'revision': 0, 'tap_git_head': 'b' * 40, 'ruby_source_path': 'Formula/g/gcc.rb',
                    'ruby_source_checksum': {'sha256': sha(self.remote)},
                    'local_patch_checksums': {self.relative: sha(self.patch_data)}}
        self.calls = []
        self.collection = common.Collection(self.base / 'materials', self.base / 'cache',
                                            'macos-arm64', '1.2.0-beta.5')

    def download(self, url):
        self.calls.append(url)
        if url == 'https://formulae.brew.sh/api/formula/gcc.json':
            return json.dumps(self.api).encode()
        self.assertIn('/' + 'b' * 40 + '/', url)
        return self.remote if url.endswith('.rb') else self.patch_data

    def recover(self):
        with patch.object(providers, 'read_url', side_effect=self.download):
            return providers.brew_local_patch(self.recipe, self.receipt, self.relative)

    def test_api_install_without_receipt_revision_recovers_verified_patch(self):
        data, origin = self.recover()
        self.assertEqual(data, self.patch_data)
        self.assertEqual(origin, 'https://raw.githubusercontent.com/Homebrew/homebrew-core/'
                         + 'b' * 40 + '/' + self.relative)
        self.assertEqual(len(self.calls), 3)

    def test_full_keg_revision_must_match(self):
        moved = self.keg.with_name('16.2.0_2')
        self.keg.rename(moved)
        self.recipe = moved / '.brew/gcc.rb'
        with self.assertRaisesRegex(common.MaterialError, 'version differs'):
            self.recover()
        self.api['revision'] = 2
        self.assertEqual(self.recover()[0], self.patch_data)

    def test_same_version_different_recipe_is_rejected_before_patch_download(self):
        self.recipe.write_bytes(self.recipe.read_bytes().replace(b'example.org', b'other.org'))
        with self.assertRaisesRegex(common.MaterialError, 'does not match'):
            self.recover()
        self.assertFalse(any(url.endswith('.diff') for url in self.calls))

    def test_bottle_gnu_mirror_prefix_change_recovers_patch(self):
        # Real GCC 16.2.0 bottle: /gnu/gcc/...; current API: /gcc/....
        current = (b'class Gcc < Formula\n'
                   b'  url "https://ftpmirror.gnu.org/gcc/gcc-16.2.0/gcc-16.2.0.tar.xz"\n'
                   b'  sha256 "' + b'a' * 64 + b'"\n'
                   b'  patch do\n    file "Patches/gcc/gcc-16.2.0.diff"\n  end\nend\n')
        installed = current.replace(b'ftpmirror.gnu.org/gcc/', b'ftpmirror.gnu.org/gnu/gcc/')
        self.recipe.write_bytes(installed)
        self.remote = current
        self.api['ruby_source_checksum']['sha256'] = sha(current)
        self.assertEqual(self.recover()[0], self.patch_data)
        self.assertEqual(self.recipe.read_bytes(), installed)

    def test_gnu_prefix_normalization_preserves_recipe_identity(self):
        original = (b'class Gcc < Formula\n'
                    b'  url "https://ftpmirror.gnu.org/gnu/gcc/gcc-16.2.0.tar.xz"\n'
                    b'  sha256 "' + b'a' * 64 + b'"\n'
                    b'  patch do\n    file "Patches/gcc/fix.diff"\n  end\nend\n')
        candidate = original.replace(b'.org/gnu/', b'.org/')
        self.assertTrue(providers.same_brew_recipe(original, candidate))
        for altered in [candidate.replace(b'16.2.0', b'16.1.0'),
                        candidate.replace(b'a' * 64, b'b' * 64),
                        candidate.replace(b'fix.diff', b'other.diff'),
                        candidate.replace(b'ftpmirror.gnu.org', b'example.org'),
                        candidate.replace(b'https:', b'http:'),
                        candidate.replace(b'tar.xz"', b'tar.xz?key=1"')]:
            with self.subTest(altered=altered):
                self.assertFalse(providers.same_brew_recipe(original, altered))
        for prefix in [b'# ', b'puts ', b'file ', b'  system ']:
            left = prefix + b'"https://ftpmirror.gnu.org/gnu/gcc/fix.diff"\n'
            right = left.replace(b'.org/gnu/', b'.org/')
            self.assertFalse(providers.same_brew_recipe(left, right))

    def test_formula_checksum_mismatch_is_rejected(self):
        self.api['ruby_source_checksum']['sha256'] = '0' * 64
        with self.assertRaisesRegex(common.MaterialError, 'formula checksum mismatch'):
            self.recover()
        self.assertEqual(len(self.calls), 2)

    def test_patch_checksum_mismatch_is_rejected(self):
        self.api['local_patch_checksums'][self.relative] = '0' * 64
        with self.assertRaisesRegex(common.MaterialError, 'patch checksum mismatch'):
            self.recover()

    def test_patch_checksum_required_before_patch_download(self):
        for checksums in [{}, {self.relative: 'invalid'}, {self.relative: 123}]:
            with self.subTest(checksums=checksums):
                self.calls.clear()
                self.api['local_patch_checksums'] = checksums
                with self.assertRaisesRegex(common.MaterialError, 'lacks a checksum'):
                    self.recover()
                self.assertFalse(any(url.endswith('.diff') for url in self.calls))

    def test_unpinned_or_unrelated_api_metadata_is_rejected(self):
        original = copy.deepcopy(self.api)
        cases = [('tap_git_head', 'HEAD'), ('tap_git_head', ''), ('tap_git_head', 123),
                 ('tap', 'other/tap'), ('name', 'other'), ('versions', {'stable': '17.0'}),
                 ('ruby_source_path', '../gcc.rb'), ('ruby_source_path', 'Formula/o/other.rb'),
                 ('ruby_source_checksum', {'sha256': 'bad'}), ('local_patch_checksums', None),
                 ('revision', -1), ('revision', None), ('versions', None)]
        for key, value in cases:
            with self.subTest(field=key, value=value):
                self.api = copy.deepcopy(original)
                self.api[key] = value
                self.calls.clear()
                with self.assertRaises(common.MaterialError):
                    self.recover()
                self.assertEqual(len(self.calls), 1)

    def test_invalid_api_json_is_reported(self):
        for data in [b'[]', b'null', b'not JSON', b'{}']:
            with self.subTest(data=data), patch.object(providers, 'read_url', return_value=data):
                with self.assertRaises(common.MaterialError):
                    providers.brew_local_patch(self.recipe, self.receipt, self.relative)

    def test_no_api_fallback_for_other_taps_or_invalid_recorded_revision(self):
        for source in [{'tap': 'other/tap'}, {'tap': 'homebrew/core', 'tap_git_head': 'HEAD'}]:
            with self.subTest(source=source), patch.object(
                    providers, 'read_url', side_effect=AssertionError('must not query API')):
                with self.assertRaises(common.MaterialError):
                    providers.brew_local_patch(self.recipe, {'source': source}, self.relative)

    def test_recorded_revision_mismatch_does_not_switch_to_current_api(self):
        self.receipt['source']['tap_git_head'] = 'c' * 40
        def download(url):
            self.assertNotIn('formulae.brew.sh', url)
            self.assertIn('/' + 'c' * 40 + '/', url)
            return b'different recipe'
        with patch.object(providers, 'read_url', side_effect=download):
            with self.assertRaisesRegex(common.MaterialError, 'does not match'):
                providers.brew_local_patch(self.recipe, self.receipt, self.relative)

    def test_installed_patch_remains_offline(self):
        local = self.recipe.parent / self.relative
        local.parent.mkdir(parents=True)
        local.write_bytes(self.patch_data)
        with patch.object(providers, 'read_url', side_effect=AssertionError('must remain offline')):
            data, origin = providers.brew_local_patch(self.recipe, self.receipt, self.relative)
        self.assertEqual((data, origin), (self.patch_data, 'installed-keg'))

    def test_reuses_one_api_snapshot_and_records_provenance(self):
        second = 'Patches/gcc/second.diff'
        self.api['local_patch_checksums'][second] = sha(self.patch_data)
        metadata = {'patches': [{'kind': 'local', 'path': name, 'strip': 'p1'}
                                for name in [self.relative, second]]}
        component = self.collection.component('brew-gcc', '16.2.0', 'homebrew-installed-keg')
        with patch.object(providers, 'read_url', side_effect=self.download):
            providers.save_brew_patches(self.collection, component, self.recipe, self.receipt, metadata)
        records = self.collection.records / 'recipes/brew-gcc'
        self.assertEqual((records / self.relative).read_bytes(), self.patch_data)
        snapshot = json.loads((records / 'local-patch-recovery.json').read_text())
        self.assertEqual(snapshot['tap_git_head'], 'b' * 40)
        self.assertEqual(snapshot['ruby_source_sha256'], sha(self.remote))
        self.assertNotIn(str(self.base), json.dumps(snapshot))
        self.assertEqual(len(self.calls), 4)  # API, formula, two patches
        self.assertTrue(all(item['sha256'] == sha(self.patch_data) for item in metadata['patches']))

    def test_recovery_completes_source_and_license_collection_for_both_macos_arches(self):
        library = self.keg / 'lib/libgcc_s.1.1.dylib'
        library.parent.mkdir()
        library.write_bytes(b'fixture Mach-O')
        (self.keg / 'INSTALL_RECEIPT.json').write_text(json.dumps(self.receipt))
        source = self.base / 'gcc.tar.gz'
        with tarfile.open(source, 'w:gz') as archive:
            for name, data in {'gcc/COPYING3': b'fixture license', 'gcc/source.c': b'fixture source'}.items():
                item = tarfile.TarInfo(name)
                item.size = len(data)
                archive.addfile(item, io.BytesIO(data))
        metadata = {'version': '16.2.0', 'sources': [
            {'url': 'https://example.org/gcc.tar.gz', 'sha256': common.digest(source), 'kind': 'source'}],
            'patches': [{'kind': 'local', 'path': self.relative, 'strip': 'p1'}]}
        def native(args, cwd=None):
            if args == ['brew', '--cellar']:
                return str(self.base / 'Cellar')
            self.assertEqual(args[:2], ['brew', 'ruby'])
            self.assertEqual(Path(args[-1]).resolve(), self.recipe.resolve())
            return json.dumps(metadata)
        for platform in ['macos-arm64', 'macos-x86_64']:
            with self.subTest(platform=platform):
                collection = common.Collection(self.base / platform, self.base / 'cache', platform, '1.2.0-beta.5')
                with patch.object(providers, 'run', side_effect=native), \
                        patch.object(providers, 'same_binary', return_value=True), \
                        patch.object(providers, 'read_url', side_effect=self.download), \
                        patch.object(common, 'download', return_value=source):
                    owners = providers.brew_sources(collection, [library], {})
                self.assertEqual(collection.errors, [])
                self.assertEqual(owners[library], 'brew-gcc')
                component = collection.components[0]
                self.assertTrue(component['sources'])
                self.assertTrue(component['notices'])
                self.assertTrue((collection.records / 'recipes/brew-gcc/local-patch-recovery.json').is_file())


if __name__ == '__main__':
    unittest.main()
