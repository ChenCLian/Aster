"""Run every desktop regression executable; retain all QtTest suite totals."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

TARGETS = ['aster_core_tests', 'aster_batch_tests', 'aster_analysis_tests', 'aster_cross_spectrum_tests', 'aster_ui_tests', 'aster_joint_ui_tests', 'aster_rewrite_tests', 'aster_linked_input_tests',
           'aster_style_export_tests', 'aster_workspace_tests',
           'aster_integration_tests', 'aster_update_tests']

def failure_excerpt(output):
    lines = output.splitlines()
    selected = set()
    for index, line in enumerate(lines):
        if any(marker in line for marker in ('FAIL!', 'QFATAL', 'Received signal')):
            selected.update(range(max(0, index - 2), min(len(lines), index + 12)))
    if not selected:
        selected.update(range(max(0, len(lines) - 25), len(lines)))
    return '\n'.join(lines[index] for index in sorted(selected))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--timeout', type=int, default=900,
                        help='Maximum seconds per test executable (default: 900)')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be a positive number of seconds')
    build = args.build.resolve()
    if not build.is_dir():
        parser.error(f'Build directory does not exist: {build}')
    logs = build / 'test-results'
    logs.mkdir(exist_ok=True)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', OPENBLAS_NUM_THREADS='1')
    results = []
    summary = logs / 'summary.json'
    def checkpoint():
        temporary = logs / 'summary.json.tmp'
        temporary.write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
        temporary.replace(summary)
    checkpoint()  # Never retain an earlier run's successful summary.
    for name in TARGETS:
        candidates = [build / 'bin' / name, build / 'bin' / (name + '.exe'),
                      build / 'tests' / name, build / 'tests' / (name + '.exe'),
                      build / 'bin' / (name + '.app') / 'Contents/MacOS' / name]
        exe = next((p for p in candidates if p.is_file()), None)
        log = logs / (name + '.txt')
        started = time.monotonic()
        code, status = None, 'failed'
        # Send output straight to disk so a timeout/launch error cannot discard
        # earlier diagnostics. subprocess.run kills/waits for a timed-out child.
        with log.open('w', encoding='utf-8') as stream:
            if exe is None:
                status = 'missing'
                stream.write(f'Missing test executable: {name}\n')
            else:
                try:
                    result = subprocess.run([str(exe)], cwd=build, env=env,
                                            stdout=stream, stderr=subprocess.STDOUT,
                                            timeout=args.timeout)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    status = 'timeout'
                    stream.write(f'\nRunner: Timed out after {args.timeout} seconds.\n')
                except OSError as error:
                    status = 'launch_error'
                    stream.write(f'\nRunner: Could not launch {name}: {error}\n')
                except KeyboardInterrupt:
                    status = 'interrupted'
                    stream.write('\nRunner: Interrupted; remaining targets were not started.\n')
        output = log.read_text(encoding='utf-8', errors='replace')
        rows = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', output)
        totals = [sum(int(r[i]) for r in rows) for i in range(3)]
        if code == 0 and rows and not totals[1] and not totals[2]:
            status = 'passed'
        entry = dict(target=name, exit_code=code, passed=totals[0],
                     failed=totals[1], skipped=totals[2], suites=len(rows), status=status,
                     seconds=round(time.monotonic() - started, 3), log=log.name)
        results.append(entry)
        checkpoint()
        print(entry, flush=True)
        if status != 'passed':
            print(f'Failure details for {name} (full log: {log}):', flush=True)
            print(failure_excerpt(output), flush=True)
        if status == 'interrupted':
            break
    if len(results) != len(TARGETS) or any(r['status'] != 'passed' for r in results):
        raise SystemExit('Regression verification failed; inspect test-results/*.txt')

if __name__ == '__main__':
    main()
