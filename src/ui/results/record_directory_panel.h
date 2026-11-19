// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_DIRECTORY_PANEL_H
#define ASTER_RECORD_DIRECTORY_PANEL_H
#include "io/database/result_repository.h"
#include <QWidget>
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QToolButton;
class QProgressDialog;
namespace aster::ui
{
class RecordFilterPanel;
class RecordItemDelegate;
class RecordDirectoryService;
enum class RecordSelectionScope;
// Browsing and checked records are separate: rebuilding a page cannot lose checks.
class RecordDirectoryPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit RecordDirectoryPanel(QWidget *parent = nullptr);
    void setDatabase(const QString &path);
    void refresh();
    bool refreshIfIdle();
    void focusRecord(qint64 id);
    void setOpenedRecord(qint64 id);
    void setMutationsEnabled(bool enabled);
    int recordCount() const;
    RecordFilter filter() const;
    const RecordIdentities &checkedRecords() const
    {
        return m_checked;
    }
  signals:
    void recordActivated(qint64 id);
    void recordsRemoved(aster::RecordIdentities records);
    void metadataEdited(qint64 id);
    void busyChanged(bool busy);
    void mutationBusyChanged(bool busy);
    void message(QString message);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void load(bool append);
    void receive(RecordList list, QStringList units, qint64 beforeId, bool selectionOnly);
    void syncChecks();
    void updateActions();
    void fetchIfNeeded();
    void selectRecords(RecordSelectionScope scope, bool checked);
    void mergeDatabaseFile();
    void backupDatabaseFile();
    void exportFileNames();
    void showTransferProgress();
    void toggleCheck(QListWidgetItem *item);
    void deleteRecords(RecordIdentities records);
    void deleteSelection();
    void editRecord(qint64 id, QString name, QString notes);
    RecordIdentities identity(QListWidgetItem *item) const;
    RecordDirectoryService *m_service;
    RecordFilterPanel *m_filters;
    QListWidget *m_list;
    RecordItemDelegate *m_delegate;
    QLabel *m_selection;
    QToolButton *m_database;
    QProgressDialog *m_transferProgress = nullptr;
    QPushButton *m_delete, *m_selectAll, *m_selectFiltered, *m_exportNames;
    QString m_path;
    RecordIdentities m_checked;
    qint64 m_cursor = 0, m_matching = 0, m_databaseTotal = -1, m_pressedCheck = 0;
    bool m_busy = false, m_hasMore = false, m_filterValid = true, m_mutationsEnabled = true,
         m_filterPending = false, m_pagePending = false;
};
} // namespace aster::ui
#endif
