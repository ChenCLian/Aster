// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <archive.h>
#include <archive_entry.h>
#include <cstring>
#include "update/package_download.h"
using namespace aster::updates;
namespace {
UpdateConfig config(){UpdateConfig c{"ChenCLian","Aster","2026-03-10"};c.packageMetadata="Aster-update.json";c.timeoutMs=80;return c;}
QString version(){return "99.0.0-beta.1";}
QString platform(){return nativeUpdatePlatform();}
QString root(){return "Aster-"+version()+"-"+platform();}
QString launch(){return platform()=="windows-x64"?"Aster.exe":platform().startsWith("macos")?"Aster.app/Contents/MacOS/Aster":"AppRun";}
QByteArray json(const QJsonObject &o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
QJsonObject metadata(const QByteArray &body,bool mirror=false){
 const auto name=root()+(platform()=="linux-x64"?".tar.gz":".zip");
 QJsonArray sources{QJsonObject{{"name","GitHub"},{"url","https://github.com/ChenCLian/Aster/releases/download/v"+version()+"/"+name}}};
 if(mirror)sources.append(QJsonObject{{"name","Gitee"},{"url","https://gitee.com/ChenCLian/Aster/releases/download/v"+version()+"/"+name}});
 return {{"schema",1},{"version",version()},{"packages",QJsonArray{QJsonObject{{"platform",platform()},{"file",name},{"bytes",body.size()},{"unpacked_bytes",67108864},{"sha256",QString(QCryptographicHash::hash(body,QCryptographicHash::Sha256).toHex())},{"sources",sources}}}}};
}
struct Entry{QString name;QByteArray body;int type=AE_IFREG;QString link;int mode=0755;};
QByteArray pack(QVector<Entry> entries,bool zip=false){
 QTemporaryFile file; if(!file.open())return {};
 auto *a=archive_write_new();if(zip)archive_write_set_format_zip(a);else{archive_write_set_format_pax_restricted(a);archive_write_add_filter_gzip(a);}
 if(archive_write_open_fd(a,file.handle())!=ARCHIVE_OK){archive_write_free(a);return {};}
 for(const auto &e:entries){auto *item=archive_entry_new();const auto name=e.name.toUtf8();archive_entry_set_pathname(item,name.constData());archive_entry_set_filetype(item,e.type);archive_entry_set_perm(item,e.mode);archive_entry_set_size(item,e.type==AE_IFREG?e.body.size():0);if(!e.link.isEmpty())archive_entry_set_symlink(item,e.link.toUtf8().constData());
  if(archive_write_header(a,item)==ARCHIVE_OK&&e.type==AE_IFREG)archive_write_data(a,e.body.constData(),size_t(e.body.size()));archive_entry_free(item);
 }archive_write_close(a);archive_write_free(a);file.seek(0);return file.readAll();
}
QVector<Entry> entries(){return {{root()+"/"+launch(),"executable"},{root()+"/aster-package.json",json({{"schema",1},{"version",version()},{"platform",platform()}})}};}
struct Response{int status=200;QByteArray body;int delay=1;QString redirect;};
class Reply:public QNetworkReply{
 QByteArray body;qint64 at=0;
public:
 Reply(const QNetworkRequest &req,Response r,QObject *parent):QNetworkReply(parent),body(r.body){setRequest(req);setUrl(req.url());setOperation(QNetworkAccessManager::GetOperation);setAttribute(QNetworkRequest::HttpStatusCodeAttribute,r.status);if(!r.redirect.isEmpty())setAttribute(QNetworkRequest::RedirectionTargetAttribute,QUrl(r.redirect));open(QIODevice::ReadOnly);QTimer::singleShot(r.delay,this,[this]{if(isFinished())return;emit readyRead();if(!isFinished()){setFinished(true);emit finished();}});}
 void abort()override{if(isFinished())return;setError(OperationCanceledError,"cancelled");setFinished(true);emit finished();}
 qint64 bytesAvailable()const override{return body.size()-at+QNetworkReply::bytesAvailable();}
 bool isSequential()const override{return true;}
 qint64 readData(char *p,qint64 count)override{auto n=qMin<qint64>(count,body.size()-at);if(n<=0)return -1;std::memcpy(p,body.constData()+at,size_t(n));at+=n;return n;}
};
class Manager:public QNetworkAccessManager{
public:QVector<Response> replies;QVector<QUrl> urls;
 QNetworkReply *createRequest(Operation,const QNetworkRequest &r,QIODevice*)override{urls.append(r.url());return new Reply(r,replies.isEmpty()?Response{500,""}:replies.takeFirst(),this);}
};
}
class DownloadTests:public QObject{
 Q_OBJECT
private slots:

