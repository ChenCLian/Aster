// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_DOWNLOAD_PACKAGE_H
#define ASTER_DOWNLOAD_PACKAGE_H
#include "update/release_feed.h"
#include <atomic>
#include <memory>
namespace aster::updates {
struct PackageSource { QString name; QUrl url; };
struct DownloadPackage {
    QString version,platform,file,root,entry;
    qint64 bytes=0,unpackedBytes=0;
    QByteArray sha256;
    QVector<PackageSource> sources;
};
QString nativeUpdatePlatform();
bool allowedPackageUrl(const QUrl &, const UpdateConfig &, const QString &source, bool redirect=false);
std::optional<DownloadPackage> parseDownloadPackage(const QByteArray &, const UpdateConfig &,
    const QString &version,const QString &platform,QString *error);
struct ExtractionResult { bool ok=false,cancelled=false;QString error,directory; };
// Only extracts into a fresh private directory; never executes downloaded code.
ExtractionResult extractDownload(const QString &archivePath,const QString &staging,
    const DownloadPackage &, const std::shared_ptr<std::atomic_bool> &cancel);
}
#endif
