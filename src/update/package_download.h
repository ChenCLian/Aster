// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_PACKAGE_DOWNLOAD_H
#define ASTER_PACKAGE_DOWNLOAD_H
#include "update/download_package.h"
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
namespace aster::updates {
class PackageDownload : public QObject {
    Q_OBJECT
public:
    explicit PackageDownload(UpdateConfig config,QObject *parent=nullptr,QNetworkAccessManager *manager=nullptr);
    ~PackageDownload() override;
    bool busy() const {return m_busy;}
    std::optional<DownloadPackage> package() const {return m_package;}
    QString metadataSource() const {return m_metadataSource;}
    QString automaticSource(const QString &lastSuccess) const;
    const QList<QPair<QString,QString>> &metadataErrors() const {return m_metadataErrors;}
    QString completedDirectory() const {return m_completed;}
    void prepare(const QString &tag,const QString &source=QStringLiteral("GitHub"));
    void start(const QString &parentDirectory,const QString &source="auto",const QString &preferred={});
    void cancel();
signals:
    void phaseChanged(QString key);
    void progress(qint64 received,qint64 total);
    void prepared(bool available,QString error);
    void finished(bool success,QString error);
    void sourceChanged(QString source);
private:
    void beginMetadata(const QString &source, bool legacyAttachment=false);
    void request(const QUrl &url);
    void consume();
    void responseFinished();
    void beginSource();
    void fail(const QString &error,bool mayFallback=false);
    void extract();
    void cleanup();
    UpdateConfig m_config;
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_idle,m_deadline;
    QByteArray m_body;
    QString m_tag,m_failure,m_parent,m_completed,m_metadataSource=QStringLiteral("GitHub");
    std::optional<DownloadPackage> m_package;
    QList<QPair<QString,QString>> m_metadataErrors;
    QVector<PackageSource> m_sources;
    int m_source=0,m_redirects=0;
    bool m_busy=false,m_metadata=false,m_giteeReleaseRecord=false;
    quint64 m_generation=0;
    qint64 m_received=0;
    std::unique_ptr<QTemporaryDir> m_temp;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QThread *m_worker=nullptr;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
}
#endif
