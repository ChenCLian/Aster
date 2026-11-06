// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/presentation.h"
#include "ui/services/run_controller.h"
#include <QThread>
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
            return BatchRunner().run(
                plan, path, concurrency, [this](const RunEvent &event) { emit eventReady(event); },
                [this](const BatchItem &item) { emit itemReady(item); }, cancel);
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
