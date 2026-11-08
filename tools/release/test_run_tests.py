"""Regression runner failures must retain diagnostics and an honest summary."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import run_tests as runner


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name)
        (self.build / 'bin').mkdir()
        self.log = self.build / 'test-results'

    def executable(self, name, suffix=''):
        path = self.build / 'bin' / (name + suffix)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
        return path

    def emit(self, text, kwargs, code=0):
        stream = kwargs['stdout']
        if stream != subprocess.PIPE:
            stream.write(text)
            stream.flush()
        return subprocess.CompletedProcess([], code, text)

    def invoke(self, names, effect, *extra):
        with patch.object(runner, 'TARGETS', names), patch.object(sys, 'argv',
                ['run_tests', str(self.build), *extra]), patch.object(
                runner.subprocess, 'run', side_effect=effect) as calls, contextlib.redirect_stdout(io.StringIO()):
            try:
                runner.main()
                code = 0
            except SystemExit as e:
                code = e.code
        return code, calls

    def summary(self):
        return json.loads((self.log / 'summary.json').read_text())

    def test_missing_executable_is_recorded_and_remaining_targets_run(self):
        self.executable('two')
        code, calls = self.invoke(['one', 'two'], lambda *a, **k:
                                 self.emit('Totals: 4 passed, 0 failed, 0 skipped\n', k))
        self.assertNotEqual(code, 0)
        self.assertEqual(calls.call_count, 1)
        self.assertEqual([r['status'] for r in self.summary()], ['missing', 'passed'])
        self.assertIn('Missing test executable', (self.log / 'one.txt').read_text())

    def test_timeout_preserves_partial_log_and_continues(self):
        for name in ['one', 'two']: self.executable(name)
        def run(args, **kwargs):
            if Path(args[0]).name == 'one':
                self.emit('PASS: early case\n', kwargs)
                raise subprocess.TimeoutExpired(args, 900, output=b'PASS: early case\n')
            return self.emit('Totals: 3 passed, 0 failed, 0 skipped\n', kwargs)
        code, calls = self.invoke(['one', 'two'], run)
        self.assertNotEqual(code, 0)
        self.assertEqual(calls.call_count, 2)
        self.assertEqual([r['status'] for r in self.summary()], ['timeout', 'passed'])
        self.assertIn('early case', (self.log / 'one.txt').read_text())
        self.assertIn('Timed out', (self.log / 'one.txt').read_text())

    def test_launch_failure_is_recorded(self):
        self.executable('one')
        code, _ = self.invoke(['one'], PermissionError('not executable'))
        self.assertNotEqual(code, 0)
        self.assertEqual(self.summary()[0]['status'], 'launch_error')
        self.assertIn('not executable', (self.log / 'one.txt').read_text())

    def test_all_suite_totals_and_windows_filename_are_retained(self):
        exe = self.executable('one', '.exe')
        output = 'Totals: 3 passed, 0 failed, 0 skipped\nTotals: 4 passed, 0 failed, 0 skipped\n'
        code, calls = self.invoke(['one'], lambda *a, **k: self.emit(output, k))
        self.assertEqual(code, 0)
        self.assertEqual(calls.call_args.args[0], [str(exe.resolve())])
        row = self.summary()[0]
        self.assertEqual((row['passed'], row['suites'], row['status']), (7, 2, 'passed'))

    def test_build_directory_symlink_uses_canonical_executable(self):
        real = self.build / 'real build'
        (real / 'bin').mkdir(parents=True)
        alias = self.build / 'build alias'
        try:
            alias.symlink_to(real.resolve(), target_is_directory=True)
        except OSError:
            if sys.platform == 'win32':
                self.skipTest('Creating a directory symlink requires Windows privileges')
            raise
        self.build = alias
        self.log = alias / 'test-results'
        exe = self.executable('one', '.exe')
        code, calls = self.invoke(['one'], lambda *a, **k:
                                 self.emit('Totals: 3 passed, 0 failed, 0 skipped\n', k))
        self.assertEqual(code, 0)
        self.assertEqual(calls.call_args.args[0], [str(exe.resolve())])
        self.assertEqual(calls.call_args.kwargs['cwd'], real.resolve())
        self.assertEqual(self.summary()[0]['status'], 'passed')

    def test_crash_skips_and_missing_totals_fail(self):
        for name in ['one', 'two', 'three']: self.executable(name)
        def run(args, **kwargs):
            name = Path(args[0]).name
            return self.emit('Totals: 2 passed, 0 failed, 0 skipped\n' if name == 'one' else
                             'Totals: 2 passed, 0 failed, 1 skipped\n' if name == 'two' else '',
                             kwargs, -6 if name == 'one' else 0)
        code, _ = self.invoke(['one', 'two', 'three'], run)
        self.assertNotEqual(code, 0)
        self.assertTrue(all(r['status'] == 'failed' for r in self.summary()))

    def test_summary_is_checkpointed_before_next_target(self):
        for name in ['one', 'two']: self.executable(name)
        def run(args, **kwargs):
            rows = self.summary()
            if Path(args[0]).name == 'one': self.assertEqual(rows, [])
            else: self.assertEqual(rows[0]['status'], 'passed')
            return self.emit('Totals: 1 passed, 0 failed, 0 skipped\n', kwargs)
        code, _ = self.invoke(['one', 'two'], run)
        self.assertEqual(code, 0)

    def test_interrupt_keeps_summary_without_starting_next_target(self):
        for name in ['one', 'two']: self.executable(name)
        def run(*args, **kwargs):
            self.emit('partial output\n', kwargs)
            raise KeyboardInterrupt()
        code, calls = self.invoke(['one', 'two'], run)
        self.assertNotEqual(code, 0)
        self.assertEqual(calls.call_count, 1)
        self.assertEqual(self.summary()[0]['status'], 'interrupted')

    def test_timeout_option_is_positive_and_forwarded(self):
        self.executable('one')
        code, calls = self.invoke(['one'], lambda *a, **k:
                                 self.emit('Totals: 1 passed, 0 failed, 0 skipped\n', k), '--timeout', '1200')
        self.assertEqual(code, 0)
        self.assertEqual(calls.call_args.kwargs['timeout'], 1200)
        with contextlib.redirect_stderr(io.StringIO()):
            code, calls = self.invoke(['one'], None, '--timeout', '0')
        self.assertNotEqual(code, 0)
        calls.assert_not_called()

    def test_real_timeout_keeps_streamed_output(self):
        self.executable('one')
        real_run = subprocess.run
        def run(args, **kwargs):
            return real_run([sys.executable, '-c',
                "import os,time; os.write(1, 'partial 中文\\n'.encode('utf-8')); time.sleep(5)"], **kwargs)
        code, _ = self.invoke(['one'], run, '--timeout', '1')
        self.assertNotEqual(code, 0)
        self.assertEqual(self.summary()[0]['status'], 'timeout')
        output = (self.log / 'one.txt').read_text(encoding='utf-8')
        self.assertIn('partial 中文', output)
        self.assertIn('Timed out after 1 seconds', output)


if __name__ == '__main__':
    unittest.main()
