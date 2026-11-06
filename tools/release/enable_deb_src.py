"""Generate matching APT source entries without editing system configuration.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
from pathlib import Path
import re
from urllib.parse import urlsplit


def convert(text):
    lines = []
    for line in text.splitlines():
        line = line.strip()
        if re.match(r'^deb\s', line):
            lines.append('deb-src' + line[3:])
    return lines


def official_uri(uri):
    parsed = urlsplit(uri)
    host = (parsed.hostname or '').lower()
    return parsed.scheme in ('http', 'https') and any(
        host == domain or host.endswith('.' + domain)
        for domain in ('ubuntu.com', 'debian.org'))


def legacy_entries(text):
    for line in text.splitlines():
        line = line.split('#', 1)[0].strip()
        match = re.match(r'^(deb|deb-src)\s+(?:\[[^\]]+\]\s+)?(\S+)', line)
        if match and official_uri(match[2]):
            yield match[1], line


def deb822_entries(text):
    """Retain complete fields, including embedded signing keys and restrictions."""
    for block in re.split(r'\n\s*\n', text.strip()):
        fields = []
        for line in block.splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            if line[0].isspace() and fields:
                fields[-1][1] += '\n' + line
                continue
            if ':' not in line or line[0].isspace():
                raise ValueError('Invalid deb822 source field')
            name, value = line.split(':', 1)
            if any(name.lower() == key.lower() for key, _ in fields):
                raise ValueError('Duplicate deb822 source field: ' + name)
            fields.append([name, value.lstrip()])
        values = {name.lower(): value for name, value in fields}
        if values.get('enabled', 'yes').strip().lower() in ('no', 'false', '0'):
            continue
        types = values.get('types', '').split()
        uris = [uri for uri in values.get('uris', '').split() if official_uri(uri)]
        if not uris or not set(types).intersection(('deb', 'deb-src')):
            continue
        if not values.get('suites', '').strip():
            raise ValueError('Missing Suites in enabled distribution repository')
        yield fields, types, uris


def collect(apt_root):
    legacy, existing, modern = [], set(), []
    found = False
    paths = [apt_root / 'sources.list']
    paths += sorted((apt_root / 'sources.list.d').glob('*.list'))
    for path in paths:
        if not path.is_file() or path.name == 'aster-deb-src.list':
            continue
        for kind, line in legacy_entries(path.read_text(encoding='utf-8-sig')):
            found = True
            if kind == 'deb-src':
                existing.add(line)
            else:
                legacy.append('deb-src' + line[3:])
    for path in sorted((apt_root / 'sources.list.d').glob('*.sources')):
        if path.name == 'aster-deb-src.sources':
            continue
        for fields, types, uris in deb822_entries(path.read_text(encoding='utf-8-sig')):
            found = True
            if 'deb-src' in types:
                continue
            modern.append('\n'.join(
                name + ': ' + ('deb-src' if name.lower() == 'types' else
                               ' '.join(uris) if name.lower() == 'uris' else value)
                for name, value in fields))
    if not found:
        raise ValueError('No enabled Ubuntu/Debian repositories found in .list or .sources files')
    return ([line for line in dict.fromkeys(legacy) if line not in existing],
            list(dict.fromkeys(modern)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apt-root', type=Path, default=Path('/etc/apt'))
    parser.add_argument('--output-dir', type=Path,
                        help='Write .list and .sources files to this staging directory')
    args = parser.parse_args()
    try:
        legacy, modern = collect(args.apt_root)
        if args.output_dir is None:
            if modern:
                raise ValueError('deb822 repositories found; use --output-dir to preserve .sources fields')
            print('\n'.join(legacy))
            return
        output = args.output_dir.resolve()
        apt_root = args.apt_root.resolve()
        if output == apt_root or apt_root in output.parents:
            raise ValueError('Use a staging directory outside the APT configuration directory')
        output.mkdir(parents=True, exist_ok=True)
        for extension, entries, separator in [('list', legacy, '\n'), ('sources', modern, '\n\n')]:
            path = output / ('aster-deb-src.' + extension)
            path.write_text('# Generated source entries; original APT configuration is unchanged.\n' +
                            separator.join(entries) + '\n', encoding='utf-8')
            print(f'{path}: {len(entries)} source entries')
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error


if __name__ == '__main__':
    main()
