#!/usr/bin/env python3
"""Optional standard-library tool. Aster itself never needs Python or network access."""
import argparse
from decimal import Decimal, InvalidOperation
import hashlib
import json
import math
from pathlib import Path
import sys
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'resources' / 'astronomy' / 'solutions'
MAX_BYTES = 32 * 1024 * 1024


def catalogue():
    return json.loads((DATA / 'catalogue.json').read_text(encoding='utf-8'))['solutions']


def embedded(entry):
    compressed = (DATA / entry['resource']).read_bytes()
    if len(compressed) < 5 or int.from_bytes(compressed[:4], 'big') != entry['source_bytes']:
        raise ValueError('Invalid qCompress header: ' + entry['id'])
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed[4:], MAX_BYTES + 1)
    if (len(raw) != entry['source_bytes'] or len(raw) > MAX_BYTES or not decoder.eof
            or decoder.unused_data or decoder.unconsumed_tail
            or hashlib.sha256(raw).hexdigest() != entry['source_sha256']):
        raise ValueError('Checksum, length or compression failure: ' + entry['id'])
    return raw


def validate_pt(raw):
    """Same external PT schema/range contract as orbital_table.cpp, no resampling."""
    if not raw or len(raw) > MAX_BYTES:
        raise ValueError('PT data must be nonempty and at most 32 MiB')
    previous = None
    count = 0
    for number, line in enumerate(raw.decode('utf-8-sig').splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        values = [float(x.replace('D', 'E').replace('d', 'e')) for x in line.split()]
        if len(values) != 4 or not all(math.isfinite(x) for x in values):
            raise ValueError(f'Invalid PT row at line {number}')
        age, tilt, angle, climate = -values[0], *values[1:]
        if not (0 <= age <= 100000 and 0 <= tilt <= math.pi / 2
                and abs(angle) <= math.pi + 1e-10 and abs(climate) < 1):
            raise ValueError(f'Invalid physical range at line {number}')
        if (previous is None and age != 0) or (previous is not None and not 0 < age-previous <= 2):
            raise ValueError(f'Invalid PT time coverage/spacing at line {number}')
        previous = age
        count += 1
        if count > 500000:
            raise ValueError('Too many PT rows')
    if previous != 100000 or count < 100000:
        raise ValueError('PT table must contain the complete 0..100000 ka solution')
    return count


def write_new(path, raw):
    # Exclusive creation preserves existing user files. Download/validation is
    # already complete before this function is called.
    try:
        handle = path.open('xb')
    except FileExistsError:
        raise FileExistsError('Refusing to overwrite: ' + str(path)) from None
    try:
        with handle:
            handle.write(raw)
    except BaseException:
        path.unlink(missing_ok=True)
        raise


def pt_filename(ed, td):
    try:
        ed, td = Decimal(ed), Decimal(td)
        if not ed.is_finite() or not td.is_finite():
            raise ValueError('Ed/Td must be finite')
        if not (Decimal('.995') <= ed <= Decimal('1.005') and 0 <= td <= Decimal('1.2')):
            raise ValueError('Published grid: Ed .995..1.005, Td 0..1.2')
        if ed * 1000 != (ed * 1000).to_integral_value() or td * 10 != (td * 10).to_integral_value():
            raise ValueError('Published grid increments: Ed .001, Td .1')
    except InvalidOperation as error:
        raise ValueError('Invalid Ed/Td') from error
    return f'PT.De{ed:.4f}Td{td:.4f}.dat', float(ed), float(td)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('verify', help='Verify all embedded original files offline')
    unpack = sub.add_parser('unpack', help='Extract hash-verified original ASCII; never overwrite')
    unpack.add_argument('directory', type=Path)
    fetch = sub.add_parser('fetch-pt', help='Download one optional published NOAA PT table')
    fetch.add_argument('directory', type=Path)
    fetch.add_argument('--ed', default='1')
    fetch.add_argument('--td', default='1')
    args = parser.parse_args(argv)
    entries = catalogue()
    if args.command in ('verify', 'unpack'):
        verified = [(entry, embedded(entry)) for entry in entries.values()]
        if args.command == 'unpack':
            paths = [args.directory / entry['filename'] for entry, _ in verified]
            if any(p.exists() for p in paths):
                raise FileExistsError('Destination contains an original filename; nothing was written')
            args.directory.mkdir(parents=True, exist_ok=True)
            for path, (_, raw) in zip(paths, verified):
                write_new(path, raw)
        for entry, raw in verified:
            print(entry['id'], len(raw), hashlib.sha256(raw).hexdigest())
    else:
        filename, ed, td = pt_filename(args.ed, args.td)
        target = args.directory / filename
        receipt = target.with_suffix('.provenance.json')
        if target.exists() or receipt.exists():
            raise FileExistsError('Destination already exists; nothing was downloaded')
        url = entries['pt_zb18a']['data_directory_url'] + filename
        with urllib.request.urlopen(url, timeout=45) as response:
            raw = response.read(MAX_BYTES + 1)
        rows = validate_pt(raw)
        metadata = dict(filename=filename, source_url=url, ed=ed, td=td, rows=rows,
                        source_bytes=len(raw), source_sha256=hashlib.sha256(raw).hexdigest(),
                        verification='Downloaded over HTTPS; schema/range checked; retain this original file')
        args.directory.mkdir(parents=True, exist_ok=True)
        write_new(target, raw)
        write_new(receipt, (json.dumps(metadata, indent=2)+'\n').encode())
        print(target)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, zlib.error) as error:
        print('Error:', error, file=sys.stderr)
        sys.exit(1)
