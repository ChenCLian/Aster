import tempfile
from pathlib import Path, PurePosixPath
import unittest
import document_links as d

class DocumentLinksTests(unittest.TestCase):
    def test_same_body_four_contexts(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'docs').mkdir();(root/'FAQ.md').write_text('FAQ')
            text='[FAQ](https://github.com/ChenCLian/Aster/blob/main/FAQ.md#answer)\n'
            portable=d.portable(text,PurePosixPath('docs/guide.md'),root)
            self.assertEqual(portable,'[FAQ](../FAQ.md#answer)\n')
            for host in ('github.com','gitee.com'):
                self.assertEqual(d.release_body(portable,PurePosixPath('docs/guide.md'),host,'v1.3.0'),f'[FAQ](https://{host}/ChenCLian/Aster/blob/v1.3.0/FAQ.md#answer)\n')

    def test_external_and_history_unchanged(self):
        for url in ('https://github.com/other/repo/blob/main/file.md','https://github.com/ChenCLian/Aster/releases','https://github.com/ChenCLian/Aster/blob/v1.0.0/FAQ.md'):
            self.assertIsNone(d.own_path(url))
        self.assertEqual(d.release_body('[external](https://example.org/paper)'), '[external](https://example.org/paper)')

    def test_code_blocks_and_reference_links(self):
        text='```md\n[Example](FAQ.md)\n```\n[ref]: FAQ.md#test\n'
        changed=d.release_body(text,host='gitee.com',ref='v1.3.0')
        self.assertIn('[Example](FAQ.md)',changed)
        self.assertIn('[ref]: https://gitee.com/ChenCLian/Aster/blob/v1.3.0/FAQ.md#test',changed)

    def test_escape_and_host(self):
        with self.assertRaises(ValueError):d.release_body('[x](../../etc/passwd)')
        with self.assertRaises(ValueError):d.release_body('[x](FAQ.md)',host='evil.invalid')

    def test_current_manual_links(self):
        self.assertEqual(d.check(),[])
        resources=d.manual_files()
        self.assertIn(PurePosixPath('docs/USER_GUIDE.md'),resources)
        self.assertIn(PurePosixPath('docs/building.md'),resources)
        self.assertFalse(any('dependency-materials' in p.parts for p in resources))

    def test_mirror_preserves_published_notes_and_historical_links(self):
        text='Manual announcement\n[Guide](https://github.com/ChenCLian/Aster/blob/v1.3.0/FAQ.md)\n[Old](https://github.com/ChenCLian/Aster/blob/v1.2.0/FAQ.md)\n[Materials](https://github.com/ChenCLian/Aster/releases/tag/v1.3.0)'
        program='https://github.com/ChenCLian/Aster/releases/download/v1.3.0/Aster-1.3.0-windows-x64.zip'
        text+='\n[Windows]('+program+')'
        result=d.mirror_release_body(text,'v1.3.0')
        self.assertTrue(result.startswith('Manual announcement'))
        self.assertIn('https://github.com/ChenCLian/Aster/blob/v1.3.0/FAQ.md',result)
        self.assertIn(program.replace('github.com','gitee.com'),result)
        self.assertIn('https://github.com/ChenCLian/Aster/blob/v1.2.0/FAQ.md',result)
        self.assertIn('https://github.com/ChenCLian/Aster/releases/tag/v1.3.0',result)
