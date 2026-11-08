// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CORE_BATCH_WORKFLOW_H
#define ASTER_CORE_BATCH_WORKFLOW_H

#include "core/workflow_executor.h"

namespace aster {

// Identity belongs to an imported dataset, not its name, array position,
// content hash or completion order. Preserve id when editing import options.
struct BatchDataset {
    QUuid id;
    SourceItem source;
};
struct BatchSeriesRef {
    QUuid dataset;
    QString port;
    NodeId node{}; // Empty: the joint node's tree parent (legacy same-batch binding).
    bool operator==(const BatchSeriesRef& other) const
    { return dataset == other.dataset && port == other.port && node == other.node; }
};
struct BatchPair {
    QUuid id;
    QString label;
    BatchSeriesRef a, b;
};
using BatchGrouping = QMap<NodeId, QVector<BatchPair>>;
struct BatchGroupingReadResult {
    BatchGrouping grouping;
    ValidationResult validation;
};
QJsonObject batchGroupingToJson(const BatchGrouping& grouping);
BatchGroupingReadResult batchGroupingFromJson(const QJsonObject& json);

// Compiled dependencies name the actual producing job, including explicit joint references.
// Pair order is explicit: inputs[0] is A; inputs[1] is B.
struct BatchDependency {
    qsizetype producer = -1;
    QString port;
};
struct BatchJob {
    qsizetype index = -1;
    NodeId node;
    QUuid item; // dataset id before a pair node; group id after it
    QString label;
    QVector<BatchDependency> inputs;
    SourceItem source; // used only by a source job
    QJsonObject provenance;
    NodeId sourceRoot{}; // Owning tree for status and persistence; empty in legacy plans.
};

class BatchSnapshot;
struct BatchPlanResult {
    std::shared_ptr<const BatchSnapshot> snapshot;
    ValidationResult validation;
};
constexpr qsizetype BatchDatasetLimit = 10000;
constexpr qsizetype BatchPairLimit = 10000;
constexpr qsizetype BatchJobLimit = 100000;

// Compiles one source root and its subtree. Other roots are never implicit
// candidates. Each paired node needs explicit groups; unused data is not an
// error. Reusing a reference in several groups is supported. Pair-after-pair
// grouping is deliberately outside the first same-source-batch contract.
BatchPlanResult prepareBatch(std::shared_ptr<const RunSnapshot> workflow,
                             NodeId root, const QVector<BatchDataset>& datasets,
                             const BatchGrouping& grouping);

// Optional references are confined to paired tasks. Ordinary nodes retain their
// single tree parent. Pair-produced streams cannot be joint inputs yet.
using BatchSources = QMap<NodeId, QVector<BatchDataset>>;
bool hasLinkedInputs(const BatchGrouping& grouping);
QVector<NodeId> linkedInputOrigins(const WorkflowModel& model, NodeId joint);
BatchPlanResult prepareLinkedBatch(std::shared_ptr<const RunSnapshot> workflow,
                                  const BatchSources& sources, const BatchGrouping& grouping);

BatchPlanResult batchDependencySlice(std::shared_ptr<const BatchSnapshot> plan, NodeId target, QUuid item);

class BatchSnapshot {
public:
    const QVector<BatchJob>& jobs() const { return m_jobs; }
    const RunSnapshot& workflow() const { return *m_workflow; }
    NodeId root() const { return m_root; }
    bool linkedInputs() const { return m_linked; }
    const BatchGrouping& grouping() const { return m_grouping; }
    const QMap<NodeId, QVector<BatchSeriesRef>>& unassigned() const { return m_unassigned; }
    // Small JSON locator/parameter snapshot; numerical arrays remain DataRef.
    // With an item, retain only its jobs and transitive input dependencies.
    QJsonObject manifest(QUuid item = {}) const;
private:
    BatchSnapshot() = default;
    friend BatchPlanResult prepareBatch(std::shared_ptr<const RunSnapshot>, NodeId,
                                       const QVector<BatchDataset>&, const BatchGrouping&);
    friend BatchPlanResult prepareLinkedBatch(std::shared_ptr<const RunSnapshot>,
                                               const BatchSources&, const BatchGrouping&);
    friend BatchPlanResult batchDependencySlice(std::shared_ptr<const BatchSnapshot>, NodeId, QUuid);
    friend class BatchWorkflowExecutor;
    std::shared_ptr<const RunSnapshot> m_workflow;
    NodeId m_root;
    bool m_linked = false;
    QVector<NodeId> m_roots;
    QMap<QUuid, NodeId> m_datasetRoots;
    QVector<BatchDataset> m_datasets;
    BatchGrouping m_grouping;
    QVector<BatchJob> m_jobs;
    QMap<QUuid, QVector<qsizetype>> m_itemJobs;
    QMap<QUuid, qsizetype> m_datasetIndices;
    QMap<NodeId, QVector<BatchSeriesRef>> m_unassigned;
};

struct BatchJobSummary {
    NodeState state = NodeState::Pending;
    bool executed = false;
    QJsonObject report;
    QString error;
    QStringList warnings;
};
struct BatchExecutionResult {
    RunId batchId;
    RunState state = RunState::Invalid;
    QVector<BatchJobSummary> jobs; // plan order, irrespective of completion order
    QString error;
    QStringList observerWarnings;
    quint64 peakCachedPayloadBytes = 0;
};
struct BatchExecutionOptions {
    int concurrency = 1;
    // Numeric payload retained by the coordinator, counted once per DataObject.
    // This is not a process-RAM estimate: source buffers, task workspaces and
    // arrays retained by an observer are outside this cache budget.
    quint64 maxCachedPayloadBytes = 256ull * 1024 * 1024;
};
struct BatchEvent {
    RunId batchId;
    qsizetype job = -1;
    NodeId node;
    QUuid item;
    NodeState state = NodeState::Pending;
    int progress = -1;
    QString message;
};
struct BatchObserver {
    // Serialized; progress can originate on a worker. GUI code must queue it.
    std::function<void(const BatchEvent&)> event;
    // Called on the coordinator thread before outputs are released. Persist or
    // explicitly retain required outputs here. Exceptions become observerWarnings.
    std::function<void(const BatchJob&, const NodeResult&)> completed;
    // Persistence adapters need the actual execute() flag, including cancellation.
    std::function<void(const BatchJob&, const NodeResult&, bool executed)> finished;
};

class BatchWorkflowExecutor {
public:
    // Blocking service: run off the GUI thread. Workers never wait for other
    // workers' inputs. The coordinator schedules only ready jobs and releases
    // producer payloads after their last consumer completes.
    BatchExecutionResult run(std::shared_ptr<const BatchSnapshot> plan,
                             BatchExecutionOptions options = {},
                             BatchObserver observer = {},
                             CancellationToken cancellation = {}) const;
};

} // namespace aster
#endif
