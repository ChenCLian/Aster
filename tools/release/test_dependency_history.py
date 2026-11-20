"""Published reuse must be exact, revocable, and independent of app versions."""
import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

import dependency_history as history
import dependency_common as common
import collect_dependency_materials as collector


class HistoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.platform = 'windows-x64'
        self.version = '1.3.0-beta.20'
        self.source = {'asset':'gsl.tar.gz','checksum_algorithm':'sha256',
                       'expected_checksum':'a'*64,'sha256':'a'*64,'bytes':123,'status':'verified'}
        self.component = {'id':'vcpkg-gsl','version':'2.8','provider':'vcpkg',
                          'sources':[self.source], 'notices':['licenses/vcpkg-gsl'], 'recipes':[]}
        self.report = {'schema':1,'project':'AstroBatch','version':self.version,'platform':self.platform,
                       'status':'collected','errors':[], 'components':[self.component], 'binaries':[]}
        self.directory = self.root/'history'
        self.directory.mkdir()

    def records(self, extra=None):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream,'w') as z:
            z.writestr('DEPENDENCIES.json',json.dumps(self.report))
            z.writestr('licenses/vcpkg-gsl/LICENSE','Exact license')
            for name, value in (extra or {}).items(): z.writestr(name,value)
        return stream.getvalue()

    def install(self, available=True, extra=None):
        data=self.records(extra)
        (self.directory/'records.zip').write_bytes(data)
        entry={'file':'records.zip','sha256':history.sha(data),'available':
               {history.asset_url(self.version,'gsl.tar.gz'):'a'*64} if available else {}}
        (self.directory/'index.json').write_text(json.dumps({'schema':1,'platform':self.platform,'records':[entry]}))
        return history.PublishedHistory(self.directory,self.platform)

    def collection(self):
        return common.Collection(self.root/'out',self.root/'cache',self.platform,'1.3.1-beta.1')

    def test_reuse_keeps_notices_and_skips_source_network_and_upload(self):
        c=self.collection(); c.history=self.install()
        component=c.component('vcpkg-gsl','2.8','vcpkg')
        with patch.object(common,'download',side_effect=AssertionError('source was downloaded')):
            c.source(component,'https://example.org/gsl.tar.gz','sha256','a'*64)
        self.assertEqual(component['sources'][0]['status'],'reused-published')
        self.assertFalse((c.assets/'gsl.tar.gz').exists())
        self.assertEqual((c.licenses/'vcpkg-gsl/LICENSE').read_text(),'Exact license')
        package=self.root/'package'; package.mkdir()
        self.assertEqual(collector.finish(c,package,{},[]),0)
        index=(package/'SOURCE_DEPENDENCIES.md').read_text()
        self.assertIn('/v1.3.0-beta.20/gsl.tar.gz',index)
        lock=json.loads((c.assets/'AstroBatch-1.3.1-beta.1-windows-x64-dependency-lock.json').read_text())
        self.assertTrue(lock['components'][0]['processed'])
        self.assertEqual(lock['version'],'1.3.1-beta.1')

    def test_incomplete_collection_does_not_mark_dependencies_processed(self):
        c=self.collection(); c.component('new','1','vcpkg')
        c.error('new','Missing source')
        package=self.root/'package';package.mkdir()
        self.assertEqual(collector.finish(c,package,{},[]),2)
        lock=json.loads((c.output/'dependency-lock.json').read_text())
        self.assertFalse(lock['components'][0]['processed'])
        self.assertFalse((package/'SOURCE_DEPENDENCIES.md').exists())

    def test_network_failure_clears_previous_availability(self):
        self.install()
        with patch.object(history,'github',side_effect=ValueError('network')):
            with self.assertRaises(ValueError): history.fetch(self.directory,self.platform)
        self.assertEqual(history.PublishedHistory(self.directory,self.platform).entries,[])

    def test_configured_history_is_inactive_until_valid_receipts_exist(self):
        with patch.dict('os.environ',{'ASTER_DEPENDENCY_HISTORY':str(self.directory)}):
            self.assertFalse(self.collection().history)
            index=self.directory/'index.json'
            index.write_text(json.dumps({'schema':1,'platform':self.platform,'records':[]}))
            self.assertFalse(self.collection().history)
            index.write_text('invalid JSON')
            self.assertFalse(self.collection().history)
            self.install()
            self.assertTrue(self.collection().history)
            (self.directory/'records.zip').write_bytes(b'corrupt')
            self.assertFalse(self.collection().history)

    def test_new_version_checksum_provider_or_removed_asset_cannot_reuse(self):
        c=self.collection(); record=dict(self.source)
        for key,value in [('version','2.9'),('provider','brew'),('id','other')]:
            component=dict(self.component); component[key]=value
            self.assertFalse(self.install().source(c,component,record,True))
        record['expected_checksum']='b'*64
        self.assertFalse(self.install().source(c,dict(self.component),record,True))
        self.assertFalse(self.install(available=False).source(c,dict(self.component),dict(self.source),True))

    def test_corrupt_receipt_cannot_reuse_and_plan_only_ignores_history(self):
        self.install(); (self.directory/'records.zip').write_bytes(b'corrupt')
        self.assertEqual(history.PublishedHistory(self.directory,self.platform).entries,[])
        with patch.dict('os.environ',{'ASTER_DEPENDENCY_HISTORY':str(self.directory)}):
            c=common.Collection(self.root/'out',self.root/'cache',self.platform,'1.3.1-alpha.1',True)
        self.assertIsNone(c.history)

    def test_wrong_platform_incomplete_and_alpha_are_rejected(self):
        for field,value in [('platform','macos-arm64'),('status','incomplete'),('version','1.3.1-alpha.1')]:
            old=self.report[field];self.report[field]=value
            with self.assertRaises(ValueError):history.read_records(self.records(),self.platform)
            self.report[field]=old

    def test_license_paths_cannot_escape_records(self):
        c=self.collection(); component=c.component('vcpkg-gsl','2.8','vcpkg')
        self.assertFalse(self.install(extra={'licenses/vcpkg-gsl/../../../escape':'bad'}).source(c,component,dict(self.source),True))
        self.assertFalse((self.root/'escape').exists())

    def test_sources_must_stay_on_official_release(self):
        for url in ['https://evil.example/x','http://github.com/ChenCLian/AstroBatch/releases/download/v1.0.0/gsl.tar.gz',
                    'https://github.com/Other/AstroBatch/releases/download/v1.0.0/gsl.tar.gz',
                    'https://github.com/ChenCLian/AstroBatch/releases/download/v1.3.1-alpha.1/gsl.tar.gz']:
            with self.assertRaises(ValueError): history.source_location(dict(self.source,published_url=url),self.version)

    def test_debian_requires_every_current_apt_checksum(self):
        self.component.update(provider='dpkg',source_package='gsl',source_version='2.8',architecture='amd64')
        c=self.collection(); h=self.install()
        current=dict(self.component,sources=[])
        self.assertFalse(h.debian(c,current,{'gsl.tar.gz':'b'*64}))
        self.assertTrue(h.debian(c,current,{'gsl.tar.gz':'a'*64}))
        self.assertEqual(current['sources'][0]['status'],'reused-published')

    def test_fetch_checks_asset_digests_and_ignores_drafts(self):
        data=self.records(); name=f'AstroBatch-{self.version}-{self.platform}-dependency-records.zip'
        release={'id':1,'tag_name':'v'+self.version,'draft':False}
        assets=[{'id':2,'name':name,'size':len(data),'digest':'sha256:'+history.sha(data),'state':'uploaded'},
                {'id':3,'name':'gsl.tar.gz','size':123,'digest':'sha256:'+'a'*64,'state':'uploaded'}]
        def api(endpoint,binary=False):
            if binary:return data
            if '/assets?' in endpoint:return assets
            if '/tags/' in endpoint:return release
            return [dict(release,id=8,draft=True),release]
        with patch.object(history,'github',side_effect=api):history.fetch(self.directory,self.platform)
        self.assertEqual(len(history.PublishedHistory(self.directory,self.platform).entries),1)
        assets[1]['digest']='sha256:'+'b'*64
        with patch.object(history,'github',side_effect=api):history.fetch(self.directory,self.platform)
        c=self.collection()
        self.assertFalse(history.PublishedHistory(self.directory,self.platform).source(c,dict(self.component),dict(self.source),True))

    def test_split_receipts_and_sources_can_be_reused_across_layouts(self):
        version = '1.3.1-beta.2'
        self.report['version'] = version
        self.source['published_url'] = history.asset_url(self.version, self.source['asset'])
        old = 'v'+self.version
        main = 'v'+version
        materials = 'materials-'+main
        data=self.records()
        def api(endpoint,binary=False):
            if binary:return data
            if '/tags/' in endpoint:
                tag=endpoint.rsplit('/',1)[1]
                self.assertIn(tag,(materials,old))
                return dict(id=2 if tag==materials else 1,tag_name=tag,draft=False)
            if '/releases/2/assets?' in endpoint:
                return [dict(id=3,name=f'AstroBatch-{version}-{self.platform}-dependency-records.zip',
                             size=len(data),digest='sha256:'+history.sha(data),state='uploaded')]
            if '/releases/1/assets?' in endpoint:
                return [dict(id=4,name=self.source['asset'],size=123,digest='sha256:'+'a'*64,state='uploaded')]
            return [dict(tag_name=main,draft=False),dict(tag_name=materials,draft=False)]
        with patch.object(history,'github',side_effect=api):
            history.fetch(self.directory,self.platform)
        self.assertTrue(history.PublishedHistory(self.directory,self.platform).source(
            self.collection(),dict(self.component),dict(self.source),False))
        self.assertIn('/'+materials+'/',history.asset_url(version,'new~source.tar.gz'))
    def test_normalized_dependency_name_reused_with_original_identity(self):
        original='gcc-12_12.3.0-1ubuntu1~22.04.3.debian.tar.xz'
        published='gcc-12_12.3.0-1ubuntu1_22.04.3.debian.tar.xz'
        self.source['asset']=original
        data=self.records();name=f'AstroBatch-{self.version}-{self.platform}-dependency-records.zip'
        release={'id':1,'tag_name':'v'+self.version,'draft':False}
        assets=[dict(id=2,name=name,size=len(data),digest='sha256:'+history.sha(data),state='uploaded'),
                dict(id=3,name=published,size=123,digest='sha256:'+'a'*64,state='uploaded')]
        def api(endpoint,binary=False):
            if binary:return data
            if '/assets?' in endpoint:return assets
            if '/tags/' in endpoint:return release
            return [release]
        with patch.object(history,'github',side_effect=api):history.fetch(self.directory,self.platform)
        c=self.collection();record=dict(self.source)
        self.assertTrue(history.PublishedHistory(self.directory,self.platform).source(c,dict(self.component),record,True))
        self.assertEqual(record['asset'],original)
        self.assertTrue(record['published_url'].endswith('/'+published))
        self.assertEqual(history.source_location(record,'1.3.1-beta.1')[0],self.version)
        index=collector.dependency_index(self.version,self.platform,[original])
        self.assertIn('/'+published+')',index)
        # Subsequent releases can retain this exact published URL.
        self.source.update(record)
        self.assertEqual(history.source_location(self.source,'1.3.1-beta.1')[1],record['published_url'])

    def test_explicit_legacy_original_dependency_url_remains_valid(self):
        record=dict(self.source,asset='foo~1.tar.gz',published_url=f'https://github.com/{history.REPO}/releases/download/v{self.version}/foo~1.tar.gz')
        self.assertEqual(history.source_location(record,self.version)[1],record['published_url'])
        record['published_url']=record['published_url'].replace('foo~1','unrelated')
        with self.assertRaises(ValueError):history.source_location(record,self.version)

if __name__=='__main__':unittest.main()
