// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "update/update_checker.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxyFactory>
namespace aster::updates {
UpdateChecker::UpdateChecker(UpdateConfig config,QObject *parent,QNetworkAccessManager *manager)
    :QObject(parent),m_config(std::move(config)),m_manager(manager?manager:new QNetworkAccessManager(this)) {
    if(!manager)QNetworkProxyFactory::setUseSystemConfiguration(true);
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{if(m_reply){m_transportFailure="update.error_timeout";m_reply->abort();}});
}
UpdateChecker::~UpdateChecker() {
    if(m_reply){disconnect(m_reply,nullptr,this,nullptr);m_reply->abort();m_reply->deleteLater();}
}
void UpdateChecker::check(const QString &current,bool includePrereleases) {
    startCheck(current,includePrereleases,false);
}
void UpdateChecker::checkChannel(const QString &current,bool beta) {
    startCheck(current,beta,beta);
}
void UpdateChecker::startCheck(const QString &current,bool includePrereleases,bool betaOnly) {
    if(m_busy)return;
    if(isAlphaVersion(current)){emit finished({UpdateResult::State::Failed,"update.alpha_development_only",{}});return;}
    m_current=ReleaseVersion::parse(current);m_includePrereleases=includePrereleases;m_betaOnly=betaOnly;
    if(!m_config.valid()||!m_current){emit finished({UpdateResult::State::Failed,"update.error_config",{}});return;}
    m_busy=true;m_mirror=false;m_releases.clear();m_page=1;requestPage();
}
void UpdateChecker::requestPage() {
    m_body.clear();m_transportFailure.clear();
    QNetworkRequest request(m_config.apiUrl(m_page,m_mirror));
    request.setRawHeader("Accept",m_mirror?"application/json":"application/vnd.github+json");
    request.setRawHeader("User-Agent","Aster-update-checker");
    if(!m_mirror)request.setRawHeader("X-GitHub-Api-Version",m_config.apiVersion.toLatin1());
    // Use the configured public endpoint; do not follow redirects or ignore TLS errors.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    request.setTransferTimeout(m_config.timeoutMs);
    m_reply=m_manager->get(request);m_reply->setReadBufferSize(65536);
    connect(m_reply,&QNetworkReply::readyRead,this,&UpdateChecker::consume);
    connect(m_reply,&QNetworkReply::finished,this,&UpdateChecker::pageFinished);
    m_deadline.start(m_config.timeoutMs);
}
void UpdateChecker::consume() {
    if(!m_reply||!m_transportFailure.isEmpty())return;
    while(m_reply->bytesAvailable()>0){
        const auto part=m_reply->read(qMin<qint64>(65536,m_config.maximumResponseBytes-m_body.size()+1));
        if(part.isEmpty())break;
        m_body+=part;
        if(m_body.size()>m_config.maximumResponseBytes){m_transportFailure="update.error_response_limit";if(!m_reply->isFinished())m_reply->abort();return;}
    }
}
void UpdateChecker::pageFinished() {
    if(!m_reply||!m_busy)return;
    m_deadline.stop();consume();
    // consume() may abort the reply, but finished is emitted only once by Qt.
    const int status=m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError=m_reply->error();
    const bool limited=m_reply->rawHeader("X-RateLimit-Remaining")=="0"||!m_reply->rawHeader("Retry-After").isEmpty();
    m_reply->deleteLater();m_reply=nullptr;
    QString failure=m_transportFailure;
    if(failure.isEmpty()) {
        if(status==401)failure="update.error_permission";
        else if(status==404)failure="update.error_repository";
        else if(status==429||(status==403&&limited))failure="update.error_rate";
        else if(status==403)failure="update.error_permission";
        else if(status>=300&&status<400)failure="update.error_redirect";
        else if(status>=400)failure="update.error_server";
        else if(networkError!=QNetworkReply::NoError||status!=200)failure="update.error_network";
    }
    if(!failure.isEmpty()){failed(failure);return;}
    const auto page=parseReleasePage(m_body,m_config,m_includePrereleases,m_mirror);
    if(!page.errorKey.isEmpty()){failed(page.errorKey);return;}
    m_releases+=page.releases;
    if(page.hasNext) {
        if(m_page>=m_config.maximumPages){failed("update.error_page_limit");return;}
        ++m_page;requestPage();return;
    }
    if(m_betaOnly)m_releases.removeIf([](const Release &release){
        return release.version.prerelease.isEmpty() ||
               release.version.prerelease.first().compare("beta",Qt::CaseInsensitive)!=0;
    });
    const auto best=newestRelease(m_releases);
    if(!best){finish({UpdateResult::State::NoReleases,"update.none",{}});return;}
    const bool newer=best->version.compare(*m_current)>0;
    finish({newer?UpdateResult::State::Available:UpdateResult::State::Current,newer?"update.available":"update.current",best});
}
void UpdateChecker::failed(const QString &key){
    if(m_config.mirrorFallback&&!m_mirror){m_mirror=true;m_releases.clear();m_page=1;requestPage();return;}
    finish({UpdateResult::State::Failed,key,{}});
}
void UpdateChecker::finish(UpdateResult result) {
    result.partial=m_mirror;
    m_busy=false;m_deadline.stop();m_body.clear();m_releases.clear();
    emit finished(std::move(result));
}
void UpdateChecker::cancel() {
    if(!m_busy)return;
    if(m_reply){disconnect(m_reply,nullptr,this,nullptr);m_reply->abort();m_reply->deleteLater();m_reply=nullptr;}
    finish({UpdateResult::State::Cancelled,"update.cancelled",{}});
}
}
