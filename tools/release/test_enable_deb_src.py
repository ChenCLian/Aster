"""Regression tests for source repositories on release build hosts.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import enable_deb_src


class SourceRepositoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.apt = self.root / 'apt'
        self.parts = self.apt / 'sources.list.d'
        self.parts.mkdir(parents=True)
        self.source = self.parts / 'ubuntu.sources'
        self.stanza = (
            'Types: deb\n'
            'URIs: http://azure.archive.ubuntu.com/ubuntu\n'
            'Suites: jammy jammy-updates jammy-backports\n'
            'Components: main restricted universe multiverse\n'
            'Architectures: amd64\n'
            'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n'
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_deb822_only_runner_preserves_suites_components_and_signature(self):
        self.source.write_text(self.stanza)
        legacy, modern = enable_deb_src.collect(self.apt)
        self.assertEqual(legacy, [])
        self.assertEqual(modern, [self.stanza.replace('Types: deb\n', 'Types: deb-src\n').strip()])

    def test_disabled_and_third_party_sources_are_not_enabled(self):
        self.source.write_text(self.stanza + '\n' + self.stanza + 'Enabled: no\n\n' +
                               self.stanza.replace('azure.archive.ubuntu.com', 'packages.vendor.example'))
        _, modern = enable_deb_src.collect(self.apt)
        self.assertEqual(len(modern), 1)
        self.assertNotIn('vendor', modern[0])

    def test_existing_source_type_is_success_without_duplicate(self):
        self.source.write_text(self.stanza.replace('Types: deb', 'Types: deb deb-src'))
        self.assertEqual(enable_deb_src.collect(self.apt), ([], []))

    def test_legacy_source_keeps_options_and_skips_existing_entry(self):
        line = 'deb [arch=amd64 signed-by=/usr/share/keyrings/a.gpg] https://archive.ubuntu.com/ubuntu jammy main'
        existing = line.replace(' jammy ', ' jammy-updates ')
        (self.apt / 'sources.list').write_text(line + '\n' + existing + '\n' +
                                             'deb-src' + existing[3:] + '\n')
        self.assertEqual(enable_deb_src.collect(self.apt), (['deb-src' + line[3:]], []))

    def test_mixed_formats_and_repeated_generation(self):
        self.source.write_text(self.stanza)
        (self.parts / 'security.list').write_text('deb https://security.ubuntu.com/ubuntu jammy-security main\n')
        first = enable_deb_src.collect(self.apt)
        (self.parts / 'aster-deb-src.list').write_text('\n'.join(first[0]))
        (self.parts / 'aster-deb-src.sources').write_text('\n\n'.join(first[1]))
        self.assertEqual(enable_deb_src.collect(self.apt), first)
        self.assertEqual(tuple(map(len, first)), (1, 1))

    def test_embedded_key_and_multiline_values_are_preserved(self):
        key = 'Signed-By:\n -----BEGIN PGP PUBLIC KEY BLOCK-----\n .\n testdata\n -----END PGP PUBLIC KEY BLOCK-----\n'
        stanza = self.stanza.split('Signed-By:')[0] + key + 'Check-Valid-Until: yes\n'
        stanza = stanza.replace('jammy jammy-updates', 'jammy\n jammy-updates')
        self.source.write_text(stanza)
        _, modern = enable_deb_src.collect(self.apt)
        self.assertIn(key.replace('Signed-By:\n', 'Signed-By: \n'), modern[0])
        self.assertIn('Suites: jammy\n jammy-updates jammy-backports', modern[0])
        self.assertIn('Check-Valid-Until: yes', modern[0])

    def test_uri_filter_checks_host_not_comments_or_lookalike_domains(self):
        self.source.write_text(self.stanza.replace('azure.archive.ubuntu.com', 'ubuntu.com.example.org'))
        (self.apt / 'sources.list').write_text(
            'deb https://vendor.example/ubuntu jammy main # https://archive.ubuntu.com\n')
        with self.assertRaisesRegex(ValueError, 'No enabled'):
            enable_deb_src.collect(self.apt)

    def test_multiple_uris_keep_only_matching_distribution_hosts(self):
        self.source.write_text(self.stanza.replace('http://azure.archive.ubuntu.com/ubuntu',
            'https://archive.ubuntu.com/ubuntu\n https://vendor.example/repo'))
        _, modern = enable_deb_src.collect(self.apt)
        self.assertIn('URIs: https://archive.ubuntu.com/ubuntu\n', modern[0])
        self.assertNotIn('vendor.example', modern[0])

    def test_missing_or_disabled_repositories_fail_clearly(self):
        for content in ['', self.stanza + 'Enabled: no\n']:
            with self.subTest(content=content):
                self.source.write_text(content)
                with self.assertRaisesRegex(ValueError, 'No enabled'):
                    enable_deb_src.collect(self.apt)

    def write_runner_mirror_list(self):
        path = self.apt / 'apt-mirrors.txt'
        path.write_text('# GitHub runner mirror priorities\n\n'
                        'http://azure.archive.ubuntu.com/ubuntu/\tpriority:1\n'
                        'https://archive.ubuntu.com/ubuntu/\tpriority:2\n'
                        'https://security.ubuntu.com/ubuntu/\tpriority:3\n')
        return path

    def test_github_ubuntu22_legacy_mirror_transport(self):
        self.write_runner_mirror_list()
        entries = [f'deb mirror+file:/etc/apt/apt-mirrors.txt {suite} main restricted universe multiverse'
                   for suite in ('jammy', 'jammy-updates', 'jammy-backports', 'jammy-security')]
        (self.apt / 'sources.list').write_text('\n'.join(entries) + '\n')
        legacy, modern = enable_deb_src.collect(self.apt)
        self.assertEqual(legacy, ['deb-src' + line[3:] for line in entries])
        self.assertEqual(modern, [])

    def test_github_deb822_mirror_keeps_signature_and_uri(self):
        self.write_runner_mirror_list()
        stanza = self.stanza.replace('http://azure.archive.ubuntu.com/ubuntu',
                                    'mirror+file:/etc/apt/apt-mirrors.txt')
        self.source.write_text(stanza)
        legacy, modern = enable_deb_src.collect(self.apt)
        self.assertEqual(legacy, [])
        self.assertEqual(modern, [stanza.replace('Types: deb\n', 'Types: deb-src\n').strip()])

    def test_missing_empty_and_nonofficial_mirror_lists_are_rejected(self):
        uri = 'mirror+file:/etc/apt/apt-mirrors.txt'
        self.assertFalse(enable_deb_src.official_uri(uri, self.apt))
        path = self.apt / 'apt-mirrors.txt'
        for content in ['', '# no mirrors\n', 'https://ubuntu.com.example.org/ubuntu\n',
                        'https://archive.ubuntu.com/ubuntu\nhttps://vendor.example/repo\n',
                        'mirror+file:/etc/apt/apt-mirrors.txt\n']:
            with self.subTest(content=content):
                path.write_text(content)
                self.assertFalse(enable_deb_src.official_uri(uri, self.apt))

    def test_mirror_path_must_stay_inside_apt_directory(self):
        outside = self.root / 'outside.txt'
        outside.write_text('https://archive.ubuntu.com/ubuntu\n')
        self.assertFalse(enable_deb_src.official_uri('mirror+file:' + str(outside), self.apt))
        self.assertFalse(enable_deb_src.official_uri('mirror+file:/etc/apt/../outside.txt', self.apt))
        self.assertFalse(enable_deb_src.official_uri('mirror+file://remote/etc/apt/apt-mirrors.txt', self.apt))

    def test_mirror_with_architecture_options_keeps_original_options(self):
        self.write_runner_mirror_list()
        line = ('deb [arch=amd64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] '
                'mirror+file:/etc/apt/apt-mirrors.txt jammy main')
        (self.apt / 'sources.list').write_text(line)
        self.assertEqual(enable_deb_src.collect(self.apt), (['deb-src' + line[3:]], []))

    def test_existing_mirror_source_entries_are_not_duplicated(self):
        self.write_runner_mirror_list()
        line = 'deb mirror+file:/etc/apt/apt-mirrors.txt jammy main'
        (self.apt / 'sources.list').write_text(line + '\n' + 'deb-src' + line[3:])
        self.assertEqual(enable_deb_src.collect(self.apt), ([], []))

    @unittest.skipUnless(shutil.which('apt-get'), 'APT parser is unavailable on this host')
    def test_generated_mirror_source_is_accepted_by_apt(self):
        mirrors = self.write_runner_mirror_list()
        # APT source lines split on whitespace; encode a temporary path as a URI.
        uri = 'mirror+' + mirrors.resolve().as_uri()
        (self.apt / 'sources.list').write_text('deb ' + uri + ' jammy main\n')
        legacy, modern = enable_deb_src.collect(self.apt)
        stage = self.root / 'generated'
        stage.mkdir()
        (stage / 'aster-deb-src.list').write_text('\n'.join(legacy) + '\n')
        lists = self.root / 'lists'
        (lists / 'partial').mkdir(parents=True)
        result = subprocess.run(['apt-get', '-o', 'Dir::Etc::sourcelist=/dev/null',
            '-o', f'Dir::Etc::sourceparts={stage}', '-o', f'Dir::State::lists={lists}',
            '-o', 'Debug::NoLocking=true', '--print-uris', 'update'],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Sources', result.stdout)
        self.assertIn('mirror+file:', result.stdout)

    def test_cli_writes_staging_files_without_changing_input(self):
        self.source.write_text(self.stanza)
        output = self.root / 'generated'
        result = subprocess.run([sys.executable, str(Path(enable_deb_src.__file__).resolve()),
            '--apt-root', str(self.apt), '--output-dir', str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.source.read_text(), self.stanza)
        self.assertEqual(sorted(p.name for p in self.parts.iterdir()), ['ubuntu.sources'])
        self.assertEqual(sorted(p.name for p in output.iterdir()), ['aster-deb-src.list', 'aster-deb-src.sources'])
        self.assertIn('Types: deb-src', (output / 'aster-deb-src.sources').read_text())

    @unittest.skipUnless(shutil.which('apt-get'), 'APT parser is unavailable on this host')
    def test_generated_deb822_is_accepted_by_apt_without_downloading(self):
        self.source.write_text(self.stanza)
        legacy, modern = enable_deb_src.collect(self.apt)
        stage = self.root / 'generated'
        stage.mkdir()
        (stage / 'aster-deb-src.sources').write_text('\n\n'.join(modern) + '\n')
        lists = self.root / 'lists'
        lists.mkdir()
        (lists / 'partial').mkdir()
        result = subprocess.run(['apt-get', '-o', 'Dir::Etc::sourcelist=/dev/null',
            '-o', f'Dir::Etc::sourceparts={stage}', '-o', f'Dir::State::lists={lists}',
            '-o', 'Debug::NoLocking=true', '--print-uris', 'update'],
            capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('jammy-updates', result.stdout)
        self.assertIn('Sources', result.stdout)


if __name__ == '__main__':
    unittest.main()
