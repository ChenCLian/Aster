"""Print matching deb-src lines for Ubuntu 22.04 CI; never edit apt settings.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
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


if __name__ == '__main__':
    paths = [Path('/etc/apt/sources.list')]
    paths += sorted(Path('/etc/apt/sources.list.d').glob('*.list'))
    result = sorted({line for path in paths if path.is_file()
                     for line in convert(path.read_text())
                     if any((urlsplit(token).hostname or '').endswith(('.ubuntu.com','.debian.org'))
                            for token in line.split() if token.startswith(('http://','https://')))})
    if not result:
        raise SystemExit('No deb-style repositories found. Enable Types: deb deb-src in the matching deb822 .sources files, then run apt-get update.')
    print('\n'.join(result))
