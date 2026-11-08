// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_WORKFLOW_DOCUMENT_H
#define ASTER_UI_WORKFLOW_DOCUMENT_H
#include "core/workflow_rewrite.h"
#include "io/database/output_reference.h"
#include <QObject>
namespace aster::ui
{
// GUI-thread adapter. All edits publish through one validated transaction.
class WorkflowDocument : public QObject
{
    Q_OBJECT
  public:
    explicit WorkflowDocument(TaskRegistry registry, QObject *parent = nullptr);
    const WorkflowModel &model() const
    {
        return m_editor.model();
    }
    const SourceSelection &sources() const
    {
        return m_editor.state().sources;
    }
    const NodeDefinition *node(const NodeId &id) const;
    EditResult addRoot(const QString &method, QJsonObject params = {});
    EditResult addChild(const NodeId &parent, ChildSlot slot, const QString &method,
                        QJsonObject params = {}, QString output = {});
    EditResult setParameters(const NodeId &id, const QJsonObject &parameters);
    EditResult removeSubtree(const NodeId &id);
    EditResult undo();
    EditResult redo();
    void setFiles(const NodeId &root, const QStringList &paths);
    EditResult importFiles(NodeId root, const QStringList &paths);
    // Shared entry point for future file/database/memory source pickers.
    QString setSources(const NodeId &root, QVector<SourceItem> items);
    QJsonObject fileOptions(const NodeId &root, const QString &path) const;
    bool setFileOptions(const NodeId &root, const QStringList &paths, const QJsonObject &options);
    QStringList files(const NodeId &root) const;
    RunPlan plan() const;
    QVector<BatchDataset> datasets(NodeId node) const;
    BatchGrouping grouping() const { return m_editor.state().grouping; }
    QString setPairs(NodeId node, QVector<BatchPair> pairs);
    BatchPlanResult pairPreview(NodeId node, QUuid pair) const;
    NodeId rootFor(NodeId node) const;
    NodeId pairNodeFor(NodeId node) const;
    EditResult addDatabaseSource(const OutputReference &reference);
    EditResult reset();
    EditResult replaceDefinition(const WorkflowDefinition &definition);
    bool canUndo() const { return m_editor.canUndo(); }
    bool canRedo() const { return m_editor.canRedo(); }
    quint64 revision() const { return m_editor.revision(); }
    void setEditingLocked(bool locked) { m_editor.setLocked(locked); }
    bool editingLocked() const { return m_editor.locked(); }
    WorkflowEditDraft beginEdit() const { return m_editor.beginEdit(); }
    PreparedEditResult prepareEdit(WorkflowEditDraft draft) const
    { return m_editor.prepare(std::move(draft)); }
    WorkflowEditResult commitEdit(const std::shared_ptr<const PreparedWorkflowEdit> &edit);
    PreparedNodeRewrite prepareRewrite(const NodeRewriteRequest &request) const
    { return prepareNodeRewrite(m_editor, request); }
  signals:
    void changed();
    void sourcesChanged();

  private:
    WorkflowEditor m_editor;
    EditResult editGraph(const std::function<EditResult(WorkflowModel&)> &operation);
    WorkflowEditResult applyEdit(WorkflowEditDraft draft);
    void notifyEdit(const WorkflowEditResult &result);
};
} // namespace aster::ui
#endif
