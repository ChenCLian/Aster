#include "application/batch_runner.h"
#include <algorithm>
#include <deque>
#include <future>
#include <mutex>
#include <condition_variable>
#include <map>

namespace aster
{
BatchResult BatchRunner::run(const RunPlan &plan, const QString &databasePath, int concurrency,
                             EventSink nodeEvents, CompletionSink completed,
                             CancellationToken cancellation) const
{
    BatchResult batch;
    if (!plan.validation.ok() || plan.requests.isEmpty() || concurrency < minimumConcurrency || concurrency > maximumConcurrency)
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
    std::deque<qsizetype> ready;
    std::map<qsizetype, std::future<RunResult>> active;
    std::map<qsizetype, BatchItem> collected;
    qsizetype next = 0;
    auto launch = [&]
    {
        const auto index = next;
        const auto request = plan.requests[index];
        active.emplace(
            index, std::async(std::launch::async,
                              [&, index, request, serialized, cancellation]
                              {
                                  const auto notify = [&]
                                  {
                                      {
                                          std::lock_guard<std::mutex> lock(completionMutex);
                                          ready.push_back(index);
                                      }
                                      completionChanged.notify_one();
                                  };
                                  try
                                  {
                                      auto result =
                                          WorkflowExecutor().run(request, serialized, cancellation);
                                      notify();
                                      return result;
                                  }
                                  catch (...)
                                  {
                                      notify();
                                      throw;
                                  }
                              }));
        ++next;
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
    while (!launchFailed && next < plan.requests.size() &&
           active.size() < static_cast<size_t>(concurrency))
        tryLaunch();
    // Even if launching another worker fails, drain and save all work already
    // accepted. The remaining requests were never started; batch.error reports it.
    while (!active.empty())
    {
        qsizetype index;
        {
            std::unique_lock<std::mutex> lock(completionMutex);
            completionChanged.wait(lock, [&] { return !ready.empty(); });
            index = ready.front();
            ready.pop_front();
        }
        RunResult result;
        try
        {
            result = active.at(index).get();
        }
        catch (const std::exception &e)
        {
            result.runId = QUuid::createUuid();
            result.root = plan.requests[index].root;
            result.sourceLabel = plan.requests[index].source.label;
            result.error = QString::fromUtf8(e.what());
        }
        catch (...)
        {
            result.runId = QUuid::createUuid();
            result.root = plan.requests[index].root;
            result.sourceLabel = plan.requests[index].source.label;
            result.error = "Unexpected worker failure";
        }
        active.erase(index);
        // Refill the free compute slot before serial persistence/observers.
        if (!launchFailed && next < plan.requests.size())
            tryLaunch();
        BatchItem item{result.runId, result.sourceLabel, result.state, repository.save(result),
                       {},           result.root};
        if (!item.persistence.saved())
            item.unsavedResult = std::make_shared<const RunResult>(std::move(result));
        collected.emplace(index, item);
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
    for (const auto &entry : collected)
        batch.items.push_back(entry.second);
    return batch;
}
} // namespace aster
