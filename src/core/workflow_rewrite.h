// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CORE_WORKFLOW_REWRITE_H
#define ASTER_CORE_WORKFLOW_REWRITE_H
#include "core/workflow_editor.h"

namespace aster {
enum class NodeRewriteKind { InsertBefore, Replace };
struct NodeOutputMapping {
    NodeId child;
    QString previousOutput;
    QVector<OutputSpec> choices;
};
struct NodeRewriteOptions {
    QString error;
    NodeId parent;
    QString previousInput;
    QVector<OutputSpec> inputs;
    QVector<NodeOutputMapping> outputs;
    bool ok() const { return error.isEmpty(); }
};
struct NodeRewriteRequest {
    NodeRewriteKind kind = NodeRewriteKind::InsertBefore;
    NodeId target;
    QString method, inputOutput;
    QMap<NodeId, QString> childOutputs;
    QJsonObject parameters;
};
struct PreparedNodeRewrite {
    QString error;
    std::shared_ptr<const PreparedWorkflowEdit> edit;
    NodeId nodeId;
    qsizetype shiftedNodes = 0;
    bool ok() const { return error.isEmpty() && bool(edit); }
};
// This phase edits ordinary nodes/edges only. Edges touching a paired node
// remain explicit exclusions until joint-connection rewriting is supported.
QString nodeRewriteAvailability(const WorkflowModel& model, NodeRewriteKind kind, NodeId target);
NodeRewriteOptions inspectNodeRewrite(const WorkflowModel& model, NodeRewriteKind kind,
                                     NodeId target, const QString& method);
PreparedNodeRewrite prepareNodeRewrite(const WorkflowEditor& editor, const NodeRewriteRequest& request);
} // namespace aster
#endif
