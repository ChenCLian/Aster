// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CORE_WORKFLOW_MODEL_H
#define ASTER_CORE_WORKFLOW_MODEL_H

#include "core/task_registry.h"

namespace aster {
ValidationResult validateWorkflow(const WorkflowDefinition& definition,
                                  const TaskRegistry& registry, bool requireNonEmpty = false);

class RunSnapshot {
public:
    const WorkflowDefinition& definition() const { return m_definition; }
    const TaskRegistry& registry() const { return m_registry; }
private:
    friend class WorkflowModel;
    RunSnapshot(WorkflowDefinition definition, TaskRegistry registry)
        : m_definition(std::move(definition)), m_registry(std::move(registry)) {}
    WorkflowDefinition m_definition;
    TaskRegistry m_registry;
};
struct SnapshotResult {
    std::shared_ptr<const RunSnapshot> snapshot;
    ValidationResult validation;
};
struct EditResult {
    QString error;
    NodeId nodeId;
    qsizetype affectedNodes = 0;
    bool ok() const { return error.isEmpty(); }
};
struct ChildPreview {
    QString error;
    GridPosition position;
    QString output;
    bool ok() const { return error.isEmpty(); }
};

// Mutable editor model. Not a QObject and not shared with running workers.
// All edits are checked before commit; rejected edits have no side effects.
class WorkflowModel {
public:
    explicit WorkflowModel(TaskRegistry registry) : m_registry(std::move(registry)) {}
    const WorkflowDefinition& definition() const { return m_definition; }
    const TaskRegistry& registry() const { return m_registry; }
    EditResult addRoot(const QString& method, GridPosition position, QJsonObject parameters = {});
    EditResult addChild(const NodeId& parent, ChildSlot slot, const QString& method,
                        QJsonObject parameters = {}, QString output = {});
    // Used for click/drag target highlighting; never changes the draft/history.
    ChildPreview previewChild(const NodeId& parent, ChildSlot slot, const QString& method,
                              QJsonObject parameters = {}, QString output = {}) const;
    EditResult setParameters(const NodeId& node, const QJsonObject& parameters);
    QVector<NodeId> removalPreview(const NodeId& root) const;
    EditResult removeSubtree(const NodeId& root);
    EditResult replaceDefinition(WorkflowDefinition definition);
    EditResult undo();
    EditResult redo();
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    SnapshotResult freeze() const;
private:
    EditResult commit(WorkflowDefinition candidate, NodeId affected = {}, qsizetype count = 0);
    TaskRegistry m_registry;
    WorkflowDefinition m_definition;
    QVector<WorkflowDefinition> m_undo, m_redo;
    static constexpr qsizetype HistoryLimit = 100;
};
} // namespace aster
#endif
