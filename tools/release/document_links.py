"""One Markdown source: portable repository links and generated Release URLs."""
from pathlib import Path, PurePosixPath
import argparse
import posixpath
import re
from urllib.parse import quote, unquote, urlsplit
import shutil

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = 'ChenCLian/AstroBatch'
INLINE = re.compile(r'!?\[[^\]\n]*\]\(\s*(?P<url><[^>\n]+>|[^\s)]+)(?:\s+[^)]*)?\)')
REFERENCE = re.compile(r'^\s{0,3}\[[^\]\n]+\]:\s*(?P<url><[^>\n]+>|\S+)', re.M)
HTML_LINK = re.compile(r'(?:href|src)=["\'](?P<url>[^"\']+)["\']', re.I)


def editable_blocks(text):
    fence = False
    for line in text.splitlines(keepends=True):
        if re.match(r'^\s*(```|~~~)', line):
            fence = not fence
            yield line, False
        else:
            yield line, not fence


def destinations(line):
    found = {}
    for pattern in (INLINE, REFERENCE, HTML_LINK):
        for match in pattern.finditer(line):
            found[match.span('url')] = match.group('url')
    return sorted(found.items())


def own_path(url):
    parsed = urlsplit(url)
    if parsed.scheme != 'https' or parsed.netloc not in ('github.com', 'gitee.com'):
        return None
    match = re.fullmatch(r'/' + REPOSITORY + r'/(blob|tree)/(main|master|release-stable|release-beta)/(.+)', parsed.path)
    if not match or parsed.query:
        return None
    return unquote(match[3]) + ('/' if match[1] == 'tree' else ''), parsed.fragment


def map_links(text, transform):
    lines = []
    for line, editable in editable_blocks(text):
        if editable:
            for (start, end), url in reversed(destinations(line)):
                bracketed = url.startswith('<') and url.endswith('>')
                result = transform(url[1:-1] if bracketed else url)
                if bracketed or ' ' in result:
                    result = '<' + result + '>'
                line = line[:start] + result + line[end:]
        lines.append(line)
    return ''.join(lines)


def portable(text, document, root=ROOT):
    def replace(url):
        target = own_path(url)
        if target is None:
            return url
        path, anchor = target
        resolved = (root / path).resolve()
        if not resolved.is_relative_to(root.resolve()) or not resolved.exists():
            return url
        relative = posixpath.relpath(path.rstrip('/'), document.parent.as_posix())
        if path.endswith('/'):
            relative += '/'
        return relative + ('#' + anchor if anchor else '')
    return map_links(text, replace)


def resolve_local(url, document):
    parsed = urlsplit(url)
    if parsed.scheme or parsed.netloc or parsed.query or not parsed.path:
        return None
    path = unquote(parsed.path)
    if path.startswith('/'):
        # Root-relative repository links break in local Markdown viewers.
        return path.lstrip('/')
    path = posixpath.normpath(posixpath.join(document.parent.as_posix(), path))
    if path == '..' or path.startswith('../'):
        raise ValueError('Link escapes the repository: ' + url)
    return path


def release_body(text, document=PurePosixPath('RELEASE_NOTES.md'), host='github.com', ref='main'):
    if host not in ('github.com', 'gitee.com') or not re.fullmatch(r'[A-Za-z0-9_.-]+', ref):
        raise ValueError('Invalid documentation host/ref')
    def replace(url):
        local = resolve_local(url, document)
        if local is None:
            return url
        fragment = urlsplit(url).fragment
        kind = 'tree' if urlsplit(url).path.endswith('/') else 'blob'
        return f'https://{host}/{REPOSITORY}/{kind}/{quote(ref, safe="")}/{quote(local, safe="/")}' + ('#'+fragment if fragment else '')
    return map_links(text, replace)


def mirror_release_body(text, tag):
    from download_metadata import valid_version, PLATFORMS
    version = tag.removeprefix('v')
    if tag != 'v' + version or not valid_version(version):
        raise ValueError('Invalid version tag')
    prefix = f'https://github.com/{REPOSITORY}/releases/download/{tag}/AstroBatch-{version}-'
    originals = {prefix + platform + ('.tar.gz' if platform == 'linux-x64' else '.zip')
                 for platform in PLATFORMS}
    def replace(url):
        return url.replace('https://github.com/', 'https://gitee.com/', 1) if url in originals else url
    return map_links(release_body(text, host='github.com', ref=tag), replace)


def manual_files(root=ROOT):
    # The embedded manual is the entry list; follow local links to include all
    # referenced document pages and small text/image resources in installed packages.
    import xml.etree.ElementTree as ET
    seeds = [node.get('alias') for node in ET.parse(root/'resources/help.qrc').iter('file')]
    pending = [PurePosixPath(p) for p in seeds if p and (root/p).is_file()]
    seen = set()
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        file = root/name
        if not file.is_file() or file.is_symlink() or file.stat().st_size > 4*1024*1024:
            continue
        if any(part in ('dependency-materials', 'dependency-cache', 'release-assets', '.git') for part in name.parts):
            continue
        seen.add(name)
        if file.suffix.lower() != '.md':
            continue
        for line, editable in editable_blocks(file.read_text(encoding='utf-8')):
            if not editable:
                continue
            for _, raw in destinations(line):
                local = resolve_local(raw.strip('<>'), name)
                if local:
                    target = root/local
                    if target.is_dir() and (target/'README.md').is_file():
                        local += '/README.md'
                    pending.append(PurePosixPath(local))
    return seen


def manifest_text(root=ROOT):
    import json
    return json.dumps(sorted(str(p) for p in manual_files(root)), ensure_ascii=False, indent=2)+"\n"


def stage_manual(destination, root=ROOT):
    for relative in manual_files(root):
        target = destination/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/relative, target)


def check(root=ROOT):
    errors = []
    for relative in manual_files(root):
        file = root/relative
        if file.suffix != '.md':
            continue
        for line, editable in editable_blocks(file.read_text(encoding='utf-8')):
            if not editable:
                continue
            for _, raw in destinations(line):
                url = raw.strip('<>')
                if own_path(url):
                    errors.append(f'{relative}: hard-coded own-repository document URL: {url}')
                local = resolve_local(url, relative)
                if local and not (root/local).exists():
                    # Generated in each binary package, not a source-tree document.
                    if local != 'SOURCE_DEPENDENCIES.md':
                        errors.append(f'{relative}: missing local target: {url}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--normalize', action='store_true')
    parser.add_argument('--write-manifest', action='store_true')
    parser.add_argument('--stage', type=Path)
    args = parser.parse_args()
    if args.normalize:
        count = 0
        for p in ROOT.rglob('*.md'):
            if any(part in ('validation_results', 'history') for part in p.parts):
                continue
            text = p.read_text(encoding='utf-8'); changed = portable(text, PurePosixPath(p.relative_to(ROOT).as_posix()))
            if text != changed:
                p.write_text(changed, encoding='utf-8'); count += 1
        print(f'Normalized {count} Markdown files')
    if args.stage:
        stage_manual(args.stage)
    manifest = ROOT/"resources/manual_files.json"
    if args.write_manifest:
        manifest.write_text(manifest_text(), encoding="utf-8")
    errors = check()
    if not manifest.exists() or manifest.read_text(encoding="utf-8") != manifest_text():
        errors.append("Manual file list is stale; run document_links.py --write-manifest")
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'Portable manual links checked; {len(manual_files())} packaged resources')


if __name__ == '__main__':
    main()
