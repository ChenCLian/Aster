// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CORE_WORKFLOW_EXECUTOR_H
#define ASTER_CORE_WORKFLOW_EXECUTOR_H

#include "core/workflow_model.h"
#include <QDateTime>

namespace aster {
struct SourceItem {
    QString label;
    DataRef data;
    QJsonObject provenance; // File/column selection or other application metadata.
};
struct RunRequest {
    std::shared_ptr<const RunSnapshot> workflow;
    NodeId root;
    SourceItem source;
};
class BatchSnapshot;
struct RunPlan {
    QVector<RunRequest> requests;
    ValidationResult validation;
    QVector<std::shared_ptr<const BatchSnapshot>> batches{};
};
using SourceSelection = QMap<NodeId, QVector<SourceItem>>;
// Expands each bound input root once per selected dataset. Unbound generators
// run once each, independent of the number of selected files at other roots.
RunPlan prepareRuns(std::shared_ptr<const RunSnapshot> snapshot, const SourceSelection& selection);

enum class RunState { Succeeded, PartialFailed, Failed, Cancelled, Invalid };
QString runStateName(RunState state);
struct RunResult {
    RunId runId;
    NodeId root;
    RunState state = RunState::Invalid;
    QDateTime startedAt, finishedAt;
    QString sourceLabel;
    QJsonObject provenance;
    QJsonObject workflowSnapshot; // Only this root's executed subtree and parameters.
    QMap<NodeId, NodeResult> nodes;
    QVector<NodeId> executionOrder; // Actual execute() calls, excludes blocked/cancelled-before-start.
    QString error;
    QStringList observerWarnings;
};

class WorkflowExecutor {
public:
    // Blocking, reentrant and stateless. The application chooses worker threads.
    // One request = one root + one concrete input = one future result record.
    RunResult run(const RunRequest& request, EventSink sink = {}, CancellationToken cancellation = {}) const;
};
} // namespace aster
#endif
