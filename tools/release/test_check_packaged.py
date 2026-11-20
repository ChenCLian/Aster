import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
from contextlib import nullcontext
from unittest.mock import patch
import check_packaged as c

class PackagedChecksTests(unittest.TestCase):
    def package(self, base, extra=None):
        version='1.3.1-beta.1';platform='windows-x64'
        name=f'AstroBatch-{version}-{platform}'
        archive=base/(name+'.zip')
        with zipfile.ZipFile(archive,'w') as z:
            z.writestr(name+'/astrobatch-package.json',json.dumps(dict(schema=1,version=version,platform=platform)))
            z.writestr(name+'/AstroBatch.exe',b'fixture, not executable')
            for key,value in (extra or {}).items():z.writestr(key,value)
        return archive,version,platform

    def test_windows_smoke_probes_use_configured_or_system_fonts(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            archive, version, platform = self.package(base)
            system = base / 'Windows'
            custom = base / 'test fonts'
            for override, expected in [('', system / 'Fonts'), (str(custom), custom)]:
                def startup(command, folder, env, log):
                    self.assertEqual(env['QT_QPA_PLATFORM'], 'offscreen')
                    self.assertEqual(env['QT_QPA_FONTDIR'], str(expected))
                    self.assertEqual(env['PATH'].split(c.os.pathsep),
                                     [str(folder), str(system / 'System32'), str(system)])
                    if '--release-smoke-output' in command:
                        Path(command[-1]).touch()
                    return subprocess.CompletedProcess(command, 0, 'AstroBatch ' + version)

                with self.subTest(override=override), patch.dict(c.os.environ, {
                    'SYSTEMROOT': str(system), 'QT_QPA_FONTDIR': override
                }), patch.object(c, 'probe', side_effect=startup) as probes:
                    c.run(archive, version, platform, base / 'report')
                    self.assertEqual(probes.call_count, 4)

    def test_alias_staging_root_keeps_package_inside_and_probes_all_modes(self):
        with tempfile.TemporaryDirectory() as td:
            base=Path(td).resolve();actual=base/'actual';actual.mkdir()
            # Portable alias: normalizes to actual without requiring Windows
            # symlink privileges. Exercises the same root/target mismatch as
            # expanded Windows short names or linked temporary directories.
            alias=actual/'..'/'actual'
            archive,version,platform=self.package(base)
            calls=[]
            def startup(command,folder,env,log):
                self.assertEqual(folder,actual/f'AstroBatch-{version}-{platform}')
                self.assertTrue(Path(command[0]).is_file())
                calls.append(command)
                if '--release-smoke-output' in command:
                    Path(command[-1]).write_bytes(b'screenshot fixture')
                log.write_text('fixture startup')
                return subprocess.CompletedProcess(command,0,'AstroBatch '+version if '--version' in command else '')
            with patch.object(c.tempfile,'TemporaryDirectory',return_value=nullcontext(str(alias))),patch.object(c,'probe',side_effect=startup):
                c.run(archive,version,platform,base/'report')
            self.assertEqual(len(calls),4)
            self.assertEqual(json.loads((base/'report/result.json').read_text())['status'],'passed')

    def test_alias_root_still_rejects_escape_before_extraction_or_execution(self):
        with tempfile.TemporaryDirectory() as td:
            base=Path(td).resolve();actual=base/'actual';actual.mkdir()
            alias=actual/'..'/'actual'
            archive,version,platform=self.package(base,{'../escaped.txt':'must not extract'})
            with patch.object(c.tempfile,'TemporaryDirectory',return_value=nullcontext(str(alias))),patch.object(c,'probe') as startup:
                with self.assertRaisesRegex(ValueError,'Archive path escapes staging'):
                    c.run(archive,version,platform,base/'report')
                startup.assert_not_called()
            self.assertFalse((base/'escaped.txt').exists())
            self.assertEqual(list(actual.iterdir()),[])
            self.assertEqual(json.loads((base/'report/result.json').read_text())['status'],'failed')

    def test_failed_rerun_clears_stale_success_and_screenshots(self):
        with tempfile.TemporaryDirectory() as td:
            out=Path(td);(out/'result.json').write_text('{"status":"passed"}')
            for name in ('packaged-zh.png','packaged-en.png','version.log','notes.txt'):(out/name).write_text('old')
            with patch.object(c,'_run_checks',side_effect=ValueError('bad package')):
                with self.assertRaisesRegex(ValueError,'bad package'):c.run(Path('package.zip'),'1.3.0','windows-x64',out)
            result=json.loads((out/'result.json').read_text());self.assertEqual(result['status'],'failed')
            self.assertEqual(result['version'],'1.3.0');self.assertFalse((out/'packaged-zh.png').exists());self.assertFalse((out/'version.log').exists());self.assertTrue((out/'notes.txt').exists())
    def test_timeout_keeps_partial_diagnostic_output(self):
        with tempfile.TemporaryDirectory() as td:
            log=Path(td)/'log'
            with patch.object(c.subprocess,'run',side_effect=subprocess.TimeoutExpired(['app'],30,output=b'loading plugin\n')):
                with self.assertRaises(subprocess.TimeoutExpired):c.probe(['app'],Path(td),{},log)
            self.assertIn('loading plugin',log.read_text());self.assertIn('timed out',log.read_text())
    def test_new_result_does_not_inherit_previous_failure(self):
        with tempfile.TemporaryDirectory() as td:
            out=Path(td);(out/'result.json').write_text('{"status":"failed","error":"old failure"}')
            with patch.object(c,'_run_checks'):c.run(Path('package.zip'),'1.3.0','windows-x64',out)
            result=json.loads((out/'result.json').read_text());self.assertEqual(result['status'],'passed');self.assertNotIn('error',result)
