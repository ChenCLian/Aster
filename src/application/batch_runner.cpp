// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "application/batch_runner.h"
#include "application/joint_batch_runner.h"
#include <algorithm>
#include <QThreadPool>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace aster
{
BatchResult BatchRunner::run(const RunPlan &plan, const QString &databasePath, int concurrency,
                             EventSink nodeEvents, CompletionSink completed,
                             CancellationToken cancellation, PendingInputsOnCancel pending) const
{
    BatchResult batch;
    if (!plan.validation.ok() || (plan.requests.isEmpty() && plan.batches.isEmpty()) || concurrency < minimumConcurrency || concurrency > maximumConcurrency)
    {
        batch.error = QString("Invalid run plan or concurrency (expected %1..%2)")
                          .arg(minimumConcurrency).arg(maximumConcurrency);
        return batch;
    }
    ResultRepository repository;
    batch.error = repository.open(databasePath);
    if (!batch.error.isEmpty())
        return batch;
    std::mutex eventMutex;
    EventSink serialized = [&](const RunEvent &event)
    {
        if (nodeEvents)
        {
            std::lock_guard<std::mutex> lock(eventMutex);
            nodeEvents(event);
        }
    };
    std::mutex completionMutex;
    std::condition_variable completionChanged;
    struct FinishedRun { qsizetype index; RunResult result; };
    std::vector<FinishedRun> ready;
    ready.reserve(concurrency);
    QThreadPool workers;
    workers.setMaxThreadCount(concurrency);
    // Destroy the pool before the queue and callbacks it uses. The caller
    // consumes completions in finish order; input order is kept in batch.items.
    qsizetype next = 0;
    int active = 0;
    batch.items.resize(plan.requests.size());
    const auto launch = [&]
    {
        const auto index = next;
        workers.start([&, index] {
            const auto &request = plan.requests[index];
            RunResult result;
            try {
                result = WorkflowExecutor().run(request, serialized, cancellation);
            } catch (const std::exception &e) {
                result.error = QString::fromUtf8(e.what());
            } catch (...) {
                result.error = "Unexpected worker failure";
            }
            if (result.runId.isNull()) {
                result.runId = QUuid::createUuid();
                result.root = request.root;
                result.sourceLabel = request.source.label;
            }
            {
                std::lock_guard<std::mutex> lock(completionMutex);
                ready.push_back({index, std::move(result)});
            }
            completionChanged.notify_one();
        });
        ++next;
        ++active;
    };
    bool launchFailed = false;
    const auto tryLaunch = [&]
    {
        try
        {
            launch();
        }
        catch (const std::exception &e)
        {
            launchFailed = true;
            batch.error = QString::fromUtf8(e.what());
        }
        catch (...)
        {
            launchFailed = true;
            batch.error = "Cannot launch worker";
        }
    };
    const auto accepting = [&] {
        return pending == PendingInputsOnCancel::RecordCancelled || !cancellation.isCancelled();
    };
    while (!launchFailed && accepting() && next < plan.requests.size() &&
           active < concurrency)
        tryLaunch();
    // Even if launching another worker fails, drain and save all work already
    // accepted. The remaining requests were never started; batch.error reports it.
    while (active > 0)
    {
        FinishedRun finished;
        {
            std::unique_lock<std::mutex> lock(completionMutex);
            completionChanged.wait(lock, [&] { return !ready.empty(); });
            finished = std::move(ready.front());
            ready.erase(ready.begin());
        }
        --active;
        auto &result = finished.result;
        // Refill the free compute slot before serial persistence/observers.
        if (!launchFailed && accepting() && next < plan.requests.size())
            tryLaunch();
        BatchItem item{result.runId, result.sourceLabel, result.state, repository.save(result),
                       {},           result.root};
        if (!item.persistence.saved())
            item.unsavedResult = std::make_shared<const RunResult>(std::move(result));
        batch.items[finished.index] = item;
        if (completed)
        {
            try
            {
                completed(item);
            }
            catch (const std::exception &e)
            {
                batch.observerWarnings.push_back(QString::fromUtf8(e.what()));
            }
            catch (...)
            {
                batch.observerWarnings.push_back("Completion observer threw an exception");
            }
        }
    }
    batch.items.resize(next);
    if (cancellation.isCancelled()) batch.notStarted = plan.requests.size() - next;
    for (const auto &joint : plan.batches)
    {
        if (cancellation.isCancelled()) break;
        auto result = runJointBatch(joint, databasePath, concurrency, serialized, completed, cancellation);
        batch.items += result.items;
        batch.observerWarnings += result.observerWarnings;
        if (!result.error.isEmpty()) { batch.error = result.error; break; }
    }
    return batch;
}
} // namespace aster