 void cancelAtNotification_data(){QTest::addColumn<QString>("at");for(const auto *at:{"metadata","source","downloading","zero-progress","verifying","extracting"})QTest::newRow(at)<<QString(at);}
 void cancelAtNotification(){
  QFETCH(QString,at);Manager manager;auto bytes=pack(entries(),platform()!="linux-x64");manager.replies={{200,json(metadata(bytes))},{200,bytes}};
  PackageDownload client(config(),nullptr,&manager);QSignalSpy ready(&client,&PackageDownload::prepared),done(&client,&PackageDownload::finished);
  connect(&client,&PackageDownload::phaseChanged,&client,[&](const QString &key){
   if((at=="metadata"&&key=="update.package_checking")||(at=="downloading"&&key=="update.package_downloading")||(at=="verifying"&&key=="update.package_verifying")||(at=="extracting"&&key=="update.package_extracting"))client.cancel();
  });
  connect(&client,&PackageDownload::sourceChanged,&client,[&]{if(at=="source")client.cancel();});
  connect(&client,&PackageDownload::progress,&client,[&](qint64 n,qint64){if(at=="zero-progress"&&n==0)client.cancel();});
  client.prepare(version());QTRY_COMPARE(ready.size(),1);
  QTemporaryDir dir;
  if(at=="metadata"){QVERIFY(!ready[0][0].toBool());QCOMPARE(manager.urls.size(),0);}
  else{
   QVERIFY(ready[0][0].toBool());client.start(dir.path(),"GitHub");QTRY_COMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());
   QCOMPARE(manager.urls.size(),at=="verifying"||at=="extracting"?2:1);
  }
  QVERIFY(!client.busy());QVERIFY(QDir(dir.path()).entryList({".aster-download-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
 }

 void metadataFallback_data(){QTest::addColumn<QString>("mode");for(const auto *mode:{"network","timeout","missing","invalid","cancel"})QTest::newRow(mode)<<QString(mode);}
 void metadataFallback(){
  QFETCH(QString,mode);Manager manager;auto cfg=config();cfg.mirrorFallback=true;
  manager.replies.append(mode=="timeout"||mode=="cancel"?Response{200,{},10000}:mode=="invalid"?Response{200,"not JSON"}:Response{mode=="missing"?404:503,{}});
  manager.replies.append(Response{200,json(metadata("abc",true))});
  PackageDownload client(cfg,nullptr,&manager);QSignalSpy done(&client,&PackageDownload::prepared);
  client.prepare("v"+version());if(mode=="cancel")client.cancel();QTRY_COMPARE(done.size(),1);
  const bool fallback=mode!="invalid"&&mode!="cancel";QCOMPARE(done[0][0].toBool(),fallback);
  QCOMPARE(manager.urls.size(),fallback?2:1);
  if(fallback){QCOMPARE(manager.urls.last().host(),QString("gitee.com"));QCOMPARE(manager.urls.last().path(),manager.urls.first().path());}
 }
 void cancelAndRestartAtNotification(){
  Manager manager;auto bytes=pack(entries(),platform()!="linux-x64");manager.replies={{200,json(metadata(bytes))},{200,bytes}};
  PackageDownload client(config(),nullptr,&manager);QSignalSpy ready(&client,&PackageDownload::prepared),done(&client,&PackageDownload::finished);
  client.prepare(version());QTRY_COMPARE(ready.size(),1);QTemporaryDir dir;bool restarted=false;
  connect(&client,&PackageDownload::progress,&client,[&](qint64 n,qint64){if(!restarted&&n==0){restarted=true;client.cancel();client.start(dir.path(),"GitHub");}});
  client.start(dir.path(),"GitHub");QTRY_COMPARE(done.size(),2);QVERIFY(!done[0][0].toBool());QVERIFY(done[1][0].toBool());QCOMPARE(manager.urls.size(),2);QVERIFY(!client.busy());
 }
 void giteeMetadataOrigin(){
  Manager manager;const auto bytes=pack(entries());manager.replies={{302,{},1,"https://gitee.com/ChenCLian/Aster/attach_files/123/download/Aster-update.json"},{302,{},1,"https://foruda.gitee.com/attach_file/123/Aster-update.json?token=public-temporary"},{200,json(metadata(bytes,true))}};
  PackageDownload client(config(),nullptr,&manager);QSignalSpy prepared(&client,&PackageDownload::prepared);
  client.prepare("v"+version(),"Gitee");QVERIFY(prepared.wait());QVERIFY(prepared[0][0].toBool());QCOMPARE(manager.urls.size(),3);
  QCOMPARE(manager.urls[0].host(),QString("gitee.com"));QCOMPARE(manager.urls[1].host(),QString("gitee.com"));QCOMPARE(manager.urls[2].host(),QString("foruda.gitee.com"));
  for(const auto *url:{"https://other.test/attach_file/123","https://foruda.gitee.com.evil.test/attach_file/123","https://gitee.com/other/repo/attach_files/123/download","http://foruda.gitee.com/attach_file/123"})QVERIFY(!allowedPackageUrl(QUrl(url),config(),"Gitee",true));
  QVERIFY(!allowedPackageUrl(QUrl("https://foruda.gitee.com/attach_file/123"),config(),"Gitee",false));
 }
 void initTestCase(){QCoreApplication::setApplicationVersion("1.3.0-beta.16");QVERIFY(!platform().isEmpty());}
 void metadataValidation_data(){QTest::addColumn<QString>("mutation");for(const auto *name:{"valid","schema","version","alpha","platform","filename","negative","fraction","huge","hash","http","host","userinfo","tag","duplicate","empty","query"})QTest::newRow(name)<<QString(name);}
 void metadataValidation(){
  QFETCH(QString,mutation);auto m=metadata("abc");auto p=m["packages"].toArray()[0].toObject();auto s=p["sources"].toArray()[0].toObject();
  if(mutation=="schema")m["schema"]=2;if(mutation=="version")m["version"]="1.0.0";if(mutation=="alpha")m["version"]="99.0.0-alpha.1";
  if(mutation=="platform")p["platform"]="wrong";if(mutation=="filename")p["file"]="../file";if(mutation=="negative")p["bytes"]=-1;if(mutation=="fraction")p["bytes"]=2.5;if(mutation=="huge")p["unpacked_bytes"]=1e20;if(mutation=="hash")p["sha256"]="ab";
  auto url=s["url"].toString();if(mutation=="http")url.replace("https:","http:");if(mutation=="host")url.replace("github.com","github.com.evil.test");if(mutation=="userinfo")url.replace("https://","https://x@");if(mutation=="tag")url.replace("/v99.0.0-beta.1/","/v88.0.0/");if(mutation=="query")url+="?anything=1";
  s["url"]=url;p["sources"]=mutation=="empty"?QJsonArray{}:QJsonArray{s};m["packages"]=mutation=="duplicate"?QJsonArray{p,p}:QJsonArray{p};QString error;
  auto parsed=parseDownloadPackage(json(m),config(),"v"+version(),platform(),&error);QCOMPARE(bool(parsed),mutation=="valid");if(!parsed)QVERIFY(!error.isEmpty());
 }
 void archiveValidation_data(){QTest::addColumn<QString>("mutation");for(const auto *name:{"valid-tar","valid-zip","traversal","absolute","drive","backslash","duplicate","case-collision","reserved","outside-root","symlink-escape","symlink-loop","symlink-file-child","fifo","wrong-version","missing-entry","limit","truncated","cancelled","existing-stage","valid-symlink"})QTest::newRow(name)<<QString(name);}
 void archiveValidation(){
  QFETCH(QString,mutation);auto list=entries();
  if(mutation=="traversal")list.append(Entry{root()+"/../escape","bad"});if(mutation=="absolute")list.append(Entry{"/escape","bad"});if(mutation=="drive")list.append(Entry{"C:/escape","bad"});if(mutation=="backslash")list.append(Entry{root()+"/..\\escape","bad"});
  if(mutation=="duplicate")list.append(list[0]);if(mutation=="case-collision")list.append(Entry{list[0].name.toUpper(),"bad"});if(mutation=="reserved")list.append(Entry{root()+"/NUL.txt","bad"});if(mutation=="outside-root")list.append(Entry{"other/file","bad"});
  if(mutation=="symlink-escape")list.append(Entry{root()+"/link",{},AE_IFLNK,"../../escape"});if(mutation=="symlink-loop")list.append(Entry{root()+"/link",{},AE_IFLNK,"link"});if(mutation=="symlink-file-child"){list.append(Entry{root()+"/link",{},AE_IFLNK,"target"});list.append(Entry{root()+"/link/child","bad"});}
  if(mutation=="fifo")list.append(Entry{root()+"/pipe",{},AE_IFIFO});if(mutation=="wrong-version")list[1].body="{}";if(mutation=="missing-entry")list.removeFirst();if(mutation=="valid-symlink")list.append(Entry{root()+"/link",{},AE_IFLNK,launch()});
  auto bytes=pack(list,mutation=="valid-zip");QVERIFY(!bytes.isEmpty());if(mutation=="truncated")bytes=bytes.left(bytes.size()/2);
  QString error;auto p=*parseDownloadPackage(json(metadata(bytes)),config(),version(),platform(),&error);if(mutation=="limit")p.unpackedBytes=2;
  QTemporaryDir dir;const auto archive=dir.filePath("archive");QFile file(archive);QVERIFY(file.open(QIODevice::WriteOnly));file.write(bytes);file.close();const auto stage=dir.filePath("extract");
  if(mutation=="existing-stage")QDir().mkdir(stage);auto cancel=std::make_shared<std::atomic_bool>(mutation=="cancelled");const auto result=extractDownload(archive,stage,p,cancel);
  bool expected=mutation=="valid-tar"||mutation=="valid-zip";
#ifndef Q_OS_WIN
  expected|=mutation=="valid-symlink";
#endif
  QCOMPARE(result.ok,expected);if(expected){QVERIFY(QFileInfo::exists(result.directory+"/"+launch()));QVERIFY(!QFileInfo::exists(dir.filePath("escape")));}
 }
 void downloadLifecycle_data(){QTest::addColumn<QString>("mode");for(const auto *name:{"success","network-fallback","hash-fallback","manual-no-fallback","short","oversize","checksum","redirect","redirect-rejected","redirect-loop","timeout","cancel","extract-invalid","target-exists","target-race","unicode-path","cancel-extract","cancel-progress"})QTest::newRow(name)<<QString(name);}
 void downloadLifecycle(){
  QFETCH(QString,mode);const auto bytes=pack(entries(),platform()!="linux-x64");Manager manager;manager.replies.append(Response{200,json(metadata(bytes,true))});
  if(mode=="network-fallback"||mode=="manual-no-fallback")manager.replies.append(Response{500,{}});
  if(mode=="hash-fallback")manager.replies.append(Response{200,QByteArray(bytes.size(),'x')});
  if(mode=="redirect")manager.replies.append(Response{302,{},1,"https://release-assets.githubusercontent.com/asset"});
  if(mode=="redirect-rejected")manager.replies.append(Response{302,{},1,"https://evil.test/asset"});
  if(mode=="redirect-loop")for(int i=0;i<6;++i)manager.replies.append(Response{302,{},1,"https://release-assets.githubusercontent.com/asset"});
  auto payload=bytes;if(mode=="short")payload.chop(1);if(mode=="oversize")payload+='x';if(mode=="checksum")payload[0]='x';
  if(mode=="extract-invalid"){payload="this is not an archive";manager.replies[0].body=json(metadata(payload));}
  manager.replies.append(Response{200,payload,(mode=="timeout"||mode=="cancel")?10000:1});
  PackageDownload client(config(),nullptr,&manager);QSignalSpy prepared(&client,&PackageDownload::prepared),finished(&client,&PackageDownload::finished);
  client.prepare("v"+version());QTRY_COMPARE(prepared.size(),1);QVERIFY(prepared[0][0].toBool());
  QTemporaryDir directory;auto parent=directory.path();if(mode=="unicode-path"){parent=directory.filePath(QString::fromUtf8("中文 目录"));QVERIFY(QDir().mkdir(parent));}
  if(mode=="target-exists")QVERIFY(QDir().mkdir(parent+'/'+root()));
  if(mode=="target-race")connect(&client,&PackageDownload::phaseChanged,&client,[&](const QString &key){if(key=="update.package_extracting")QDir().mkdir(parent+'/'+root());});
  if(mode=="cancel-extract")connect(&client,&PackageDownload::phaseChanged,&client,[&](const QString &key){if(key=="update.package_extracting")QTimer::singleShot(0,&client,&PackageDownload::cancel);});
  if(mode=="cancel-progress")connect(&client,&PackageDownload::progress,&client,[&](qint64 n,qint64){if(n>0)client.cancel();});
  const bool automatic=mode=="network-fallback"||mode=="hash-fallback";
  client.start(parent,automatic?"auto":"GitHub");if(mode=="cancel")client.cancel();
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QVERIFY(!client.busy());
  const bool expected=mode=="success"||mode=="network-fallback"||mode=="hash-fallback"||mode=="redirect"||mode=="unicode-path";
  QCOMPARE(finished[0][0].toBool(),expected);
  if(expected)QVERIFY(QFileInfo::exists(client.completedDirectory()+"/"+launch()));else QVERIFY(client.completedDirectory().isEmpty());
  QVERIFY(QDir(parent).entryList({".aster-download-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
  if(mode=="network-fallback"||mode=="hash-fallback")QCOMPARE(manager.urls.last().host(),QString("gitee.com"));
  if(mode=="manual-no-fallback")QCOMPARE(manager.urls.size(),2);
 }
 void metadataMissingTimeoutCancel(){
  Manager manager;manager.replies={{404,{}},{200,{},10000},{200,{},10000}};PackageDownload client(config(),nullptr,&manager);QSignalSpy prepared(&client,&PackageDownload::prepared);
  client.prepare(version());QTRY_COMPARE(prepared.size(),1);QCOMPARE(prepared[0][1].toString(),QString("update.package_missing"));
  client.prepare(version());QTRY_COMPARE(prepared.size(),2);QCOMPARE(prepared[1][1].toString(),QString("update.error_timeout"));
  client.prepare(version());client.cancel();QCOMPARE(prepared.size(),3);QVERIFY(!client.busy());
 }
 void repeatedPreparationResetsSize(){Manager m;auto data=json(metadata("abc"));m.replies={{200,data},{200,data}};PackageDownload c(config(),nullptr,&m);QSignalSpy ready(&c,&PackageDownload::prepared);c.prepare(version());QTRY_COMPARE(ready.size(),1);c.prepare(version());QTRY_COMPARE(ready.size(),2);QVERIFY(ready[1][0].toBool());}
};
QTEST_GUILESS_MAIN(DownloadTests)
#include "download_tests.moc"
