"""Stable download names shared by publication, dependency links and reuse.

Keep original archive names locally (notably Debian .dsc references). Only
publication names are normalized; the release includes the reversible mapping.
"""
import re


def asset_name(name):
    if not isinstance(name, str) or not name or any(c in name for c in '/\\\x00\r\n') or name in ('.', '..'):
        raise ValueError('Release asset must have a plain filename')
    # GitHub changes special characters on upload. Use a conservative alphabet
    # ourselves so checksums and dependency links agree before any upload starts.
    normalized = re.sub(r'[^A-Za-z0-9._-]', '_', name).strip('.')
    if not normalized:
        raise ValueError('Release asset name is empty after normalization')
    return normalized


def validate_names(names):
    seen = {}
    for name in names:
        normalized = asset_name(name)
        if normalized in seen:
            raise ValueError(f'Release filename collision: {seen[normalized]!r} and {name!r} -> {normalized!r}')
        seen[normalized] = name
    return seen


def legacy_asset_names(name):
    # Conservative candidates only; the digest must still match. Never adopt an
    # arbitrary attachment just because it happens to contain the same bytes.
    return {name, re.sub(r'[^A-Za-z0-9._-]', '.', name).strip('.'),
            re.sub(r'[^A-Za-z0-9._-]', '', name).strip('.')}
