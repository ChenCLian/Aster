// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_CORE_WORKFLOW_EDITOR_H
#define ASTER_CORE_WORKFLOW_EDITOR_H
#include "core/batch_workflow.h"

namespace aster {
// One draft, one history entry. Numerical buffers remain immutable DataRefs.
struct WorkflowEditState {
    WorkflowDefinition definition;
    SourceSelection sources;
    BatchGrouping grouping;
};
struct WorkflowEditDraft {
    WorkflowEditState state;
private:
    friend class WorkflowEditor;
    QUuid owner;
    quint64 revision = 0;
};
class PreparedWorkflowEdit {
public:
    const WorkflowEditState& state() const { return m_state; }
private:
    friend class WorkflowEditor;
    WorkflowEditState m_state;
    QUuid m_owner;
    quint64 m_revision = 0;
};
struct PreparedEditResult {
    std::shared_ptr<const PreparedWorkflowEdit> edit;
    ValidationResult validation;
};
struct WorkflowEditResult {
    QString error;
    bool graphChanged = false, sourcesChanged = false, groupingChanged = false;
    bool ok() const { return error.isEmpty(); }
    bool changed() const { return graphChanged || sourcesChanged || groupingChanged; }
};

// GUI-thread editor, separate from frozen execution plans. Preparation never
// publishes data. A token is valid only for its originating editor/revision.
// Empty sources/groups are valid drafts; execution readiness is checked by
// prepareRuns/prepareBatch. Removing data preserves unresolved pair references.
class WorkflowEditor {
public:
    explicit WorkflowEditor(TaskRegistry registry) : m_model(std::move(registry), false) {}
    WorkflowEditor(const WorkflowEditor&) = delete;
    WorkflowEditor& operator=(const WorkflowEditor&) = delete;
    const WorkflowModel& model() const { return m_model; }
    const WorkflowEditState& state() const { return m_state; }
    quint64 revision() const { return m_revision; }
    WorkflowEditDraft beginEdit() const;
    PreparedEditResult prepare(WorkflowEditDraft draft) const;
    WorkflowEditResult commit(const std::shared_ptr<const PreparedWorkflowEdit>& edit);
    WorkflowEditResult apply(WorkflowEditDraft draft);
    WorkflowEditResult undo();
    WorkflowEditResult redo();
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    void setLocked(bool locked);
    bool locked() const { return m_locked; }
    static constexpr qsizetype HistoryLimit = 100;
private:
    ValidationResult validateBindings(const WorkflowEditState& candidate) const;
    WorkflowEditResult publish(WorkflowEditState state);
    WorkflowModel m_model;
    WorkflowEditState m_state;
    QVector<WorkflowEditState> m_undo, m_redo;
    QUuid m_identity = QUuid::createUuid();
    quint64 m_revision = 0;
    bool m_locked = false;
};
} // namespace aster
#endif
