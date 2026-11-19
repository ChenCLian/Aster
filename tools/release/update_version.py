"""Synchronize the release version without rewriting user documentation.

Examples (run from the repository root, Python 3.10+):
  python tools/release/update_version.py NEW_VERSION --notes "Brief release note."
  python tools/release/update_version.py NEW_VERSION --notes-file notes.md --dry-run
  python tools/release/update_version.py --check

VERSION remains the only application-version source. This script never builds,
executes scientific tests, publishes a release, or marks old tests as new passes.
"""
# Copyright (C) 2026 ZiJie Chen
# SPDX-License-Identifier: GPL-3.0-only
import argparse
from dataclasses import dataclass
from datetime import date
from fnmatch import fnmatchcase
import json
import os
from pathlib import Path
import posixpath
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = re.compile(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-((?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*))?')
# Historical links and explicitly protected prose are never version-stamped.
PROTECTED = re.compile(r'<!-- version-sync: off -->.*?<!-- version-sync: on -->|\[[^\]\n]*\]\((?:\.\./)*(?:docs/)?(?:history|validation_results)/[^)\n]+\)', re.S)
TOKENS = re.compile(r'(?<![\w.])v?(\d+\.\d+\.\d+-(?:alpha|beta|rc)\.\d+)\b')


def version_key(value):
    match = VERSION.fullmatch(value)
    if len(value) > 128 or not match:
        raise ValueError('Use MAJOR.MINOR.PATCH[-prerelease], without v or build metadata: ' + value)
    major, minor, patch, pre = match.groups()
    # SemVer: numeric identifiers precede text, and a final release follows prereleases.
    suffix = tuple((0, int(x)) if x.isdigit() else (1, x) for x in pre.split('.')) if pre else ()
    return int(major), int(minor), int(patch), not bool(pre), suffix


def policy(root):
    result = json.loads((root / 'tools/release/version_policy.json').read_text(encoding='utf-8'))
    if result['schema'] != 1:
        raise ValueError('Unsupported version policy schema')
    return result


def current_documents(root):
    rules = policy(root)
    paths = set()
    for pattern in rules['current_documents']:
        for path in root.glob(pattern):
            name = path.relative_to(root).as_posix()
            if path.is_file() and not any(fnmatchcase(name, p) for p in rules['exclude']):
                paths.add(path)
    return sorted(paths)


def unprotected(text, transform):
    parts, start = [], 0
    for match in PROTECTED.finditer(text):
        parts.extend((transform(text[start:match.start()]), match[0]))
        start = match.end()
    return ''.join(parts) + transform(text[start:])


def replace_version(text, old, new):
    token = re.compile(r'(?<![\w.])(v?)' + re.escape(old) + r'(?![\w]|\.[0-9])')
    def substitute(part):
        return token.sub(lambda m: m[1] + new, part)
    def rewrite(part):
        if '-' in old:
            return substitute(part)
        # Bare stable numbers also identify dependencies and methods. Restrict
        # stable-version updates to app names and explicit version declarations.
        lines = []
        for line in part.splitlines(keepends=True):
            if re.search(r'`VERSION`|适用版本|适用源码|当前版本|源码版本|Source version|--tag|git (?:tag|checkout)', line):
                line = substitute(line)
            else:
                line = re.sub(r'(\bAster[ -])' + re.escape(old) + r'(?![0-9]|\.[0-9]|-(?!windows|macos|linux)[A-Za-z])',
                              lambda m: m[1] + new, line)
            lines.append(line)
        return ''.join(lines)
    return unprotected(text, rewrite)


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


@dataclass
class Change:
    path: Path
    before: bytes | None
    after: bytes


def check(root):
    errors = []
    version = (root / 'VERSION').read_text(encoding='utf-8').strip()
    version_key(version)
    rules = policy(root)
    for name in rules['required_headers']:
        path = root / name
        if not path.is_file() or version not in '\n'.join(path.read_text(encoding='utf-8').splitlines()[:8]):
            errors.append('Missing current version in header: ' + name)
    for path in current_documents(root):
        text = path.read_text(encoding='utf-8')
        if text.count('<!-- version-sync: off -->') != text.count('<!-- version-sync: on -->'):
            errors.append('Unbalanced version-sync markers: ' + str(path.relative_to(root)))
        active = unprotected(text, lambda s: s)
        # Remove protected spans for inspection (unprotected retains them for editing).
        active = PROTECTED.sub('', active)
        for token in TOKENS.findall(active):
            # Product-series references such as 1.3.0 in prerelease documentation
            # are intentional; explicit prerelease references must match exactly.
            if token != version and ('-' in token or token != version.split('-')[0]):
                errors.append('Stale version ' + token + ': ' + str(path.relative_to(root)))
    status = json.loads((root / 'PACKAGE_STATUS.json').read_text(encoding='utf-8'))
    if status['version'] != version:
        errors.append('PACKAGE_STATUS.json differs from VERSION')
    review = status['documentation_review']
    if review['version'] != version:
        errors.append('Documentation review version differs from VERSION')
    for name, required in [('validation', None), ('documentation_review', 'documentation')]:
        record = root / status[name]['record']
        if not record.is_file():
            errors.append('Missing ' + name + ' record')
            continue
        evidence = json.loads(record.read_text(encoding='utf-8'))
        if evidence.get('version') != version or (required and required not in evidence):
            errors.append('Invalid version/scope in ' + name + ' record')
    if status['validation']['record'] == review['record']:
        errors.append('Documentation and application validation require separate records')
    return sorted(set(errors))


def archive_links(text, directory):
    """Keep archived root-document links working from their new directory."""
    def link(match):
        dest = match[2]
        if re.match(r'[A-Za-z][\w+.-]*:', dest) or dest.startswith(('/', '#', '<')):
            return match[0]
        path, sep, fragment = dest.partition('#')
        if path in ('RELEASE_NOTES.md', 'REVIEW_CN.md'):
            return match[0]  # Both root documents are archived together.
        return match[1] + posixpath.relpath(path, directory) + (sep + fragment if sep else '') + ')'
    return re.sub(r'(\[[^\]\n]*\]\()([^\s)]+)\)', link, text)


def plan(root, new, notes):
    old = (root / 'VERSION').read_text(encoding='utf-8').strip()
    if version_key(new) <= version_key(old):
        raise ValueError('New version must be greater than ' + old + '; use --check for an unchanged version')
    if not notes.strip():
        raise ValueError('Supply a short release note or a notes file; old notes are never reused')
    if re.match(r'\s*#\s+Aster\s+\d+\.', notes):
        raise ValueError('--notes-file takes the body only; the version heading is generated')
    errors = check(root)
    if errors:
        raise ValueError('Fix current synchronization before bumping:\n' + '\n'.join(errors))
    changes = {}
    def add(name, data, exclusive=False):
        path = root / name
        if not path.resolve().is_relative_to(root.resolve()) or any(p.is_symlink() for p in [path, *path.parents] if p.is_relative_to(root)):
            raise ValueError('Refusing a symlink or path outside the project: ' + name)
        before = path.read_bytes() if path.exists() else None
        if exclusive and before is not None:
            raise ValueError('Refusing to overwrite historical evidence: ' + name)
        after = data.encode('utf-8') if isinstance(data, str) else data
        if before != after:
            changes[name] = Change(path, before, after)
    for path in current_documents(root):
        raw = path.read_bytes()
        add(path.relative_to(root).as_posix(), replace_version(raw.decode('utf-8'), old, new))
    archived_names = ['RELEASE_NOTES.md']
    if 'REVIEW_CN.md' in (root / 'RELEASE_NOTES.md').read_text(encoding='utf-8'):
        archived_names.append('REVIEW_CN.md')
    for name in archived_names:
        if (root / name).exists():
            directory = 'docs/history/releases/' + old
            add(directory + '/' + name, archive_links((root / name).read_text(encoding='utf-8'), directory), True)
    prefix = 'docs/validation_results/' + new
    status = json.loads((root / 'PACKAGE_STATUS.json').read_text(encoding='utf-8'))
    previous = status['validation']['record']
    previous_docs = status['documentation_review']['record']
    status['version'] = new
    status['release_channel'] = new.split('-', 1)[1].split('.')[0] if '-' in new else 'stable'
    status['validation'] = {'status': 'pending', 'record': prefix + '/checks.json',
        'summary': 'docs/validation_current.md', 'logs': prefix, 'source_manifest': 'PACKAGE_MANIFEST.json',
        'scope': 'Version synchronized; this version has not yet been validated.',
        'previous_version_record': previous, 'previous_version_summary': previous}
    status['documentation_review'] = {'date': date.today().isoformat(), 'version': new,
        'status': 'version_synchronized_content_review_not_implied', 'record': prefix + '/documentation_checks.json',
        'embedded_help_rebuild_required': True, 'scope': 'Version tokens synchronized; existing document bodies retained.',
        'previous_record': previous_docs}
    add('VERSION', new + '\n')
    add('PACKAGE_STATUS.json', json_bytes(status))
    add('RELEASE_NOTES.md', '# Aster ' + new + ' 发布说明\n\n' + notes.strip() + '\n')
    add(prefix + '/checks.json', json_bytes({'version': new, 'status': 'pending',
        'previous_record': previous, 'scope': 'No build or application test is performed by update_version.py.'}), True)
    add(prefix + '/documentation_checks.json', json_bytes({'version': new, 'documentation': {
        'status': 'version_sync_only', 'content_rewritten': False,
        'scope': 'Mechanical version synchronization only; release notes supplied by maintainer.'}}), True)
    add('docs/validation_current.md', f'''# Aster {new} 验证范围

[本版记录](validation_results/{new}/checks.json) · [文档记录](validation_results/{new}/documentation_checks.json)

本版已同步版本号，尚未完成构建与应用回归。版本更新脚本不执行测试，也不将旧版本的通过结果视为本版证据。

上一个版本的记录见[历史验证]({posixpath.relpath(previous, 'docs')})。完成相应检查后，在本版记录中填写实际范围、结果和未覆盖项。
''')
    if (root / 'REVIEW_CN.md').is_file():
        add('REVIEW_CN.md', f'''# Aster {new} 更新说明

本次发布内容见 [RELEASE_NOTES.md](RELEASE_NOTES.md)。常规文档仅同步版本号，正文沿用。

构建与验证状态见[本版验证范围](docs/validation_current.md)。版本脚本不代替编译、测试或人工文档补充。
''')
    return list(changes.values())


def write_changes(changes):
    """Stage all writes first; roll back normal I/O failures and preserve permissions."""
    staged, committed = [], []
    try:
        for change in changes:
            actual = change.path.read_bytes() if change.path.exists() else None
            if actual != change.before:
                raise ValueError('File changed since planning: ' + str(change.path))
        for change in changes:
            change.path.parent.mkdir(parents=True, exist_ok=True)
            fd, name = tempfile.mkstemp(prefix='.version-', dir=change.path.parent)
            temp = Path(name)
            staged.append((change, temp))
            with os.fdopen(fd, 'wb') as stream:
                stream.write(change.after)
            os.chmod(temp, change.path.stat().st_mode & 0o777 if change.before is not None else 0o644)
        for change, temp in staged:
            os.replace(temp, change.path)
            committed.append(change)
    except BaseException:
        for change in reversed(committed):
            if change.before is None:
                change.path.unlink(missing_ok=True)
            else:
                change.path.write_bytes(change.before)
        raise
    finally:
        for _, temp in staged:
            temp.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('version', nargs='?')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--dry-run', action='store_true')
    note = parser.add_mutually_exclusive_group()
    note.add_argument('--notes')
    note.add_argument('--notes-file', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.check:
            if args.version or args.notes or args.notes_file or args.dry_run:
                parser.error('--check is used alone')
            errors = check(ROOT)
            if errors:
                raise ValueError('\n'.join(errors))
            print('Version and documentation references are synchronized (not a build/test verdict).')
            return 0
        if not args.version or (args.notes is None and args.notes_file is None):
            parser.error('Provide NEW_VERSION and --notes or --notes-file')
        notes = args.notes_file.read_text(encoding='utf-8-sig') if args.notes_file else args.notes
        changes = plan(ROOT, args.version, notes)
        for change in changes:
            print(('CREATE ' if change.before is None else 'UPDATE ') + str(change.path.relative_to(ROOT)))
        if not args.dry_run:
            write_changes(changes)
            if (ROOT / "resources/help.qrc").is_file():
                from document_links import manifest_text
                (ROOT / "resources/manual_files.json").write_text(manifest_text(ROOT), encoding="utf-8")
        print(f'{len(changes)} files; ' + ('preview only, no files written.' if args.dry_run else 'synchronized. Rebuild the application to update its version and embedded help.'))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print('Version update failed: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
