"""Release layout and macOS provenance regressions.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import base64
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependency_common as common
import dependency_providers as providers
import package_source

ROOT = Path(__file__).resolve().parents[2]


class PackagingLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def source_tree(self):
        root = self.base / 'project'
        for name, data in {
            'VERSION': b'1.1.1\n', 'PACKAGE_STATUS.json': b'{"version":"1.1.1"}',
            'LICENSE': b'terms', 'COPYRIGHT.md': b'copyright', 'SOURCE_CODE.md': b'source',
            'config/local.pri.example': b'template', 'config/local.pri': b'private path',
            'build-release/main.o': b'binary', 'dist/old.zip': b'archive',
            'src/main.cpp': b'int main(){}\n', '.github/workflows/build.yml': b'name: build\n',
            'tools/run.sh': b'#!/bin/sh\n', 'results.sqlite': b'user database',
        }.items():
            p = root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        (root / 'tools/run.sh').chmod(0o755)
        return root

    def test_source_export_is_directory_verified_and_private_files_excluded(self):
        root = self.source_tree()
        out = self.base / 'out'
        with patch.object(package_source, 'ROOT', root), patch.object(sys, 'argv', ['export', '--output', str(out)]):
            package_source.main()
            with self.assertRaisesRegex(SystemExit, 'Refusing to overwrite'):
                package_source.main()
        dest = out / 'Aster-source-export-1.1.1'
        self.assertEqual(list(out.iterdir()), [dest])
        for name in ['config/local.pri', 'build-release', 'dist', 'results.sqlite']:
            self.assertFalse((dest / name).exists())
        self.assertTrue((dest / '.github/workflows/build.yml').is_file())
        self.assertTrue((dest / 'tools/run.sh').stat().st_mode & 0o100)
        manifest = json.loads((dest / 'PACKAGE_MANIFEST.json').read_text())
        for item in manifest['files']:
            data = (dest / item['path']).read_bytes()
            self.assertEqual(item['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(data, (root / item['path']).read_bytes())
        self.assertFalse(list(out.rglob('*.sha256')))
        self.assertFalse(list(out.rglob('*.zip')))

    def test_source_export_rejects_inside_project(self):
        root = self.source_tree()
        with patch.object(package_source, 'ROOT', root), patch.object(sys, 'argv', ['export', '--output', str(root / 'copy')]):
            with self.assertRaisesRegex(SystemExit, 'outside the source tree'):
                package_source.main()
        self.assertFalse((root / 'copy').exists())

    def test_relative_source_includes_are_present(self):
        # Check referenced paths, not only existing files: an omitted header
        # must fail before CI spends time compiling the desktop application.
        missing = []
        for source in sorted(ROOT.rglob('*')):
            if (not source.is_file() or source.suffix not in ('.cpp', '.h')
                    or not package_source.included(source)):
                continue
            text = source.read_text(encoding='utf-8', errors='replace')
            for name in re.findall(r'^\s*#\s*include\s*"(\.{1,2}/[^"\n]+)"',
                                   text, re.MULTILINE):
                if not (source.parent / name).is_file():
                    missing.append(f'{source.relative_to(ROOT).as_posix()}: {name}')
        self.assertEqual(missing, [], 'Missing relative source includes: ' + '; '.join(missing))

    @unittest.skipUnless(shutil.which('git'), 'Git is required for source tracking checks')
    def test_shared_test_header_survives_export_and_git_add(self):
        root = self.source_tree()
        helper = 'tests/support/joint_input_helpers.h'
        for name in ('.gitignore', helper):
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / name).read_bytes())
        out = self.base / 'export'
        with patch.object(package_source, 'ROOT', root), patch.object(
                sys, 'argv', ['export', '--output', str(out)]):
            package_source.main()
        dest = out / 'Aster-source-export-1.1.1'
        subprocess.run(['git', 'init', '-q', str(dest)], check=True, capture_output=True)
        subprocess.run(['git', '-C', str(dest), 'add', '--all'], check=True, capture_output=True)
        result = subprocess.run(['git', '-C', str(dest), 'ls-files', '--error-unmatch', helper],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((dest / helper).read_bytes(), (ROOT / helper).read_bytes())
        ignored = subprocess.run(['git', '-C', str(dest), 'check-ignore', '--no-index',
                                  'ui_generated.h', 'build-release/ui_generated.h'],
                                 capture_output=True, text=True)
        self.assertEqual(ignored.stdout.splitlines(),
                         ['ui_generated.h', 'build-release/ui_generated.h'])

    @unittest.skipUnless(shutil.which('git'), 'Git is required for source tracking checks')
    def test_distributable_cpp_sources_are_not_ignored(self):
        repo = self.base / 'tracking'
        subprocess.run(['git', 'init', '-q', str(repo)], check=True, capture_output=True)
        shutil.copyfile(ROOT / '.gitignore', repo / '.gitignore')
        names = sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob('*')
                       if p.is_file() and p.suffix in ('.cpp', '.h', '.pro', '.pri')
                       and package_source.included(p))
        result = subprocess.run(['git', '-C', str(repo), 'check-ignore', '--no-index', '--stdin'],
                                input='\n'.join(names) + '\n', capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def project_alias(self, root):
        alias = self.base / 'project-link'
        try:
            alias.symlink_to(root.resolve(), target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest('Directory symlinks are unavailable on this host')
        return alias

    def test_source_export_rejects_project_aliases(self):
        root = self.source_tree()
        alias = self.project_alias(root)
        # macOS temporary paths can be /var/... aliases of /private/var/... .
        for source in [root.resolve(), alias]:
            for output in [root, alias, root / 'copy', alias / 'copy']:
                with self.subTest(source=source.name, output=str(output)):
                    with patch.object(package_source, 'ROOT', source), patch.object(
                            sys, 'argv', ['export', '--output', str(output)]):
                        with self.assertRaisesRegex(SystemExit, 'outside the source tree'):
                            package_source.main()
                    self.assertFalse((root / 'copy').exists())
                    self.assertFalse((root / 'Aster-source-export-1.1.1').exists())

    def test_source_export_accepts_external_directory_through_project_alias(self):
        root = self.source_tree()
        alias = self.project_alias(root)
        output = self.base / 'export'
        with patch.object(package_source, 'ROOT', alias), patch.object(
                sys, 'argv', ['export', '--output', str(output)]):
            package_source.main()
        dest = output / 'Aster-source-export-1.1.1'
        self.assertEqual((dest / 'src/main.cpp').read_bytes(), (root / 'src/main.cpp').read_bytes())
        self.assertFalse((dest / 'config/local.pri').exists())

    def test_qt_framework_header_is_not_sent_to_dwarfdump(self):
        prefix = self.base / 'Qt'
        header = prefix / 'lib/QtCore.framework/Versions/A/Headers/QtCore'
        lib = prefix / 'lib/QtCore.framework/Versions/A/QtCore'
        header.parent.mkdir(parents=True)
        header.write_bytes(b'#include <QtCore/qglobal.h>')
        lib.write_bytes(b'\xcf\xfa\xed\xfeactual binary')
        staged = self.base / 'app/QtCore'
        staged.parent.mkdir()
        staged.write_bytes(b'\xcf\xfa\xed\xfedeployed binary')
        def native(args):
            self.assertEqual(args[:2], ['dwarfdump', '--uuid'])
            self.assertNotEqual(args[-1], header)
            return 'UUID: 01234567-89AB-CDEF-0123-456789ABCDEF (arm64) ' + str(args[-1])
        with patch.object(providers, 'run', side_effect=native):
            self.assertEqual(providers.qt_owners([staged], prefix, 'macos-arm64'), {staged: 'qt'})

    def test_macos_identity_compares_target_slice_of_universal_binary(self):
        thin = self.base / 'thin'
        fat = self.base / 'fat'
        uuids = {'arm64': '11111111-1111-1111-1111-111111111111',
                 'x86_64': '22222222-2222-2222-2222-222222222222'}
        for arch in uuids:
            def native(args):
                arches = [arch] if args[-1] == thin else list(uuids)
                return '\n'.join(f'UUID: {uuids[a]} ({a}) file' for a in arches)
            with patch.object(providers, 'run', side_effect=native):
                self.assertTrue(providers.same_binary(thin, fat, 'macos-' + arch))
        with patch.object(providers, 'run', return_value='UUID: 3333 (x86_64) file'):
            self.assertFalse(providers.same_binary(thin, fat, 'macos-arm64'))
        with patch.object(providers, 'run', side_effect=['UUID: 1111 (arm64) a', 'UUID: 2222 (arm64) b']):
            self.assertFalse(providers.same_binary(thin, fat, 'macos-arm64'))

    def recipe(self):
        recipe = self.base / 'Cellar/gcc/16.2.0/.brew/gcc.rb'
        recipe.parent.mkdir(parents=True)
        recipe.write_bytes(b'class Gcc < Formula\n  url "https://example.org/gcc.tar.xz"\nend\n')
        return recipe

    def test_homebrew_local_and_embedded_patch_bytes_are_preserved(self):
        recipe = self.recipe()
        data = b'diff --git a/a b/a\n-old\n+new\n'
        local = recipe.parent / 'Patches/gcc/gcc.diff'
        local.parent.mkdir(parents=True)
        local.write_bytes(data)
        metadata = {'patches': [
            {'kind': 'local', 'path': 'Patches/gcc/gcc.diff', 'strip': 'p1'},
            {'kind': 'embedded', 'data': base64.b64encode(b'inline\r\n').decode(), 'strip': 'p0'}]}
        collection = common.Collection(self.base / 'materials', self.base / 'cache', 'macos-arm64', '1.1.1')
        component = collection.component('brew-gcc', '16.2.0', 'homebrew-installed-keg')
        with patch.object(providers, 'read_url', side_effect=AssertionError('must use installed files')):
            providers.save_brew_patches(collection, component, recipe, {}, metadata)
        self.assertEqual((collection.records / 'recipes/brew-gcc/Patches/gcc/gcc.diff').read_bytes(), data)
        self.assertEqual((collection.records / 'recipes/brew-gcc/embedded-patches/001.patch').read_bytes(), b'inline\r\n')
        self.assertEqual(metadata['patches'][0]['sha256'], hashlib.sha256(data).hexdigest())
        self.assertNotIn('data', metadata['patches'][1])
        self.assertNotIn(str(self.base), json.dumps(metadata))

    def test_homebrew_recovers_local_patch_from_exact_commit_and_checks_recipe(self):
        recipe = self.recipe()
        revision = 'a' * 40
        receipt = {'source': {'tap': 'homebrew/core', 'tap_git_head': revision}}
        data = b'exact patch'
        fetched = []
        remote = recipe.read_bytes().replace(b'  url ', b'  bottle do\n    sha256 "bottle"\n  end\n\n  url ')
        def download(url):
            fetched.append(url)
            self.assertIn('/' + revision + '/', url)
            return remote if url.endswith('.rb') else data
        with patch.object(providers, 'read_url', side_effect=download):
            result, origin = providers.brew_local_patch(recipe, receipt, 'Patches/gcc/gcc.diff')
        self.assertEqual(result, data)
        self.assertTrue(origin.endswith('Patches/gcc/gcc.diff'))
        self.assertEqual(len(fetched), 2)
        with patch.object(providers, 'read_url', return_value=b'different source recipe'):
            with self.assertRaisesRegex(common.MaterialError, 'does not match'):
                providers.brew_local_patch(recipe, receipt, 'Patches/gcc/gcc.diff')

    def test_homebrew_patch_uses_matching_api_cache_without_online_formula(self):
        recipe = self.recipe()
        original = self.base / ('api-source/Homebrew/homebrew-core/' + 'b' * 40 + '/Formula/g/gcc.rb')
        original.parent.mkdir(parents=True)
        original.write_bytes(recipe.read_bytes())
        local = original.parents[2] / 'Patches/gcc/gcc.diff'
        local.parent.mkdir(parents=True)
        local.write_bytes(b'cached exact patch')
        receipt = {'source': {'path': str(original), 'tap': 'homebrew/core'}}
        with patch.object(providers, 'read_url', side_effect=AssertionError('must use installed cache')):
            data, origin = providers.brew_local_patch(recipe, receipt, 'Patches/gcc/gcc.diff')
        self.assertEqual(data, b'cached exact patch')
        self.assertEqual(origin, 'installed-formula-source-cache')

    def test_homebrew_missing_revision_or_unsafe_patch_path_is_rejected(self):
        recipe = self.recipe()
        with patch.object(providers, 'read_url', side_effect=AssertionError('must not fetch latest')):
            for receipt in [{}, {'source': {'tap': 'homebrew/core', 'tap_git_head': 'HEAD'}}]:
                with self.assertRaises(common.MaterialError):
                    providers.brew_local_patch(recipe, receipt, 'Patches/gcc/gcc.diff')
            for path in ['../../secret', '/absolute', 'Patches/../../secret']:
                with self.assertRaises(common.MaterialError):
                    providers.brew_local_patch(recipe, {}, path)


if __name__ == '__main__':
    unittest.main()
