#include "ui/common/presentation.h"
#include "ui/services/record_directory_service.h"
#include <QThread>
namespace aster::ui
{
RecordDirectoryService::~RecordDirectoryService()
{
    m_transferCancel.cancel();
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
void RecordDirectoryService::invalidate()
{
    ++m_generation;
    for (qsizetype i = m_queue.size(); i-- > 0;)
        if (m_queue[i].kind == Kind::Query)
            m_queue.removeAt(i);
}
void RecordDirectoryService::resetContext()
{
    ++m_context;
    invalidate();
    for (qsizetype i = m_queue.size(); i-- > 0;)
        if (m_queue[i].kind == Kind::Select)
            m_queue.removeAt(i);
}
void RecordDirectoryService::query(QString path, RecordFilter filter, qint64 beforeId,
                                   RecordIdentities selected)
{
    invalidate();
    enqueue({Kind::Query, path, filter, beforeId, 0, {}, {}, m_generation, std::move(selected)});
}
void RecordDirectoryService::edit(QString path, qint64 id, QString name, QString notes)
{
    enqueue({Kind::Edit, path, {}, 0, id, name, notes, 0});
}
void RecordDirectoryService::remove(QString path, qint64 id)
{
    enqueue({Kind::Remove, path, {}, 0, id, {}, {}, 0});
}
void RecordDirectoryService::selectMatching(QString path, RecordFilter filter, bool checked)
{
    enqueue({Kind::Select, path, filter, 0, 0, {}, {}, 0, {}, checked, m_context});
}
void RecordDirectoryService::removeMany(QString path, RecordIdentities selected)
{
    enqueue({Kind::RemoveMany, path, {}, 0, 0, {}, {}, 0, std::move(selected)});
}
void RecordDirectoryService::merge(QString destination, QString source)
{
    m_transferCancel = CancellationToken{};
    Request request{Kind::Merge, destination, {}, 0, 0, {}, {}, 0};
    request.transferPath = source;
    request.cancellation = m_transferCancel;
    enqueue(request);
}
void RecordDirectoryService::backup(QString source, QString newFile)
{
    m_transferCancel = CancellationToken{};
    Request request{Kind::Backup, source, {}, 0, 0, {}, {}, 0};
    request.transferPath = newFile;
    request.cancellation = m_transferCancel;
    enqueue(request);
}
void RecordDirectoryService::cancelTransfer()
{
    m_transferCancel.cancel();
    for (const auto &request : m_queue)
        if (request.kind == Kind::Merge || request.kind == Kind::Backup)
            request.cancellation.cancel();
}
void RecordDirectoryService::enqueue(Request request)
{
    const bool write = request.kind == Kind::Edit || request.kind == Kind::Remove ||
                       request.kind == Kind::RemoveMany || request.kind == Kind::Merge ||
                       request.kind == Kind::Backup;
    if (write && ++m_pendingWrites == 1)
        emit mutationBusyChanged(true);
    m_queue.enqueue(request);
    if (!m_worker)
    {
        emit busyChanged(true);
        start();
    }
}
void RecordDirectoryService::start()
{
    const auto request = m_queue.dequeue();
    auto list = std::make_shared<RecordList>();
    auto units = std::make_shared<QStringList>();
    auto selection = std::make_shared<RecordSelectionState>();
    auto removal = std::make_shared<RecordRemoval>();
    auto transfer = std::make_shared<DatabaseTransferResult>();
    m_worker = QThread::create(
        [this, request, list, units, selection, removal, transfer]
        {
            try
            {
                if (request.kind == Kind::Merge || request.kind == Kind::Backup)
                {
                    const auto progress = [this](QString stage, qint64 done, qint64 total)
                    { emit transferProgress(stage, done, total); };
                    *transfer = request.kind == Kind::Merge
                                    ? mergeDatabase(request.path, request.transferPath,
                                                    request.cancellation, progress)
                                    : backupDatabase(request.path, request.transferPath,
                                                     request.cancellation, progress);
                    return;
                }
                ResultRepository repo;
                list->error = repo.open(request.path, request.kind == Kind::Query);
                if (!list->error.isEmpty())
                    return;
                if (request.kind == Kind::Query)
                {
                    *list = repo.listBefore(request.filter, 100, request.beforeId);
                    if (list->error.isEmpty())
                    {
                        *units = repo.frequencyUnits(&list->error);
                        *selection = repo.selectionState(request.selected, request.filter);
                        if (!selection->error.isEmpty())
                            list->error = selection->error;
                    }
                }
                else if (request.kind == Kind::Edit)
                    list->error = repo.updateMetadata(request.id, request.name, request.notes);
                else if (request.kind == Kind::Remove)
                    list->error = repo.remove(request.id);
                else if (request.kind == Kind::Select)
                {
                    *selection = repo.identities(request.filter);
                    list->error = selection->error;
                }
                else
                {
                    *removal = repo.remove(request.selected);
                    list->error = removal->error;
                }
            }
            catch (const std::exception &e)
            {
                list->error = QString::fromUtf8(e.what());
            }
            catch (...)
            {
                list->error = text("error.unexpected_database_operation_error");
            }
        });
    connect(m_worker, &QThread::finished, this,
            [this, request, list, units, selection, removal, transfer]
            {
                auto *worker = m_worker;
                // Keep m_worker set while notifying observers; enqueued work starts once.
                if (request.kind == Kind::Query)
                {
                    if (request.generation == m_generation)
                    {
                        if (list->error.isEmpty())
                            emit selectionInspected(*selection);
                        emit listed(*list, *units, request.beforeId);
                    }
                }
                else if (request.kind == Kind::Select)
                {
                    selection->error = list->error;
                    if (request.context == m_context)
                        emit selectionReady(request.path, *selection, request.checked);
                }
                else if (request.kind == Kind::RemoveMany)
                {
                    removal->error = list->error;
                    emit removed(request.path, *removal);
                }
                else if (request.kind == Kind::Merge || request.kind == Kind::Backup)
                {
                    if (!list->error.isEmpty())
                        transfer->error = list->error;
                    emit transferred(request.path, *transfer, request.kind == Kind::Merge);
                }
                else
                    emit edited(request.path, request.id, request.kind == Kind::Remove,
                                list->error);
                const bool write = request.kind == Kind::Edit || request.kind == Kind::Remove ||
                                   request.kind == Kind::RemoveMany ||
                                   request.kind == Kind::Merge || request.kind == Kind::Backup;
                if (write && --m_pendingWrites == 0)
                    emit mutationBusyChanged(false);
                m_worker = nullptr;
                worker->deleteLater();
                if (!m_queue.isEmpty())
                    start();
                else
                    emit busyChanged(false);
            });
    m_worker->start();
}
} // namespace aster::ui
