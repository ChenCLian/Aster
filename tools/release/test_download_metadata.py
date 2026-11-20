import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest
import zipfile
import download_metadata as d


class DownloadMetadataTests(unittest.TestCase):
    def package(self, base, platform='windows-x64'):
        folder = base / f'AstroBatch-1.3.0-beta.17-{platform}'
        folder.mkdir()
        entry = 'AstroBatch.exe' if platform == 'windows-x64' else 'AppRun'
        (folder / entry).write_bytes(b'fake executable')
        d.stamp(folder, '1.3.0-beta.17', platform)
        path = base / (folder.name + ('.tar.gz' if platform == 'linux-x64' else '.zip'))
        if platform == 'linux-x64':
            with tarfile.open(path, 'w:gz') as a:
                a.add(folder, arcname=folder.name)
        else:
            with zipfile.ZipFile(path, 'w') as a:
                for p in folder.iterdir():
                    a.write(p, folder.name+'/'+p.name)
        return path

    def test_zip_and_tar(self):
        with tempfile.TemporaryDirectory() as td:
            paths = [self.package(Path(td), p) for p in ('windows-x64', 'linux-x64')]
            result = d.generate(paths, '1.3.0-beta.17')
            self.assertEqual(len(result['packages']), 2)
            self.assertTrue(all(len(p['sha256']) == 64 for p in result['packages']))

    def test_alpha_and_duplicate_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            path = self.package(Path(td))
            for paths, version in [([path], '1.3.0-alpha.1'), ([path, path], '1.3.0-beta.17')]:
                with self.assertRaises(ValueError):
                    d.generate(paths, version)

    def test_mirror_requires_identical_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td); path = self.package(base); mirror = base/'mirror'; mirror.mkdir()
            with self.assertRaises(ValueError):
                d.generate([path], '1.3.0-beta.17', mirror)
            shutil.copy2(path, mirror/path.name)
            self.assertEqual(len(d.generate([path], '1.3.0-beta.17', mirror)['packages'][0]['sources']), 2)
            (mirror/path.name).write_bytes(b'changed')
            with self.assertRaises(ValueError):
                d.generate([path], '1.3.0-beta.17', mirror)

    def test_marker_and_pending_materials(self):
        with tempfile.TemporaryDirectory() as td:
            path = self.package(Path(td))
            with zipfile.ZipFile(path, 'a') as a:
                a.writestr(path.stem+'/DEPENDENCY_MATERIALS_PENDING.txt', 'pending')
            with self.assertRaises(ValueError):
                d.generate([path], '1.3.0-beta.17')

    def test_stamp_requires_launch_file(self):
        with tempfile.TemporaryDirectory() as td:
            folder=Path(td)/'AstroBatch-1.3.0-beta.17-windows-x64';folder.mkdir()
            with self.assertRaises(ValueError):
                d.stamp(folder, '1.3.0-beta.17', 'windows-x64')

    def test_archives_used_by_the_desktop_updater(self):
        fixtures = d.ROOT / 'download_tests' / 'fixtures'
        version = '99.0.0-beta.1'
        with tempfile.TemporaryDirectory() as folder:
            for platform in d.PLATFORMS:
                with self.subTest(platform=platform):
                    root = f'AstroBatch-{version}-{platform}'
                    suffix = '.tar.gz' if platform == 'linux-x64' else '.zip'
                    package = Path(folder) / (root + suffix)
                    shutil.copyfile(fixtures / (platform + suffix), package)
                    self.assertEqual(d.inspect_package(package, version, platform)['file'], package.name)
                    if platform == 'linux-x64':
                        with tarfile.open(package) as archive:
                            self.assertTrue(archive.getmember(root + '/AppRun').isfile())
                    else:
                        entry = 'AstroBatch.exe' if platform == 'windows-x64' else 'AstroBatch.app/Contents/MacOS/AstroBatch'
                        with zipfile.ZipFile(package) as archive:
                            self.assertGreater(archive.getinfo(root + '/' + entry).file_size, 0)
