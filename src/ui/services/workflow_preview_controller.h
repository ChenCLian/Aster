#ifndef ASTER_UI_WORKFLOW_PREVIEW_CONTROLLER_H
#define ASTER_UI_WORKFLOW_PREVIEW_CONTROLLER_H
#include "ui/workflow/workflow_document.h"
#include <QTimer>
class QThread;
namespace aster::ui
{
// A validated ancestor path, using the same IDs, ports and factories as a real run.
RunPlan prepareNodePreview(const WorkflowModel &model, NodeId target, NodeId sourceRoot,
                           const SourceItem &source);
struct WorkflowPreviewResult
{
    NodeResult output, original;
    NodeId failedNode;
    QVector<NodeId> executed;
};
class WorkflowPreviewController : public QObject
{
    Q_OBJECT
  public:
    enum class State { Idle, Waiting, Running, Ready, Failed, Paused };
    explicit WorkflowPreviewController(WorkflowDocument *document, QObject *parent = nullptr);
    ~WorkflowPreviewController() override;
    void setSource(NodeId root, SourceItem source, bool ready);
    void selectNode(NodeId node); // Null/foreign UI clicks do not replace a valid selection.
    void setActive(bool active);
    void setEditing(bool editing);
    void cancel();
    void refresh();
    void retry() { m_key.clear(); refresh(); }
    State state() const { return m_state; }
    NodeId node() const { return m_node; }
    QString sourceLabel() const { return m_source.label; }
    bool busy() const { return m_worker != nullptr; }
    std::shared_ptr<const WorkflowPreviewResult> result() const { return m_result; }
    QString message() const { return m_message; }
  signals:
    void changed();
    void started();
    void busyChanged(bool busy);
  private:
    void start();
    WorkflowDocument *m_document;
    NodeId m_node, m_sourceRoot;
    SourceItem m_source;
    bool m_ready = false, m_active = false, m_editing = false, m_due = false;
    State m_state = State::Idle;
    QByteArray m_key;
    QString m_message;
    RunRequest m_request;
    quint64 m_generation = 0;
    CancellationToken m_cancel;
    QTimer m_timer;
    QThread *m_worker = nullptr;
    std::shared_ptr<const WorkflowPreviewResult> m_result;
};
}
#endif
