"""Export a clean source directory, excluding builds, host settings and user data.

python tools/release/package_source.py --output ../release-source
Uses the single VERSION file; never changes or uploads a GitHub release.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXCLUDED_DIRS = {'.git', '.qtcreator', '__pycache__', 'dist', '.pytest_cache',
                 'dependency-materials', 'dependency-cache', 'release-assets'}
EXCLUDED_SUFFIXES = {'.exe', '.dll', '.lib', '.a', '.o', '.obj', '.pdb', '.ilk', '.pyc', '.sqlite', '.db'}

def included(path):
    rel = path.relative_to(ROOT)
    if any(p in EXCLUDED_DIRS or p.startswith('build') or p.endswith('.backups') for p in rel.parts[:-1]):
        return False
    if path.is_symlink() or path.suffix.lower() in EXCLUDED_SUFFIXES:
        return False
    return not (str(rel).replace('\\', '/') == 'config/local.pri' or path.name in {'PACKAGE_MANIFEST.json', '.qmake.stash', 'settings.ini'}
                or path.name.startswith(('Makefile', '.env', 'moc_', 'qrc_')) or '.pro.user' in path.name
                or '.sqlite-' in path.name or '.db-' in path.name)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    version = (ROOT / 'VERSION').read_text().strip()
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?', version):
        raise SystemExit('Invalid VERSION')
    output = args.output.resolve()
    source_root = ROOT.resolve()
    if output == source_root or source_root in output.parents:
        raise SystemExit('Use an output directory outside the source tree.')
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f'Aster-source-export-{version}'
    if destination.exists():
        raise SystemExit(f'Refusing to overwrite {destination}')
    template = ROOT / 'config/local.pri.example'
    if not template.is_file() or not included(template):
        raise SystemExit('Missing distributable config/local.pri.example; restore the dependency setup template before packaging.')
    for name in ('LICENSE', 'COPYRIGHT.md', 'SOURCE_CODE.md'):
        if not (ROOT / name).is_file():
            raise SystemExit(f'Missing required project notice: {name}')
    files = sorted(p for p in ROOT.rglob('*') if p.is_file() and included(p))
    payload = {p.relative_to(ROOT).as_posix(): p.read_bytes() for p in files}
    status = json.loads(payload['PACKAGE_STATUS.json'])
    if status['version'] != version:
        raise SystemExit('PACKAGE_STATUS version differs from VERSION; update release metadata before packaging.')
    manifest = {'project': 'Aster', 'version': version, 'files': [
        {'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        for name, data in payload.items()]}
    destination.mkdir()
    try:
        for name, data in payload.items():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            shutil.copymode(ROOT / name, target)
        (destination / 'PACKAGE_MANIFEST.json').write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        for item in manifest['files']:
            if hashlib.sha256((destination / item['path']).read_bytes()).hexdigest() != item['sha256']:
                raise RuntimeError('Source export verification failed: ' + item['path'])
    except Exception:
        shutil.rmtree(destination)
        raise
    print(f'{destination}\n{len(payload) + 1} files; contents verified')

if __name__ == '__main__':
    main()
