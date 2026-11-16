// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "update/package_download.h"
#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QDir>
namespace aster::updates {
PackageDownload::PackageDownload(UpdateConfig config,QObject *parent,QNetworkAccessManager *manager)
    :QObject(parent),m_config(std::move(config)),m_manager(manager?manager:new QNetworkAccessManager(this)) {
    m_idle.setSingleShot(true);m_deadline.setSingleShot(true);
    auto timeout=[this]{if(m_reply){m_failure="update.error_timeout";m_reply->abort();}};
    connect(&m_idle,&QTimer::timeout,this,timeout);connect(&m_deadline,&QTimer::timeout,this,timeout);
}
PackageDownload::~PackageDownload(){
    if(m_cancel)m_cancel->store(true);
    if(m_reply){disconnect(m_reply,nullptr,this,nullptr);m_reply->abort();m_reply->deleteLater();}
    if(m_worker){m_worker->wait();delete m_worker;}
    m_file.close();
}
void PackageDownload::prepare(const QString &tag,const QString &source){
    if(m_busy)return;
    ++m_generation;
    m_package.reset();m_completed.clear();m_tag=tag;m_metadataSource=source;
    if((source!="GitHub"&&source!="Gitee")||!m_config.valid()||m_config.packageMetadata!="Aster-update.json"||isAlphaVersion(QCoreApplication::applicationVersion())||isAlphaVersion(tag)) {emit prepared(false,"update.package_missing");return;}
    if(!ReleaseVersion::parse(tag)){emit prepared(false,"update.package_invalid");return;}
    m_metadata=true;m_busy=true;beginMetadata(source);
}
void PackageDownload::beginMetadata(const QString &source){
    const auto generation=m_generation;QPointer<PackageDownload> alive(this);
    m_metadataSource=source;m_received=0;m_body.clear();m_failure.clear();m_redirects=0;
    QUrl url=m_config.releasesUrl();if(source=="Gitee")url.setHost("gitee.com");url.setPath(url.path()+"/download/"+m_tag+'/'+m_config.packageMetadata);
    emit phaseChanged("update.package_checking");
    if(!alive||!m_busy||generation!=m_generation)return;
    m_deadline.start(m_config.timeoutMs);request(url);
}

void PackageDownload::request(const QUrl &url){
    QNetworkRequest request(url);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setRawHeader("User-Agent","Aster-package-download");request.setRawHeader("Accept-Encoding","identity");
    request.setTransferTimeout(m_config.timeoutMs);
    m_reply=m_manager->get(request);m_reply->setReadBufferSize(65536);
    connect(m_reply,&QNetworkReply::readyRead,this,&PackageDownload::consume);
    connect(m_reply,&QNetworkReply::finished,this,&PackageDownload::responseFinished);
    m_idle.start(m_config.timeoutMs);
}
void PackageDownload::consume(){
    QPointer<PackageDownload> alive(this);const auto generation=m_generation;const auto reply=m_reply;
    if(!m_busy||!m_reply||!m_failure.isEmpty()||m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)return;
    const auto limit=m_metadata?1024LL*1024:m_package->bytes;
    while(m_reply->bytesAvailable()>0){
        const auto part=m_reply->read(qMin<qint64>(65536,limit-m_received+1));if(part.isEmpty())break;
        m_received+=part.size();m_idle.start(m_config.timeoutMs);
        if(m_received>limit){m_failure="update.package_size";if(!m_reply->isFinished())m_reply->abort();return;}
        if(m_metadata)m_body+=part;
        else {if(m_file.write(part)!=part.size()){m_failure="update.disk_error";if(!m_reply->isFinished())m_reply->abort();return;}m_hash.addData(part);emit progress(m_received,limit);if(!alive||!m_busy||generation!=m_generation||m_reply!=reply)return;}
    }
}
void PackageDownload::responseFinished(){
    if(!m_reply||!m_busy)return;
    QPointer<PackageDownload> alive(this);const auto generation=m_generation;const auto reply=m_reply;
    consume();if(!alive||!m_busy||generation!=m_generation||m_reply!=reply||!m_reply)return;
    const auto status=m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error=m_reply->error();const auto redirect=m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    const auto next=m_reply->url().resolved(redirect);
    m_reply->deleteLater();m_reply=nullptr;m_idle.stop();
    if(m_failure.isEmpty()&&status>=300&&status<400){
        const auto source=m_metadata?m_metadataSource:m_sources[m_source].name;
        if(redirect.isEmpty()||++m_redirects>5||!allowedPackageUrl(next,m_config,source,true)){fail("update.error_redirect");return;}
        request(next);return;
    }
    auto failure=m_failure;
    if(failure.isEmpty()&&(status!=200||error!=QNetworkReply::NoError))failure=status==404&&m_metadata?"update.package_missing":"update.error_network";
    if(!failure.isEmpty()){fail(failure,!m_metadata&&failure!="update.disk_error");return;}
    m_deadline.stop();
    if(m_metadata){
        m_package=parseDownloadPackage(m_body,m_config,m_tag,nativeUpdatePlatform(),&failure);
        m_body.clear();m_busy=false;emit prepared(bool(m_package),failure);return;
    }
    if(!m_file.flush()){fail("update.disk_error");return;}m_file.close();
    emit phaseChanged("update.package_verifying");
    if(!alive||!m_busy||generation!=m_generation)return;
    if(m_received!=m_package->bytes){fail("update.package_size",true);return;}
    if(m_hash.result()!=m_package->sha256){fail("update.package_checksum",true);return;}
    extract();
}
void PackageDownload::start(const QString &parentDirectory,const QString &source,const QString &preferred){
    if(m_busy)return;
    if(!m_package){emit finished(false,"update.package_missing");return;}
    m_sources.clear();for(const auto &s:m_package->sources)if(source=="auto"||source==s.name)m_sources.append(s);
    if(m_sources.isEmpty()){emit finished(false,"update.package_missing");return;}
    if(source=="auto"&&!preferred.isEmpty())for(qsizetype i=0;i<m_sources.size();++i)if(m_sources[i].name==preferred){m_sources.move(i,0);break;}
    QFileInfo parent(parentDirectory);m_parent=parent.canonicalFilePath();m_completed.clear();
    if(m_parent.isEmpty()||!parent.isDir()||!parent.isWritable()){emit finished(false,"update.disk_error");return;}
    const auto target=QDir(m_parent).filePath(m_package->root);
    if(QFileInfo::exists(target)||QFileInfo(target).isSymLink()){emit finished(false,"update.directory_exists");return;}
    const QStorageInfo storage(m_parent);
    if(storage.isValid()&&storage.bytesAvailable()>=0&&storage.bytesAvailable()<m_package->bytes+m_package->unpackedBytes+32LL*1024*1024){emit finished(false,"update.disk_space");return;}
    m_temp=std::make_unique<QTemporaryDir>(QDir(m_parent).filePath(".aster-download-XXXXXX"));
    if(!m_temp->isValid()){m_temp.reset();emit finished(false,"update.disk_error");return;}
    ++m_generation;m_busy=true;m_metadata=false;m_source=0;m_cancel=std::make_shared<std::atomic_bool>(false);beginSource();
}
void PackageDownload::beginSource(){
    QPointer<PackageDownload> alive(this);const auto generation=m_generation;
    m_file.close();m_file.setFileName(m_temp->filePath("package.part"));
    if(!m_file.open(QIODevice::WriteOnly|QIODevice::Truncate)){fail("update.disk_error");return;}
    m_hash.reset();m_received=0;m_redirects=0;m_failure.clear();
    emit sourceChanged(m_sources[m_source].name);
    if(!alive||!m_busy||generation!=m_generation)return;
    emit phaseChanged("update.package_downloading");
    if(!alive||!m_busy||generation!=m_generation)return;
    emit progress(0,m_package->bytes);
    if(!alive||!m_busy||generation!=m_generation)return;
    m_deadline.start(30*60*1000);request(m_sources[m_source].url);
}
void PackageDownload::fail(const QString &error,bool mayFallback){
    m_idle.stop();m_deadline.stop();m_file.close();
    // Release discovery can succeed while GitHub's attachment host is blocked.
    // Retry only transport/missing-asset failures for the same selected version.
    if(m_metadata&&m_metadataSource=="GitHub"&&m_config.mirrorFallback&&
       (error=="update.error_timeout"||error=="update.error_network"||error=="update.package_missing")){
        beginMetadata("Gitee");return;
    }
    if(mayFallback&&m_source+1<m_sources.size()){++m_source;beginSource();return;}
    m_busy=false;
    if(m_metadata){m_body.clear();emit prepared(false,error);}
    else {cleanup();emit finished(false,error);}
}
void PackageDownload::cleanup(){m_file.close();m_temp.reset();}
void PackageDownload::cancel(){
    if(!m_busy)return;
    ++m_generation;
    if(m_cancel)m_cancel->store(true);
    if(m_worker){emit phaseChanged("update.package_cancelling");return;} // Worker checks between chunks.
    if(m_reply){disconnect(m_reply,nullptr,this,nullptr);m_reply->abort();m_reply->deleteLater();m_reply=nullptr;}
    fail("update.cancelled");
}
void PackageDownload::extract(){
    QPointer<PackageDownload> alive(this);const auto generation=m_generation;
    emit phaseChanged("update.package_extracting");
    if(!alive||!m_busy||generation!=m_generation)return;
    const auto path=m_file.fileName(),staging=m_temp->filePath("extract");const auto package=*m_package;
    const auto cancel=m_cancel;const auto result=std::make_shared<ExtractionResult>();
    m_worker=QThread::create([path,staging,package,cancel,result]{*result=extractDownload(path,staging,package,cancel);});
    connect(m_worker,&QThread::finished,this,[this,result]{
        m_worker->wait();m_worker->deleteLater();m_worker=nullptr;
        if(m_cancel->load()||result->cancelled){fail("update.cancelled");return;}
        if(!result->ok){fail(result->error);return;}
        const auto target=QDir(m_parent).filePath(m_package->root);
        // QDir::rename refuses an occupied destination. Staging is on the same filesystem.
        if(QFileInfo::exists(target)||QFileInfo(target).isSymLink()||!QDir().rename(result->directory,target)){fail("update.directory_exists");return;}
        m_completed=target;m_busy=false;
        // A cleanup failure never removes or invalidates the completed new version.
        const bool cleaned=m_temp->remove();m_temp.reset();emit finished(true,cleaned?QString{}:QString("update.cache_cleanup"));
    });
    m_worker->start();
}
}
