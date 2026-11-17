"""Verify bundled scientific resources before installing dependencies or compiling."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]


def resource_records(root):
    resources = root / 'resources'
    examples = json.loads((resources / 'examples/catalog.json').read_text(encoding='utf-8'))
    for item in examples['datasets'].values():
        yield resources / item['resource'].removeprefix(':/'), item['sha256'], None, False
    astronomy = resources / 'astronomy'
    item = json.loads((astronomy / 'la2004/manifest.json').read_text(encoding='utf-8'))
    yield astronomy / 'la2004' / item['filename'], item['sha256'], None, False
    solutions = json.loads((astronomy / 'solutions/catalogue.json').read_text(encoding='utf-8'))
    for item in solutions['solutions'].values():
        yield astronomy / 'solutions' / item['resource'], item['source_sha256'], item['source_bytes'], True
    item = json.loads((resources / 'synthetic/model.json').read_text(encoding='utf-8'))
    yield resources / 'synthetic/depth_m.txt', item['template_sha256'], None, False


def check(root=ROOT):
    errors, count = [], 0
    for path, expected, size, compressed in resource_records(root):
        count += 1
        try:
            data = path.read_bytes()
            if compressed:
                if len(data) < 4:
                    raise ValueError('truncated qCompress resource')
                declared = struct.unpack('>I', data[:4])[0]
                data = zlib.decompress(data[4:])
                if len(data) != declared:
                    raise ValueError('qCompress length header does not match payload')
            if size is not None and len(data) != size:
                raise ValueError(f'expected {size} bytes, got {len(data)}')
            actual = hashlib.sha256(data).hexdigest()
            if actual != expected:
                hint = ''
                if hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest() == expected:
                    hint = '; CRLF conversion detected: restore the original LF bytes'
                raise ValueError(f'SHA-256 mismatch (expected {expected}, got {actual}){hint}')
        except (OSError, ValueError, zlib.error) as error:
            errors.append(f'{path.relative_to(root).as_posix()}: {error}')
    return count, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        count, errors = check(args.root.resolve())
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f'Resource catalogue error: {error}') from error
    if errors:
        raise SystemExit('Bundled resource verification failed:\n' + '\n'.join(errors))
    print(f'Bundled resources: {count} SHA-256 checks passed')


if __name__ == '__main__':
    main()
