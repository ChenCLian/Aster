"""Regression coverage for material identity, failed downloads and publication.

Copyright (C) 2026 ZiJie Chen
SPDX-License-Identifier: GPL-3.0-only
"""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import struct
import shutil
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dependency_common as common
import dependency_providers as providers
import collect_dependency_materials as collector
import enable_deb_src
import package_source


def sha(data):
    return hashlib.sha256(data).hexdigest()


def tar(path, files):
    with tarfile.open(path, 'w:gz') as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))


class DependencyMaterialTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.collection = common.Collection(self.base / 'materials', self.base / 'cache', 'linux-x64', '1.1.1')

    def tearDown(self):
        self.temp.cleanup()

    def test_verified_cache_reused_without_network(self):
        data = b'known source bytes'
        cache = self.base / 'cache'
        cache.mkdir()
        cached = cache / ('sha256-' + sha(data))
        cached.write_bytes(data)
        with patch.object(common, 'open_url', side_effect=AssertionError('must not fetch')):
            self.assertEqual(common.download('https://example.org/source.tar', cache, 'sha256', sha(data)), cached)

    def test_corrupt_cache_is_replaced_only_with_verified_bytes(self):
        data = b'correct'
        cache = self.base / 'cache'
        cache.mkdir()
        cached = cache / ('sha256-' + sha(data))
        cached.write_bytes(b'corrupt')
        with patch.object(common, 'open_url', return_value=io.BytesIO(data)):
            common.download('https://example.org/source.tar', cache, 'sha256', sha(data))
        self.assertEqual(cached.read_bytes(), data)

    def test_checksum_mismatch_does_not_publish_partial_download(self):
        with patch.object(common, 'open_url', return_value=io.BytesIO(b'wrong')):
            with self.assertRaises(common.MaterialError):
                common.download('https://example.org/s.tar', self.base / 'cache', 'sha256', sha(b'right'))
        self.assertEqual(list((self.base / 'cache').iterdir()), [])

    def test_insecure_or_unresolved_source_urls_rejected(self):
        for url in ['http://example.org/s', 'file:///tmp/s', 'https://user:secret@example.org/s',
                    'https://example.org/${VERSION}', 'https://example.org/s#branch']:
            with self.assertRaises(common.MaterialError):
                common.check_url(url)

    def test_spdx_git_conversion_preserves_pinned_revision(self):
        self.assertEqual(providers.spdx_url({'downloadLocation':'git+https://github.com/owner/repo@v${VERSION}'}, '1.2.3'),
                         'https://github.com/owner/repo/archive/v1.2.3.tar.gz')
        with self.assertRaises(common.MaterialError):
            providers.spdx_url({'downloadLocation':'git+https://github.com/owner/repo'}, '1.2.3')

    def test_archive_traversal_and_symlink_are_not_extracted(self):
        bad = self.base / 'bad.tar.gz'
        tar(bad, {'../../LICENSE':b'bad'})
        with self.assertRaises(common.MaterialError):
            common.extract_notices(bad, self.base / 'out')
        self.assertFalse((self.base / 'LICENSE').exists())
        linked = self.base / 'linked.tar'
        with tarfile.open(linked, 'w') as archive:
            info = tarfile.TarInfo('LICENSE')
            info.type = tarfile.SYMTYPE
            info.linkname = '/etc/passwd'
            archive.addfile(info)
        self.assertEqual(common.extract_notices(linked, self.base / 'out'), 0)

    def test_qt_attribution_referenced_license_keeps_bytes(self):
        source = self.base / 'qt.tar.gz'
        tar(source, {'qt/src/vendor/qt_attribution.json':json.dumps({'LicenseFile':'terms.dat'}).encode(),
                     'qt/src/vendor/terms.dat':b'upstream terms\r\n', 'qt/src/LICENSE':b'Qt terms'})
        common.extract_notices(source, self.base / 'out')
        self.assertEqual((self.base / 'out/qt/src/vendor/terms.dat').read_bytes(), b'upstream terms\r\n')

    def test_missing_attribution_license_is_not_claimed_complete(self):
        source = self.base / 'qt.tar.gz'
        tar(source, {'qt/src/qt_attribution.json':b'{"LicenseFile":"missing.txt"}'})
        with self.assertRaises(common.MaterialError):
            common.extract_notices(source, self.base / 'out')

    def test_qt_official_multiline_attribution_text(self):
        source = self.base/'multiline.tar.gz'
        tar(source, {'qt/src/qt_attribution.json':b'{"Description":"first\nsecond", "LicenseFile":"terms"}',
                     'qt/src/terms':b'terms'})
        self.assertEqual(common.extract_notices(source, self.base/'out'), 2)

    def test_current_spdx_binary_files_are_not_treated_as_port_sources(self):
        root = self.base/'vcpkg'
        (root/'ports/example').mkdir(parents=True)
        (root/'ports/example/portfile.cmake').write_bytes(b'recipe')
        sbom = {'files':[
            {'SPDXID':'SPDXRef-port-file-0','fileName':'./portfile.cmake','checksums':[{'algorithm':'SHA256','checksumValue':sha(b'recipe')}]},
            {'SPDXID':'SPDXRef-binary-file-0','fileName':'./bin/example.dll','checksums':[{'algorithm':'SHA256','checksumValue':sha(b'binary')}]}],
            'relationships':[{'spdxElementId':'SPDXRef-port','relationshipType':'CONTAINS','relatedSpdxElement':'SPDXRef-port-file-0'}]}
        item = self.collection.component('example','1','vcpkg')
        providers.vcpkg_recipe(self.collection,item,sbom,root,'example')
        self.assertEqual(len(item['recipes']),1)

    def test_qt_sdk_other_libraries_are_not_claimed_as_qt(self):
        prefix = self.base/'Qt'
        (prefix/'bin').mkdir(parents=True)
        (prefix/'bin/opengl32sw.dll').write_bytes(b'not Qt source')
        runtime = self.base/'opengl32sw.dll'
        runtime.write_bytes(b'not Qt source')
        self.assertEqual(providers.qt_owners([runtime],prefix,'windows-x64'),{})

    def test_windows_qt_path_patching_keeps_binary_identity(self):
        data=bytearray(1024)
        data[:2]=b'MZ'
        struct.pack_into('<I',data,60,128)
        data[128:132]=b'PE\0\0'
        struct.pack_into('<H',data,134,2)
        struct.pack_into('<H',data,148,240)
        struct.pack_into('<H',data,152,0x20b)
        struct.pack_into('<II',data,152+112+48,0x2000,28)
        # Code and debug sections.
        data[392:400]=b'.text\0\0\0'
        struct.pack_into('<IIII',data,400,32,0x1000,32,512)
        data[432:440]=b'.rdata\0\0'
        struct.pack_into('<IIII',data,440,256,0x2000,256,640)
        data[512:544]=bytes(range(32))
        struct.pack_into('<IIII',data,652,2,32,0x2040,704)
        data[704:728]=b'RSDS'+bytes(range(20))
        original=self.base/'original.dll';original.write_bytes(data)
        data[800:810]=b'newprefix!'
        patched=self.base/'patched.dll';patched.write_bytes(data)
        self.assertNotEqual(common.digest(original),common.digest(patched))
        self.assertTrue(providers.same_binary(original,patched,'windows-x64'))
        data[512]=99;patched.write_bytes(data)
        self.assertFalse(providers.same_binary(original,patched,'windows-x64'))

    def test_vcpkg_end_to_end_uses_installed_sources_and_notice(self):
        root=self.base/'vcpkg';prefix=root/'installed/x64-windows'
        db=root/'installed/vcpkg';(db/'info').mkdir(parents=True)
        (prefix/'bin').mkdir(parents=True)
        (root/'triplets').mkdir();(root/'triplets/x64-windows.cmake').write_text('set(VCPKG_TARGET_ARCHITECTURE x64)')
        records=[]
        source=self.base/'source.tar.gz';tar(source,{'lib/LICENSE':b'license','lib/source.c':b'source'})
        for name in ['gsl','openblas']:
            records.append(f'Package: {name}\nVersion: 1.0\nArchitecture: x64-windows\nStatus: install ok installed\n')
            (root/'ports'/name).mkdir(parents=True)
            (root/'ports'/name/'portfile.cmake').write_bytes(b'recipe')
            share=prefix/'share'/name;share.mkdir(parents=True)
            (share/'copyright').write_bytes(b'original license')
            sbom={'packages':[{'SPDXID':'SPDXRef-port','versionInfo':'1.0'},
                {'SPDXID':'SPDXRef-resource-0','downloadLocation':'https://example.org/source.tar.gz',
                 'checksums':[{'algorithm':'SHA256','checksumValue':common.digest(source)}]}],
                'files':[{'fileName':'portfile.cmake','checksums':[{'algorithm':'SHA256','checksumValue':sha(b'recipe')}]}]}
            (share/'vcpkg.spdx.json').write_text(json.dumps(sbom))
            (prefix/'bin'/f'{name}.dll').write_bytes(b'MZ'+name.encode())
            (db/'info'/f'{name}_1.0_x64-windows.list').write_text(f'x64-windows/bin/{name}.dll\n')
        (db/'status').write_text('\n'.join(records))
        package=self.base/'package';package.mkdir()
        binary=package/'gsl.dll';binary.write_bytes((prefix/'bin/gsl.dll').read_bytes())
        owners={}
        with patch.object(common,'download',return_value=source):
            providers.vcpkg_sources(self.collection,prefix,root,[binary],owners)
        self.assertEqual(owners[binary],'vcpkg-gsl')
        self.assertEqual(self.collection.errors,[])
        self.assertEqual({c['id'] for c in self.collection.components},{'vcpkg-gsl','vcpkg-openblas'})
        self.assertTrue(all(c['recipes'] and c['notices'] and c['sources'] for c in self.collection.components))

    def test_debian_collection_includes_distribution_patch_archive(self):
        library=self.base/'libfixture.so.1';library.write_bytes(b'\x7fELFtest')
        self.collection.platform='linux-x64'
        native=self.base/'native';native.mkdir()
        original=native/'fixture_1.0.orig.tar.gz';original.write_bytes(b'upstream')
        patches=native/'fixture_1.0-2.debian.tar.xz';patches.write_bytes(b'patches')
        dsc=native/'fixture_1.0-2.dsc'
        dsc.write_text('Source: fixture\nVersion: 1.0-2\nChecksums-Sha256:\n'+
                       f' {common.digest(original)} 8 {original.name}\n'+
                       f' {common.digest(patches)} 7 {patches.name}\n')
        def native_run(args,cwd=None):
            if args[:2]==['ldconfig','-p']:return f'libfixture.so.1 (libc6,x86-64) => {library}'
            if args[0]=='readelf':return 'Build ID: 112233'
            if args[:2]==['dpkg-query','-S']:return f'libfixture1:amd64: {library}'
            if args[:2]==['dpkg-query','-W']:return 'libfixture1\t1.0-2\tfixture\t1.0-2\tamd64'
            if '--print-uris' in args:
                return '\n'.join(f"'https://example.org/{p.name}' {p.name} {p.stat().st_size} SHA256:{common.digest(p)}" for p in [dsc,original,patches])
            if args[0]=='apt-get':
                for p in [dsc,original,patches]:shutil.copy2(p,Path(cwd)/p.name)
                return ''
            raise AssertionError(args)
        old_read=Path.read_bytes
        def file_read(path):
            if str(path)=='/usr/share/doc/libfixture1/copyright':return b'copyright terms'
            return old_read(path)
        with patch.object(providers,'run',side_effect=native_run),patch.object(Path,'read_bytes',file_read):
            owners={};providers.debian_sources(self.collection,[library],owners)
        self.assertEqual(self.collection.errors,[])
        self.assertEqual(owners[library],'deb-libfixture1')
        self.assertEqual({s['asset'] for s in self.collection.components[0]['sources']},
                         {dsc.name,original.name,patches.name})

    def test_homebrew_uses_keg_formula_and_exact_revision(self):
        self.collection.platform='macos-arm64'
        cellar=self.base/'Cellar';keg=cellar/'fixture/1.0_2'
        (keg/'lib').mkdir(parents=True);(keg/'.brew').mkdir()
        library=keg/'lib/libfixture.dylib';library.write_bytes(b'MachO')
        (keg/'INSTALL_RECEIPT.json').write_text(json.dumps({'runtime_dependencies':[], 'poured_from_bottle':True}))
        recipe=keg/'.brew/fixture.rb';recipe.write_bytes(b'installed recipe, not current online recipe')
        source=self.base/'fixture.tar.gz';tar(source,{'fixture/LICENSE':b'notice','fixture/code.c':b'source'})
        metadata={'version':'1.0_2','sources':[{'url':'https://example.org/fixture.tar.gz','sha256':common.digest(source),'kind':'source'}]}
        def native_run(args,cwd=None):
            if args==['brew','--cellar']:return str(cellar)
            if args[:2]==['brew','ruby']:
                self.assertEqual(args[-1],recipe)
                return json.dumps(metadata)
            raise AssertionError(args)
        with patch.object(providers,'run',side_effect=native_run),patch.object(providers,'same_binary',return_value=True),patch.object(common,'download',return_value=source):
            owners={};providers.brew_sources(self.collection,[library],owners)
        self.assertEqual(self.collection.errors,[])
        self.assertEqual(self.collection.components[0]['version'],'1.0_2')
        self.assertEqual(owners[library],'brew-fixture')

    def test_unknown_qt_library_requires_mapping(self):
        self.assertEqual(providers.qt_module('Pdf'), 'qtwebengine')
        self.assertEqual(providers.qt_module('XcbQpa'), 'qtbase')
        with self.assertRaises(common.MaterialError):
            providers.qt_module('ImaginaryModule')

    def test_recipe_hash_rejects_same_version_different_patch(self):
        root = self.base / 'vcpkg'
        (root / 'ports/gsl').mkdir(parents=True)
        (root / 'ports/gsl/portfile.cmake').write_bytes(b'changed patch set')
        item = self.collection.component('gsl','2.8','vcpkg')
        sbom = {'files':[{'fileName':'ports/gsl/portfile.cmake','checksums':[
            {'algorithm':'SHA256','checksumValue':sha(b'original patch set')}]}]}
        with self.assertRaises(common.MaterialError):
            providers.vcpkg_recipe(self.collection,item,sbom,root,'gsl')

    def test_recipe_copy_preserves_exact_patches(self):
        root = self.base / 'vcpkg'
        (root / 'ports/gsl').mkdir(parents=True)
        payload = {'portfile.cmake':b'apply(fix.patch)\n','fix.patch':b'original patch\r\n'}
        for name,data in payload.items():
            (root / 'ports/gsl' / name).write_bytes(data)
        sbom = {'files':[{'fileName':name,'checksums':[{'algorithm':'SHA256','checksumValue':sha(data)}]}
                         for name,data in payload.items()]}
        item = self.collection.component('gsl','2.8','vcpkg')
        providers.vcpkg_recipe(self.collection,item,sbom,root,'gsl')
        self.assertEqual((self.collection.records / 'recipes/gsl/fix.patch').read_bytes(),payload['fix.patch'])

    def test_debian_control_preserves_source_revision(self):
        text = 'Source: gsl\nVersion: 2.7.1+dfsg-3\nChecksums-Sha256:\n aaaa 12 gsl.orig.tar.gz\n bbbb 15 gsl.debian.tar.xz\n'
        record = providers.parse_control(text)[0]
        self.assertEqual(record['Version'], '2.7.1+dfsg-3')
        self.assertIn('gsl.debian.tar.xz', record['Checksums-Sha256'])

    def test_failed_inventory_does_not_install_notices(self):
        package = self.base / 'package'
        package.mkdir()
        binary = package / 'unknown.so'
        binary.write_bytes(b'\x7fELFrest')
        self.assertEqual(collector.finish(self.collection, package, {}, [binary]), 2)
        self.assertFalse((package / 'SOURCE_DEPENDENCIES.md').exists())
        self.assertFalse(list(self.collection.assets.glob('*dependency-records.zip')))

    def test_success_records_sources_separately_and_installs_notices(self):
        package = self.base / 'package'
        package.mkdir()
        source = self.base / 'source.tar.gz'
        tar(source, {'library/LICENSE':b'original license','library/source.c':b'int f(void){return 1;}'})
        item = self.collection.component('fixture','1.0','fixture-provider')
        with patch.object(common, 'download', return_value=source):
            self.collection.source(item,'https://example.org/source.tar.gz','sha256',common.digest(source),'fixture.tar.gz')
        self.assertEqual(collector.finish(self.collection, package, {}, []), 0)
        self.assertTrue((package / 'licenses/dependencies/fixture/library/LICENSE').is_file())
        self.assertFalse((package / 'fixture.tar.gz').exists())
        with zipfile.ZipFile(next(self.collection.assets.glob('*dependency-records.zip'))) as archive:
            self.assertIn('DEPENDENCIES.json', archive.namelist())
            self.assertFalse(any(x.endswith('.tar.gz') for x in archive.namelist()))
        report = json.loads((self.collection.output / 'report.json').read_text())
        self.assertEqual(report['status'],'collected')
        self.assertTrue(report['deferred'])
        self.assertNotIn(str(self.base), json.dumps(report))

    def test_same_asset_name_different_contents_rejected(self):
        source = self.base / 'source.tar.gz'
        tar(source, {'LICENSE':b'terms'})
        (self.collection.assets / 'same.tar.gz').write_bytes(b'other content')
        item = self.collection.component('fixture','1','test')
        with patch.object(common,'download',return_value=source):
            with self.assertRaises(common.MaterialError):
                self.collection.source(item,'https://example.org/s.tar.gz','sha256',common.digest(source),'same.tar.gz')

    def test_source_packager_excludes_downloads_but_keeps_scripts(self):
        with patch.object(package_source,'ROOT',self.base):
            self.assertFalse(package_source.included(self.base/'dependency-materials/source.tar.xz'))
            self.assertFalse(package_source.included(self.base/'dist/release-assets/source.tar.gz'))
            self.assertTrue(package_source.included(self.base/'tools/release/collect_dependency_materials.py'))

    def test_deb_src_conversion_preserves_restrictions(self):
        self.assertEqual(enable_deb_src.convert('deb [signed-by=/usr/share/keyrings/a.gpg] https://example.org jammy main\n# deb https://disabled.org stable main\n'),
                         ['deb-src [signed-by=/usr/share/keyrings/a.gpg] https://example.org jammy main'])

    def test_supplement_cannot_silently_change_installed_version(self):
        self.collection.component('fixture','1.0','vcpkg')
        manifest = self.base/'supplement.json'
        manifest.write_text(json.dumps({'components':[{'id':'fixture','version':'2.0','replace':True}]}))
        with self.assertRaises(common.MaterialError):
            collector.supplement(self.collection,manifest,self.base,{})


if __name__ == '__main__':
    unittest.main()
