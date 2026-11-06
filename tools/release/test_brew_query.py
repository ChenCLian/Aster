"""Execute the Homebrew metadata query without the optional Ruby base64 gem.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import base64
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependency_providers as providers


# Exercise the production Ruby query rather than mocking its JSON response.
# On macOS use the same Homebrew Ruby interpreter as the collector. Formula
# objects are controlled fixtures; no installed formula or network is needed.
BOOTSTRAP = r'''
module Kernel
  alias_method :aster_fixture_require, :require
  def require(name)
    raise LoadError, "base64 deliberately unavailable" if name == "base64"
    return false if ["formula", "formulary"].include?(name)
    aster_fixture_require(name)
  end
end
[:Formulary, :LocalPatch].each do |name|
  Object.send(:remove_const, name) if Object.const_defined?(name, false)
end
FixtureSource = Struct.new(:url, :checksum, :resources, :patches)
class LocalPatch
  def strip; :p1; end
  def directory; nil; end
  def file; "Patches/gcc/gcc.diff"; end
  def contents; raise "Local patches must not be read as embedded patches"; end
end
class FixtureStringPatch
  def strip; :p0; end
  def contents; [ARGV.fetch(1)].pack("H*"); end
end
class FixtureExternalPatch
  def strip; :p1; end
  def directory; "nested"; end
  def resource; FixtureSource.new("https://example.org/fix.patch", "b" * 64); end
end
module Formulary
  def self.factory(path)
    raise "Wrong formula argument" unless path == "fixture.rb"
    resource = FixtureSource.new("https://example.org/resource.tar.gz", "c" * 64,
                                 {}, [FixtureStringPatch.new])
    patches = [LocalPatch.new, FixtureStringPatch.new, FixtureExternalPatch.new]
    resources = {"resource" => resource}
    if ARGV.fetch(2) == "plain"
      patches = []
      resources = {}
    end
    stable = FixtureSource.new("https://example.org/gcc.tar.xz", "a" * 64,
                               resources, patches)
    Struct.new(:stable, :name, :pkg_version).new(stable, "gcc", "16.2.0")
  end
end
'''


class BrewQueryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform == 'darwin' and shutil.which('brew'):
            cls.ruby = [shutil.which('brew'), 'ruby']
        else:
            ruby = os.environ.get('ASTER_TEST_RUBY') or shutil.which('ruby')
            if not ruby:
                raise unittest.SkipTest('Ruby unavailable; macOS CI uses Homebrew Ruby')
            cls.ruby = [ruby]

    def query(self, mode, payload=b''):
        result = subprocess.run(
            self.ruby + ['-e', BOOTSTRAP + providers.BREW_QUERY, '--',
                         'fixture.rb', payload.hex(), mode],
            capture_output=True, text=True, timeout=60, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_formula_without_patches_needs_no_base64_gem(self):
        metadata = self.query('plain')
        self.assertEqual(metadata['name'], 'gcc')
        self.assertEqual(metadata['version'], '16.2.0')
        self.assertEqual(metadata['patches'], [])
        self.assertEqual(metadata['sources'], [
            {'url': 'https://example.org/gcc.tar.xz', 'sha256': 'a' * 64, 'kind': 'source'}])

    def test_all_patch_kinds_preserve_bytes_and_scope_without_base64_gem(self):
        # Binary bytes, UTF-8, CRLF and a long line must survive Ruby -> JSON -> Python.
        payload = bytes(range(256)) + '补丁\r\n'.encode('utf-8') + b'x' * 200
        metadata = self.query('patches', payload)
        patches = metadata['patches']
        self.assertEqual([p['kind'] for p in patches], ['local', 'embedded', 'external', 'embedded'])
        self.assertEqual([(p['scope'], p['order']) for p in patches], [(0, 0), (0, 1), (0, 2), (1, 0)])
        self.assertEqual(patches[0]['path'], 'Patches/gcc/gcc.diff')
        self.assertEqual(patches[0]['strip'], 'p1')
        self.assertEqual(patches[2]['directory'], 'nested')
        self.assertEqual(patches[2]['sha256'], 'b' * 64)
        for patch in [patches[1], patches[3]]:
            self.assertEqual(patch['data'], base64.b64encode(payload).decode('ascii'))
            self.assertEqual(base64.b64decode(patch['data'], validate=True), payload)
            self.assertEqual(patch['strip'], 'p0')
        self.assertEqual([s['kind'] for s in metadata['sources']], ['source', 'resource', 'patch'])
        self.assertEqual([s['sha256'] for s in metadata['sources']], ['a' * 64, 'c' * 64, 'b' * 64])


if __name__ == '__main__':
    unittest.main()
