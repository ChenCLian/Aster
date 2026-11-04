#ifndef ASTER_RELEASE_FEED_H
#define ASTER_RELEASE_FEED_H
#include "update/release_version.h"
#include <QJsonArray>
#include <QUrl>
namespace aster::updates {
struct UpdateConfig {
    QString owner, repository, apiVersion;
    int timeoutMs=15000, maximumPages=10;
    qsizetype maximumResponseBytes=2*1024*1024;
    static UpdateConfig fromJson(const QByteArray &json);
    bool valid() const;
    QUrl apiUrl(int page) const;
    QUrl releasesUrl() const;
    QUrl releaseUrl(const QString &tag) const;
};
struct Release {
    QString tag, name, notes;
    ReleaseVersion version;
    QUrl pageUrl;
};
struct FeedPage {
    QString errorKey;
    qsizetype entries=0;
    bool hasNext=false;
    QVector<Release> releases;
};
FeedPage parseReleasePage(const QByteArray &body,const UpdateConfig &config,bool includePrereleases);
std::optional<Release> newestRelease(const QVector<Release> &releases);
}
#endif
