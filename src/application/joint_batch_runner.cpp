// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "application/joint_batch_runner.h"
#include "model/workflow_codec.h"
#include <QJsonArray>
#include <QSet>
#include <type_traits>
namespace aster
{
BatchResult runJointBatch(std::shared_ptr<const BatchSnapshot> plan, const QString &path,
                          int concurrency, EventSink events, CompletionSink completed,
                          CancellationToken cancel, quint64 retainedResultBudget)
{
    BatchResult batch;
    if (!plan)
    {
        batch.error = "Missing joint batch plan";
        return batch;
    }
    ResultRepository repository;
    batch.error = repository.open(path);
    if (!batch.error.isEmpty())
        return batch;
    QMap<QUuid, RunResult> pending;
    QMap<QUuid, int> remaining;
    QMap<QUuid, WorkflowDefinition> definitions;
    QMap<QUuid, RunId> runIds;
    QMap<NodeId, NodeDefinition> nodes;
    QVector<QUuid> order;
    for (const auto &node : plan->workflow().definition().nodes)
        nodes.insert(node.id, node);
    const auto started = QDateTime::currentDateTimeUtc();
    // One record per imported dataset or explicit pair. Pair records contain
    // their executed suffix; the full graph and ordered source links are provenance.
    for (const auto &job : plan->jobs())
    {
        if (!pending.contains(job.item))
        {
            RunResult run;
            run.runId = QUuid::createUuid();
            run.root = job.node;
            run.sourceLabel = job.label;
            run.startedAt = started;
            run.provenance = job.provenance;
            pending[job.item] = run;
            runIds[job.item] = run.runId;
            order << job.item;
        }
        auto node = nodes.value(job.node);
        if (node.id == pending[job.item].root)
            node.input.reset();
        definitions[job.item].nodes << node;
        definitions[job.item].layout[node.id] = plan->workflow().definition().layout.value(node.id);
        ++remaining[job.item];
    }
    for (const auto &job : plan->jobs())
    {
        auto &run = pending[job.item];
        if (job.node != run.root)
            continue;
        run.workflowSnapshot = workflowToJson(definitions[job.item]);
        if (job.inputs.size() == 2)
        {
            QJsonArray links;
            for (const auto &dep : job.inputs)
            {
                const auto &producer = plan->jobs()[dep.producer];
                links.append(
                    QJsonObject{{"run_uuid", runIds[producer.item].toString(QUuid::WithoutBraces)},
                                {"node_id", producer.node.toString(QUuid::WithoutBraces)},
                                {"output", dep.port}});
            }
            run.provenance["input_records"] = links;
            run.provenance["batch_execution"] = plan->manifest(job.item);
        }
    }
    QMap<const DataObject *, QPair<int, quint64>> retained;
    quint64 retainedBytes = 0;
    const auto bytes = [](const DataRef &ref) -> quint64
    {
        return std::visit(
            [](const auto &v) -> quint64
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, SeriesData>)
                    return quint64(v.x.size() + v.y.size()) * sizeof(double);
                else if constexpr (std::is_same_v<T, SpectrumData>)
                    return quint64(v.frequency.size() + v.power.size()) * sizeof(double);
                else if constexpr (std::is_same_v<T, GridData>)
                    return quint64(v.x.size() + v.y.size() + v.values.size()) * sizeof(double);
                else
                {
                    quint64 n = 0;
                    for (const auto &row : v.rows)
                        n += quint64(row.size()) * sizeof(double);
                    for (const auto &row : v.defined)
                        n += quint64(row.size());
                    return n;
                }
            },
            *ref);
    };
    QString retentionError, persistenceError;
    QMap<QUuid, BatchItem> saved;
    BatchObserver observer;
    observer.event = [&](const BatchEvent &event)
    {
        if (!events)
            return;
        const auto &job = plan->jobs()[event.job];
        events({runIds[job.item],
                job.node,
                event.state,
                event.message,
                job.sourceRoot.isNull() ? plan->root() : job.sourceRoot,
                job.label,
                {},
                event.progress});
    };
    observer.finished = [&](const BatchJob &job, const NodeResult &result, bool executed)
    {
        auto &run = pending[job.item];
        quint64 added = 0;
        QSet<const DataObject *> unique;
        for (const auto &ref : result.outputs)
            if (ref && !unique.contains(ref.get()))
            {
                unique.insert(ref.get());
                if (!retained.contains(ref.get()))
                    added += bytes(ref);
            }
        auto stored = result;
        if (added > retainedResultBudget - retainedBytes)
        {
            retentionError = QString("Pending result payload exceeds the retention budget "
                                     "(%1 bytes); reduce input size or branches")
                                 .arg(retainedResultBudget);
            stored = NodeResult::failure(retentionError);
            cancel.cancel();
        }
        for (const auto &ref : stored.outputs)
            if (ref)
            {
                auto &entry = retained[ref.get()];
                if (entry.first++ == 0)
                {
                    entry.second = bytes(ref);
                    retainedBytes += entry.second;
                }
            }
        run.nodes[job.node] = stored;
        if (executed)
            run.executionOrder << job.node;
        if (--remaining[job.item])
            return;
        bool failed = false, success = false, cancelled = false;
        for (const auto &node : run.nodes)
        {
            success |= node.state == NodeState::Succeeded;
            failed |= node.state == NodeState::Failed || node.state == NodeState::Blocked;
            cancelled |= node.state == NodeState::Cancelled;
            if (run.error.isEmpty() && !node.error.isEmpty())
                run.error = node.error;
        }
        run.state = cancelled ? RunState::Cancelled
                    : failed  ? (success ? RunState::PartialFailed : RunState::Failed)
                              : RunState::Succeeded;
        run.finishedAt = QDateTime::currentDateTimeUtc();
        BatchItem item{run.runId, run.sourceLabel, run.state, repository.save(run),
                       {},        job.sourceRoot.isNull() ? plan->root() : job.sourceRoot};
        if (!item.persistence.saved())
        {
            item.unsavedResult = std::make_shared<const RunResult>(run);
            persistenceError = "Joint batch stopped after a result could not be saved: " +
                               item.persistence.error;
            // Keep retryable results without accumulating an unbounded failed-save queue.
            cancel.cancel();
        }
        saved[job.item] = item;
        for (const auto &node : run.nodes)
            for (const auto &ref : node.outputs)
                if (ref)
                {
                    auto it = retained.find(ref.get());
                    if (--it->first == 0)
                    {
                        retainedBytes -= it->second;
                        retained.erase(it);
                    }
                }
        pending.remove(job.item); // Release completed numerical outputs immediately after saving.
        if (completed)
            completed(item);
    };
    auto result = BatchWorkflowExecutor().run(plan, {concurrency}, observer, cancel);
    batch.error = !retentionError.isEmpty()   ? retentionError
                  : !persistenceError.isEmpty() ? persistenceError
                                                : result.error;
    batch.observerWarnings = result.observerWarnings;
    for (const auto &id : order)
        if (saved.contains(id))
            batch.items << saved[id];
    if (!pending.isEmpty() && batch.error.isEmpty())
        batch.error = "Joint batch persistence did not finish";
    return batch;
}
} // namespace aster
