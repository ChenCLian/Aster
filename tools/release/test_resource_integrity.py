"""Catch byte changes introduced by platform checkout before building Qt resources."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zlib

import check_resource_integrity as integrity


class ResourceIntegrityTests(unittest.TestCase):
    def test_actual_bundled_resources(self):
        count, errors = integrity.check()
        self.assertGreaterEqual(count, 15)
        self.assertEqual(errors, [])

    def test_corruption_missing_and_crlf_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            original = b'Age\tValue\n1\t2\n'
            expected = hashlib.sha256(original).hexdigest()
            path = root / 'example.tsv'
            with patch.object(integrity, 'resource_records', return_value=[(path, expected, None, False)]):
                self.assertIn('example.tsv', integrity.check(root)[1][0])
                path.write_bytes(original.replace(b'\n', b'\r\n'))
                self.assertIn('CRLF conversion detected', integrity.check(root)[1][0])
                path.write_bytes(original.replace(b'2', b'9'))
                self.assertIn('SHA-256 mismatch', integrity.check(root)[1][0])
                path.write_bytes(original)
                self.assertEqual(integrity.check(root)[1], [])

    def test_compressed_length_and_hash_are_verified(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            path = root / 'orbit.qz'
            data = b'original orbital table\n'
            digest = hashlib.sha256(data).hexdigest()
            record = (path, digest, len(data), True)
            with patch.object(integrity, 'resource_records', return_value=[record]):
                path.write_bytes(struct.pack('>I', len(data)) + zlib.compress(data))
                self.assertEqual(integrity.check(root)[1], [])
                path.write_bytes(struct.pack('>I', len(data) + 1) + zlib.compress(data))
                self.assertIn('length header', integrity.check(root)[1][0])
                path.write_bytes(b'bad')
                self.assertIn('truncated', integrity.check(root)[1][0])

    @unittest.skipUnless(shutil.which('git'), 'git is required for checkout simulation')
    def test_windows_autocrlf_checkout_preserves_original_bytes(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            def git(*args):
                return subprocess.run(['git', '-C', str(root), *args], check=True,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            git('init', '-q')
            git('config', 'core.autocrlf', 'true')
            shutil.copy2(integrity.ROOT / '.gitattributes', root / '.gitattributes')
            originals = {}
            for path, _, _, compressed in integrity.resource_records(integrity.ROOT):
                if not compressed:
                    relative = path.relative_to(integrity.ROOT)
                    destination = root / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    originals[relative] = path.read_bytes()
                    destination.write_bytes(originals[relative])
            git('add', '.')
            for relative in originals:
                (root / relative).unlink()
            git('checkout-index', '-a', '-f')
            for relative, data in originals.items():
                self.assertEqual((root / relative).read_bytes(), data, str(relative))


if __name__ == '__main__':
    unittest.main()
