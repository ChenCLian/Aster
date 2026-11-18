"""Select a successful desktop build and its complete, unexpired release artifacts.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

REPO = 'ChenCLian/Aster'
WORKFLOW = '.github/workflows/build-desktop.yml'
PLATFORMS = ('windows-x64', 'linux-x64', 'macos-arm64', 'macos-x86_64')


def github(endpoint):
    result = subprocess.run(['gh', 'api', endpoint], check=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, encoding='utf-8', timeout=120)
    return json.loads(result.stdout)


def select(repo, number):
    if repo != REPO or not isinstance(number, int) or number <= 0:
        raise ValueError('Select a positive Build desktop packages number in '+REPO)
    selected = None
    for page in range(1, 101):
        rows = github(f'repos/{repo}/actions/workflows/build-desktop.yml/runs?per_page=100&page={page}')['workflow_runs']
        matches = [r for r in rows if r.get('run_number') == number]
        if len(matches) > 1:
            raise ValueError('Ambiguous build number')
        if matches:
            # Fetch fresh status, not just the potentially stale listing entry.
            selected = github(f'repos/{repo}/actions/runs/{int(matches[0]["id"])}')
            break
        if len(rows) < 100:
            break
    if not selected:
        raise ValueError('Build number not found; use the # number of Build desktop packages')
    if (selected.get('run_number') != number or selected.get('path', '').split('@')[0] != WORKFLOW
            or selected.get('event') != 'workflow_dispatch'
            or selected.get('head_repository', {}).get('full_name', '').casefold() != repo.casefold()):
        raise ValueError('Build is not an official manually requested desktop run')
    if selected.get('status') != 'completed' or selected.get('conclusion') != 'success':
        raise ValueError('Build must be completed and successful')
    sha = selected.get('head_sha', '')
    if not re.fullmatch('[0-9a-fA-F]{40}', sha):
        raise ValueError('Build has no valid source commit')
    run_id = selected['id']
    artifacts = []
    for page in range(1, 101):
        rows = github(f'repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100&page={page}')['artifacts']
        artifacts.extend(rows)
        if len(rows) < 100:
            break
    else:
        raise ValueError('Artifact listing is incomplete')
    expected = {prefix+p for prefix in ('program-', 'dependency-materials-') for p in PLATFORMS}
    chosen = []
    for name in sorted(expected):
        matches = [a for a in artifacts if a.get('name') == name]
        if len(matches) != 1:
            raise ValueError('Missing or duplicate artifact: '+name+'; all four packaged platforms are required')
        item = matches[0]
        if item.get('expired') is not False or item.get('size_in_bytes', 0) <= 0:
            raise ValueError('Expired or empty artifact: '+name)
        chosen.append({'id': item['id'], 'name': name, 'bytes': item['size_in_bytes']})
    return {'schema': 1, 'repository': repo, 'build_number': number, 'run_id': run_id,
            'commit': sha, 'artifacts': chosen}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', default=REPO)
    parser.add_argument('--build-number', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = select(args.repository, args.build_number)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
            output.write(f'run_id={result["run_id"]}\ncommit={result["commit"]}\n')
    print(f'Selected build #{args.build_number}: run {result["run_id"]}, commit {result["commit"]}; all eight artifacts available')


if __name__ == '__main__':
    main()
