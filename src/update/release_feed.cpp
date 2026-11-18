// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "update/release_feed.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <cmath>
namespace aster::updates {
UpdateConfig UpdateConfig::fromJson(const QByteArray &json) {
    const auto o=QJsonDocument::fromJson(json).object();
    UpdateConfig c; c.owner=o["owner"].toString();c.repository=o["repository"].toString();c.apiVersion=o["api_version"].toString();
    c.packageMetadata=o["package_metadata"].toString();c.mirrorFallback=o["mirror_fallback"].toBool();
    return c;
}
bool UpdateConfig::valid() const {
    static const QRegularExpression name("^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$");
    static const QRegularExpression date("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
    return name.match(owner).capturedLength()==owner.size()&&!owner.isEmpty()&&
           name.match(repository).capturedLength()==repository.size()&&!repository.isEmpty()&&
           date.match(apiVersion).capturedLength()==apiVersion.size()&&!apiVersion.isEmpty()&&
           timeoutMs>0&&maximumPages>0&&maximumPages<=100&&maximumResponseBytes>0&&maximumResponseBytes<=16*1024*1024;
}
QUrl UpdateConfig::apiUrl(int page, bool mirror) const {
    QUrl u((mirror?"https://gitee.com/api/v5/repos/":"https://api.github.com/repos/")+owner+"/"+repository+"/releases");
    QUrlQuery q;q.addQueryItem("per_page","100");q.addQueryItem("page",QString::number(page));u.setQuery(q);return u;
}
QUrl UpdateConfig::releasesUrl() const {return QUrl("https://github.com/"+owner+"/"+repository+"/releases");}
QUrl UpdateConfig::releaseUrl(const QString &tag) const {
    // Never launch a URL supplied by release notes or API payloads.
    QUrl u=releasesUrl();u.setPath(u.path()+"/tag/"+tag);return u;
}
bool takeGiteeUpdateRecord(QString &notes, QByteArray &record) {
    notes.replace("\r\n", "\n");
    const QString marker = QStringLiteral("<!-- aster-update-v1\n");
    const auto start = notes.indexOf(marker);
    record.clear();
    if (start < 0) return true; // Legacy releases use a metadata attachment.
    const auto content = start + marker.size();
    const auto end = notes.indexOf("\n-->", content);
    if (end < 0 || notes.indexOf(marker, content) >= 0) {
        notes.truncate(start);
        return false;
    }
    record = notes.mid(content, end - content).trimmed().toUtf8();
    notes.remove(start, end + 4 - start);
    return !record.isEmpty();
}
FeedPage parseReleasePage(const QByteArray &body,const UpdateConfig &config,bool includePrereleases,bool mirror) {
    FeedPage result;
    if(body.size()>config.maximumResponseBytes){result.errorKey="update.error_response_limit";return result;}
    QJsonParseError error;const auto document=QJsonDocument::fromJson(body,&error);
    if(error.error!=QJsonParseError::NoError||!document.isArray()) {result.errorKey="update.error_response";return result;}
    const auto rows=document.array();result.entries=rows.size();result.hasNext=rows.size()==100;
    if(rows.size()>100){result.errorKey="update.error_response";return result;}
    for(const auto &value:rows) {
        if(!value.isObject()){result.errorKey="update.error_response";return result;}
        auto o=value.toObject();
        if(mirror&&!o.contains("draft"))o.insert("draft",false);
        if(!o["draft"].isBool()||!o["prerelease"].isBool()||!o["tag_name"].isString()) {result.errorKey="update.error_response";return result;}
        if(o["draft"].toBool())continue;
        const auto tag=o["tag_name"].toString();const auto version=ReleaseVersion::parse(tag);
        if(!version||isAlphaVersion(tag)||(!includePrereleases&&(o["prerelease"].toBool()||!version->prerelease.isEmpty())))continue;
        auto pageUrl=config.releaseUrl(tag);if(mirror)pageUrl.setHost("gitee.com");
        auto notes = o["body"].toString();
        if (mirror) {
            QByteArray record;
            takeGiteeUpdateRecord(notes, record);
        }
        result.releases.append({tag,o["name"].toString().left(256),notes.trimmed().left(16000),*version,pageUrl,QDateTime::fromString(o["published_at"].toString(),Qt::ISODate),mirror?QString("Gitee"):QString("GitHub")});
    }
    return result;
}
std::optional<Release> newestRelease(const QVector<Release> &releases) {
    std::optional<Release> best;
    for(const auto &r:releases)if(!best||r.version.compare(best->version)>0)best=r;
    return best;
}
}
