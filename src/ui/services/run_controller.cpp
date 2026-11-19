// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/presentation.h"
#include "ui/services/run_controller.h"
#include <QThread>
#include <QElapsedTimer>
#include <QHash>
namespace aster::ui
{
RunController::RunController(QObject *p) : QObject(p)
{
    qRegisterMetaType<RunEvent>();
    qRegisterMetaType<BatchItem>();
    qRegisterMetaType<std::shared_ptr<const BatchResult>>();
}
RunController::~RunController()
{
    m_cancel.cancel();
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
bool RunController::launch(std::function<BatchResult()> work)
{
    if (busy())
        return false;
    m_worker = QThread::create(
        [this, work = std::move(work)]
        {
            BatchResult result;
            try
            {
                result = work();
            }
            catch (const std::exception &e)
            {
                result.error = QString::fromUtf8(e.what());
            }
            catch (...)
            {
                result.error = text("error.unexpected_background_error");
            }
            emit batchReady(std::make_shared<const BatchResult>(std::move(result)));
        });
    connect(m_worker, &QThread::finished, this,
            [this]
            {
                auto *worker = m_worker;
                m_worker = nullptr;
                worker->deleteLater();
                emit busyChanged(false);
            });
    emit busyChanged(true);
    m_worker->start();
    return true;
}
bool RunController::start(const RunPlan &plan, const QString &path, int concurrency)
{
    if (busy())
        return false;
    m_cancel = CancellationToken();
    return launch(
        [this, plan, path, concurrency, cancel = m_cancel]
        {
            QElapsedTimer clock;
            clock.start();
            struct LastProgress { qint64 at; int percent; QString phase; };
            QHash<QPair<RunId, NodeId>, LastProgress> last;
            // BatchRunner serializes this callback. Coalesce algorithm updates
            // before posting to Qt; every start and terminal event is delivered.
            const auto notify = [&](const RunEvent &event) {
                const auto key = qMakePair(event.runId, event.nodeId);
                if (event.progressPercent >= 0 && event.state == NodeState::Running) {
                    const auto now = clock.elapsed();
                    const auto prior = last.constFind(key);
                    if (prior != last.cend() && prior->phase == event.message &&
                        (prior->percent == event.progressPercent ||
                         (event.progressPercent < 100 && now - prior->at < 200))) return;
                    last.insert(key, {now, event.progressPercent, event.message});
                } else if (event.state != NodeState::Running && event.state != NodeState::Pending) {
                    last.remove(key);
                }
                emit eventReady(event);
            };
            return BatchRunner().run(
                plan, path, concurrency, notify,
                [this](const BatchItem &item) { emit itemReady(item); }, cancel,
                PendingInputsOnCancel::LeaveUnstarted);
        });
}
bool RunController::retry(const QVector<std::shared_ptr<const RunResult>> &results,
                          const QString &path)
{
    if (busy())
        return false;
    m_cancel = CancellationToken();
    return launch(
        [this, results, path, cancel = m_cancel]
        {
            BatchResult batch;
            ResultRepository repo;
            batch.error = repo.open(path);
            if (!batch.error.isEmpty())
                return batch;
            for (const auto &result : results)
            {
                if (cancel.isCancelled())
                    break;
                if (!result)
                    continue;
                BatchItem item{
                    result->runId, result->sourceLabel, result->state, repo.save(*result), {},
                    result->root};
                if (!item.persistence.saved())
                    item.unsavedResult = result;
                batch.items.push_back(item);
                emit itemReady(item);
            }
            return batch;
        });
}
} // namespace aster::ui
