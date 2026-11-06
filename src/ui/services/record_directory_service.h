// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_RECORD_DIRECTORY_SERVICE_H
#define ASTER_RECORD_DIRECTORY_SERVICE_H
#include "io/database/result_repository.h"
#include "io/database/database_transfer.h"
#include <QObject>
#include <QQueue>
class QThread;
namespace aster::ui
{
class RecordDirectoryService : public QObject
{
    Q_OBJECT
  public:
    explicit RecordDirectoryService(QObject *parent = nullptr) : QObject(parent) {}
    ~RecordDirectoryService() override;
    void query(QString path, RecordFilter filter, qint64 beforeId, RecordIdentities selected = {});
    void edit(QString path, qint64 id, QString name, QString notes);
    void remove(QString path, qint64 id);
    void selectMatching(QString path, RecordFilter filter, bool checked);
    void removeMany(QString path, RecordIdentities selected);
    void merge(QString destination, QString source);
    void backup(QString source, QString newFile);
    void cancelTransfer();
    void invalidate();
    void resetContext();
  signals:
    void listed(aster::RecordList records, QStringList units, qint64 beforeId);
    void edited(QString path, qint64 id, bool removed, QString error);
    void selectionInspected(aster::RecordSelectionState state);
    void selectionReady(QString path, aster::RecordSelectionState state, bool checked);
    void removed(QString path, aster::RecordRemoval result);
    void transferred(QString path, aster::DatabaseTransferResult result, bool merge);
    void transferProgress(QString stage, qint64 completed, qint64 total);
    void busyChanged(bool busy);
    void mutationBusyChanged(bool busy);

  private:
    enum class Kind
    {
        Query,
        Edit,
        Remove,
        Select,
        RemoveMany,
        Merge,
        Backup
    };
    struct Request
    {
        Kind kind;
        QString path;
        RecordFilter filter;
        qint64 beforeId = 0;
        qint64 id = 0;
        QString name, notes;
        quint64 generation = 0;
        RecordIdentities selected{};
        bool checked = false;
        quint64 context = 0;
        QString transferPath{};
        CancellationToken cancellation{};
    };
    void enqueue(Request request);
    void start();
    QQueue<Request> m_queue;
    QThread *m_worker = nullptr;
    int m_pendingWrites = 0;
    quint64 m_generation = 0, m_context = 0;
    CancellationToken m_transferCancel;
};
} // namespace aster::ui
#endif
