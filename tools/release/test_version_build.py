"""Build a tiny real qmake target; detect stale compiled prerelease versions.

Set ASTER_TEST_QMAKE to the Qt 6 qmake path when it is not on PATH.
Set ASTER_TEST_MAKE to test a specific make implementation.
This test uses make (Linux/macOS); Windows packaging has a separate PS regression.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VersionBuildTests(unittest.TestCase):
    @unittest.skipIf(os.name == 'nt', 'qmake/make regression runs on Linux/macOS')
    def test_version_changes_rebuild_consumers_without_cleaning(self):
        qmake = os.environ.get('ASTER_TEST_QMAKE') or shutil.which('qmake6') or shutil.which('qmake')
        make = os.environ.get('ASTER_TEST_MAKE') or shutil.which('make')
        if not qmake or not make:
            self.skipTest('Qt 6 qmake and make are required')
        probe = subprocess.run([qmake, '-query', 'QT_VERSION'], capture_output=True, text=True)
        if probe.returncode or not probe.stdout.startswith('6.'):
            if os.environ.get('ASTER_TEST_QMAKE'):
                self.fail('ASTER_TEST_QMAKE must be a working Qt 6 qmake: ' + probe.stderr)
            self.skipTest('No working Qt 6 qmake')
        with tempfile.TemporaryDirectory(prefix='aster version build ') as directory:
            root = Path(directory)
            source, build = root / 'source tree', root / 'build tree'
            (source / 'config').mkdir(parents=True)
            build.mkdir()
            for name in ('version.pri', 'aster_version.h.in'):
                shutil.copyfile(ROOT / 'config' / name, source / 'config' / name)
            (source / 'main.cpp').write_text('#include "aster_version.h"\n#include <cstdio>\n'
                                             'int main() { puts(ASTER_VERSION); }\n', encoding='utf-8')
            (source / 'probe.pro').write_text('TEMPLATE = app\nTARGET = probe\nCONFIG += console\n'
                                             'CONFIG -= qt app_bundle debug_and_release\n'
                                             'include(config/version.pri)\nSOURCES += main.cpp\n', encoding='utf-8')

            transcript = []

            def run(*args):
                result = subprocess.run(args, cwd=build, capture_output=True, text=True, timeout=60)
                transcript.append(f'$ {args!r}\n{result.stdout}{result.stderr}')
                self.assertEqual(result.returncode, 0, '\n'.join(transcript))
                return result.stdout.strip()

            def next_timestamp_tick():
                # Some make implementations (including the macOS system make)
                # compare whole seconds. Distinguish fixture edits from the last
                # build without touching generated files or forcing a rebuild.
                # Waiting after each build also keeps the test's no-op check real.
                latest = max((p.stat().st_mtime_ns // 1_000_000_000
                              for p in (build / 'Makefile', build / 'aster_version.h',
                                        build / 'main.o', build / 'probe') if p.exists()),
                             default=0)
                deadline = time.monotonic() + 5
                while time.time_ns() // 1_000_000_000 <= latest:
                    self.assertLess(time.monotonic(), deadline,
                                    'Clock did not advance beyond build timestamps')
                    time.sleep(0.02)

            run(qmake, '-v')
            run(make, '--version')
            for index, version in enumerate(('1.2.0-beta.2', '1.2.0-beta.5', '1.2.0-beta.6', '1.2.0')):
                if index:
                    next_timestamp_tick()
                (source / 'VERSION').write_text(version + '\n', encoding='utf-8')
                # Both ordinary incremental build and explicit qmake must work.
                if index in (0, 2):
                    run(qmake, str(source / 'probe.pro'))
                run(make, '-j2')
                self.assertEqual(run(str(build / 'probe')), version, '\n'.join(transcript))
            object_time = (build / 'main.o').stat().st_mtime_ns
            next_timestamp_tick()
            run(qmake, str(source / 'probe.pro'))
            run(make, '-j2')
            self.assertEqual((build / 'main.o').stat().st_mtime_ns, object_time,
                             'Unchanged version should not force recompilation')


if __name__ == '__main__':
    unittest.main()
