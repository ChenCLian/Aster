#ifndef ASTER_UI_WORKFLOW_DOCUMENT_H
#define ASTER_UI_WORKFLOW_DOCUMENT_H
#include "core/workflow_executor.h"
#include "io/database/output_reference.h"
#include <QObject>
namespace aster::ui
{
// GUI-thread draft adapter. WorkflowModel is the sole owner of graph rules.
class WorkflowDocument : public QObject
{
    Q_OBJECT
  public:
    explicit WorkflowDocument(TaskRegistry registry, QObject *parent = nullptr);
    const WorkflowModel &model() const
    {
        return m_model;
    }
    const SourceSelection &sources() const
    {
        return m_sources;
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
    // Shared entry point for future file/database/memory source pickers.
    QString setSources(const NodeId &root, QVector<SourceItem> items);
    QJsonObject fileOptions(const NodeId &root, const QString &path) const;
    bool setFileOptions(const NodeId &root, const QStringList &paths, const QJsonObject &options);
    QStringList files(const NodeId &root) const;
    RunPlan plan() const;
    EditResult addDatabaseSource(const OutputReference &reference);
    EditResult reset();
    EditResult replaceDefinition(const WorkflowDefinition &definition);
  signals:
    void changed();
    void sourcesChanged();

  private:
    WorkflowModel m_model;
    // Bindings stay available when undo restores a removed root (keyed by ID).
    SourceSelection m_sources;
};
} // namespace aster::ui
#endif
