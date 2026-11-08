"""Offline reuse must cover the exact runtime and preserve publication materials.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import copy
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import collect_dependency_materials as collector
import dependency_common as common
import reuse_dependency_materials as reuse


class DependencyReuseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.package = self.base / 'package'
        self.package.mkdir()
        self.materials = self.base / 'materials'
        self.collection = common.Collection(self.materials, self.base / 'cache', 'windows-x64', '1.1.1')
        source = self.base / 'fixture.tar.gz'
        with tarfile.open(source, 'w:gz') as archive:
            for name, data in {'src/LICENSE': b'original terms\r\n', 'src/lib.c': b'int f(){return 1;}'}.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        for name, version in [('qt', '6.12.0'), ('vcpkg-gsl', '2.8')]:
            component = self.collection.component(name, version, 'test-fixture')
            with patch.object(common, 'download', return_value=source):
                self.collection.source(component, 'https://example.org/source.tar.gz', 'sha256',
                                       common.digest(source), name + '.tar.gz')
            self.collection.save_record(component, 'build.txt', b'build recipe', 'recipes')
        owners = {}
        for name, owner in [('Aster.exe', 'Aster-project-source'), ('Qt6Core.dll', 'qt'), ('gsl.dll', 'vcpkg-gsl')]:
            binary = self.package / name
            binary.write_bytes(b'MZ' + name.encode())
            owners[binary] = owner
        self.owners = owners
        self.assertEqual(collector.finish(self.collection, self.package, owners, list(owners)), 0)
        self.zip = self.materials / 'release-assets/Aster-1.1.1-windows-x64-dependency-records.zip'
        self.before = self.snapshot()
        self.addCleanup(patch.stopall)
        patch.object(common, 'open_url', side_effect=AssertionError('offline reuse attempted network')).start()
        patch.object(common, 'extract_notices', side_effect=AssertionError('source archives must not be reprocessed')).start()
        patch.object(reuse, 'run', return_value='6.12.0').start()

    def snapshot(self):
        return {p.relative_to(self.materials).as_posix(): p.read_bytes()
                for p in self.materials.rglob('*') if p.is_file()}

    def execute(self):
        return reuse.reuse(self.package, self.materials, '1.1.1', self.base / 'qmake.exe')

    def reject(self, pattern):
        notices = (self.package / 'licenses/dependencies/DEPENDENCIES.json').read_bytes()
        with self.assertRaisesRegex(common.MaterialError, pattern):
            self.execute()
        self.assertTrue((self.package / reuse.PENDING).is_file())
        self.assertEqual((self.package / 'licenses/dependencies/DEPENDENCIES.json').read_bytes(), notices)

    def edit_report(self, edit):
        path = self.materials / 'report.json'
        data = json.loads(path.read_text())
        edit(data)
        common.write_json(path, data)

    def edit_zip(self, edit):
        with zipfile.ZipFile(self.zip) as archive:
            data = {name: archive.read(name) for name in archive.namelist()}
        edit(data)
        with zipfile.ZipFile(self.zip, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, content in data.items():
                archive.writestr(name, content)

    def test_legacy_materials_reused_offline_after_application_rebuild(self):
        (self.package / 'Aster.exe').write_bytes(b'MZchanged application source and UI')
        # A local unpacked records directory is unnecessary: use the published ZIP.
        import shutil
        shutil.rmtree(self.materials / 'records')
        before = self.snapshot()
        self.assertEqual(self.execute(), 0)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.package / reuse.PENDING).exists())
        installed = json.loads((self.package / 'licenses/dependencies/DEPENDENCIES.json').read_text())
        binary = next(x for x in installed['binaries'] if x['path'] == 'Aster.exe')
        self.assertEqual(binary['sha256_before_signing'], common.digest(self.package / 'Aster.exe'))
        self.assertEqual(installed['reuse']['mode'], 'offline-verified')
        self.assertEqual((self.package / 'licenses/dependencies/qt/src/LICENSE').read_bytes(), b'original terms\r\n')
        self.assertIn('/v1.1.1/qt.tar.gz', (self.package / 'SOURCE_DEPENDENCIES.md').read_text())
        self.assertEqual(self.execute(), 0)
        self.assertFalse(list(self.package.rglob('*.sha256')))

    def test_same_version_dll_rebuild_is_rejected(self):
        (self.package / 'gsl.dll').write_bytes(b'MZsame version different binary')
        self.reject('Dependency binary changed: gsl.dll')

    def test_added_runtime_is_rejected(self):
        (self.package / 'extra.dll').write_bytes(b'MZextra')
        self.reject('Runtime dependency set changed')

    def test_removed_plugin_is_rejected(self):
        (self.package / 'gsl.dll').unlink()
        self.reject('Runtime dependency set changed')

    def test_missing_materials_are_rejected(self):
        (self.materials / 'report.json').unlink()
        self.reject('Missing or external material')

    def test_missing_source_asset_is_rejected(self):
        (self.materials / 'release-assets/qt.tar.gz').unlink()
        self.reject('Missing or external material')

    def test_corrupt_source_asset_is_rejected(self):
        (self.materials / 'release-assets/qt.tar.gz').write_bytes(b'broken')
        self.reject('Source asset checksum mismatch')

    def test_incomplete_report_is_rejected(self):
        self.edit_report(lambda r: r.update(status='incomplete'))
        self.reject('successfully collected')

    def test_different_release_version_is_rejected(self):
        self.edit_report(lambda r: r.update(version='1.1.2'))
        self.reject('this Aster version')

    def test_different_platform_is_rejected(self):
        self.edit_report(lambda r: r.update(platform='macos-arm64'))
        self.reject('windows-x64')

    def test_changed_qt_version_is_rejected(self):
        with patch.object(reuse, 'run', return_value='6.13.0'):
            self.reject('Qt version changed')

    def test_report_and_records_must_agree(self):
        self.edit_report(lambda r: r.update(scope='edited independently'))
        self.reject('Report and published dependency records disagree')

    def test_missing_license_in_records_is_rejected(self):
        self.edit_zip(lambda d: d.pop('licenses/qt/src/LICENSE'))
        self.reject('Missing license or recipe')

    def test_missing_recipe_in_records_is_rejected(self):
        self.edit_zip(lambda d: d.pop('recipes/qt/build.txt'))
        self.reject('Missing license or recipe')

    def test_unknown_source_owner_is_rejected(self):
        self.edit_report(lambda r: r['binaries'][1].update(component='UNRESOLVED'))
        self.reject('Unknown dependency source owner')

    def test_project_exception_cannot_be_assigned_to_a_dll(self):
        self.edit_report(lambda r: r['binaries'][1].update(component='Aster-project-source'))
        self.reject('Unknown dependency source owner')

    def test_unsafe_zip_entry_is_rejected(self):
        self.edit_zip(lambda d: d.update({'licenses/../../../outside': b'bad'}))
        self.reject('Unsafe archive member')
        self.assertFalse((self.base / 'outside').exists())

    def test_collector_success_clears_pending_but_no_install_does_not(self):
        (self.package / reuse.PENDING).write_text('pending')
        for install in (False, True):
            collection = copy.copy(self.collection)
            collection.inventory = []
            self.assertEqual(collector.finish(collection, self.package, self.owners, list(self.owners), install=install), 0)
            self.assertEqual((self.package / reuse.PENDING).exists(), not install)

    def test_failed_collection_does_not_clear_pending(self):
        (self.package / reuse.PENDING).write_text('pending')
        self.collection.error('qt', 'test failure')
        self.assertEqual(collector.finish(self.collection, self.package, self.owners, list(self.owners)), 2)
        self.assertTrue((self.package / reuse.PENDING).exists())

    def test_cli_failure_returns_nonzero_and_preserves_materials(self):
        (self.package / 'Qt6Core.dll').write_bytes(b'MZchanged Qt')
        self.assertEqual(reuse.main(['--package-dir', str(self.package), '--materials', str(self.materials),
                                    '--qmake', str(self.base / 'qmake.exe')]), 2)
        self.assertEqual(self.snapshot(), self.before)

    def test_cli_handles_corrupt_zip_as_failure(self):
        self.zip.write_bytes(b'broken archive')
        self.assertEqual(reuse.main(['--package-dir', str(self.package), '--materials', str(self.materials),
                                    '--qmake', str(self.base / 'qmake.exe')]), 2)
        self.assertTrue((self.package / reuse.PENDING).exists())


if __name__ == '__main__':
    unittest.main()
