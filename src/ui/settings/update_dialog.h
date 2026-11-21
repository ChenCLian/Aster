// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UPDATE_DIALOG_H
#define ASTER_UPDATE_DIALOG_H
#include "update/update_checker.h"
#include "update/package_download.h"
#include <QDialog>
class QLabel;
class QPushButton;
class QToolButton;
class QTabBar;
class QFrame;
class QProgressBar;
class QTextBrowser;
class QShowEvent;
class QComboBox;
namespace aster::ui {
class UpdateDialog : public QDialog {
public:
    explicit UpdateDialog(QWidget *parent=nullptr);
    UpdateDialog(updates::UpdateConfig config, QWidget *parent, QNetworkAccessManager *manager=nullptr);
    void reject() override;
protected:
    void showEvent(QShowEvent *event) override;
private:
    void check();
    void showResult(const updates::UpdateResult &result);
    void setState(const char *state, const QString &heading, const QString &detail);
    void browse(const QUrl &url);
    void packagePrepared(bool available,const QString &error);
    void download();
    void openDirectory();
    updates::UpdateConfig m_config;
    updates::UpdateChecker *m_checker;
    updates::PackageDownload *m_download;
    std::optional<updates::Release> m_selectedRelease;
    QWidget *m_downloadOptions;
    QComboBox *m_source;
    QLabel *m_destination, *m_announcementSource, *m_onlySource;
    QString m_directory,m_usedSource,m_autoSource;
    bool m_packageReady=false,m_downloadDone=false;
    QTabBar *m_channels;
    QFrame *m_summary;
    QLabel *m_heading, *m_status, *m_releaseMeta, *m_hint, *m_diagnostics;
    QTextBrowser *m_notes;
    QToolButton *m_details;
    QPushButton *m_check, *m_cancel, *m_open;
    QProgressBar *m_progress;
    QUrl m_releaseUrl;
    bool m_onlineAllowed, m_autoCheckQueued=false, m_closed=false, m_retry=false;
};
}
#endif
