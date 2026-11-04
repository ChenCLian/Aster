"""Run every desktop regression executable; retain all QtTest suite totals."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

TARGETS = ['aster_core_tests', 'aster_analysis_tests', 'aster_ui_tests',
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
    args = parser.parse_args()
    build = args.build.resolve()
    logs = build / 'test-results'
    logs.mkdir(exist_ok=True)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', OPENBLAS_NUM_THREADS='1')
    results = []
    for name in TARGETS:
        candidates = [build / 'bin' / name, build / 'bin' / (name + '.exe'),
                      build / 'tests' / name, build / 'tests' / (name + '.exe'),
                      build / 'bin' / (name + '.app') / 'Contents/MacOS' / name]
        exe = next((p for p in candidates if p.is_file()), None)
        if exe is None:
            raise SystemExit(f'Missing test executable: {name}')
        result = subprocess.run([str(exe)], cwd=build, env=env, text=True,
                                encoding='utf-8', errors='replace',
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        (logs / (name + '.txt')).write_text(result.stdout, encoding='utf-8')
        rows = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped', result.stdout)
        totals = [sum(int(r[i]) for r in rows) for i in range(3)]
        entry = dict(target=name, exit_code=result.returncode, passed=totals[0],
                     failed=totals[1], skipped=totals[2], suites=len(rows))
        results.append(entry)
        print(entry, flush=True)
        if result.returncode or totals[1] or totals[2] or not rows:
            print(f'Failure details for {name} (full log: {logs / (name + ".txt")}):', flush=True)
            print(failure_excerpt(result.stdout), flush=True)
    (logs / 'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    if any(r['exit_code'] or r['failed'] or r['skipped'] or not r['suites'] for r in results):
        raise SystemExit('Regression verification failed; inspect test-results/*.txt')

if __name__ == '__main__':
    main()
