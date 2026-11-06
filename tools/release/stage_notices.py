"""Copy project and dependency notices into a distribution directory."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--dependency-prefix', type=Path, action='append', default=[])
    args = parser.parse_args()
    dest = args.destination.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    notices = dest / 'licenses'
    notices.mkdir(exist_ok=True)
    for name in ['README.md', 'AUTHORS.md', 'QUICK_START.md', 'FAQ.md',
                 'LICENSE', 'COPYRIGHT.md', 'SOURCE_CODE.md']:
        shutil.copy2(ROOT / name, dest / name)
    icon = dest / 'resources/app/aster.png'
    icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'resources/app/aster.png', icon)
    # Release notes may be maintained on GitHub rather than in the checkout.
    # Keep author and third-party notices required; only release notes are optional.
    release_notes = ROOT / 'RELEASE_NOTES.md'
    if release_notes.is_file():
        shutil.copy2(release_notes, dest / release_notes.name)
    shutil.copy2(ROOT / 'docs/third_party.md', notices)
    for folder in ['licenses', 'src/third_party', 'resources']:
        for p in (ROOT / folder).rglob('*'):
            if p.is_file() and (p.suffix.lower() in {'.md', '.txt'} or
                               p.name.startswith(('COPYING', 'COPYRIGHT', 'LICENSE'))):
                relative = p.relative_to(ROOT / 'licenses') if folder == 'licenses' else p.relative_to(ROOT)
                target = notices / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(p, target)
    for prefix in [args.qt_prefix, *args.dependency_prefix]:
        if not prefix.exists():
            raise SystemExit(f'Dependency prefix not found: {prefix}')
        for p in prefix.iterdir():
            if p.name.lower().startswith(('license', 'copying', 'copyright')):
                target = notices / 'dependencies' / prefix.name / p.name
                target.parent.mkdir(parents=True, exist_ok=True)
                if p.is_dir():
                    shutil.copytree(p, target, dirs_exist_ok=True)
                elif p.is_file():
                    shutil.copy2(p, target)
    # Debian/Ubuntu distribute system library copyright files here.
    doc = Path('/usr/share/doc')
    if doc.exists():
        for p in doc.iterdir():
            if p.name.startswith(('libgsl', 'libopenblas', 'liblapack', 'libgfortran',
                                  'libquadmath', 'libssl', 'libqt6')) and (p/'copyright').is_file():
                shutil.copy2(p/'copyright', notices / (p.name + '-copyright.txt'))

if __name__ == '__main__':
    main()
