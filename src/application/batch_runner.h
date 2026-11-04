#ifndef ASTER_APPLICATION_BATCH_RUNNER_H
#define ASTER_APPLICATION_BATCH_RUNNER_H
#include "io/database/result_repository.h"
#include "application/execution_defaults.h"

namespace aster
{
struct BatchItem
{
    RunId runId;
    QString sourceLabel;
    RunState computation = RunState::Invalid;
    SaveReceipt persistence;
    // Retained only on write failure, so the application can retry without recomputing.
    std::shared_ptr<const RunResult> unsavedResult;
    NodeId root;
};
struct BatchResult
{
    QVector<BatchItem> items;
    QString error;
    QStringList observerWarnings;
};
using CompletionSink = std::function<void(const BatchItem &)>;

class BatchRunner
{
  public:
    // Blocking application service. Call off the GUI thread. Calculations run
    // in bounded workers; one caller-thread repository serializes commits.
    // Node events are serialized but originate on worker threads; completion
    // events originate on the caller thread, always after the save attempt.
    BatchResult run(const RunPlan &plan, const QString &databasePath, int concurrency = defaultConcurrency(),
                    EventSink nodeEvents = {}, CompletionSink completed = {},
                    CancellationToken cancellation = {}) const;
};
} // namespace aster
#endif
