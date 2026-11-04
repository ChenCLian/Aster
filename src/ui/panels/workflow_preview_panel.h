#ifndef ASTER_UI_WORKFLOW_PREVIEW_PANEL_H
#define ASTER_UI_WORKFLOW_PREVIEW_PANEL_H
#include "ui/services/workflow_preview_controller.h"
#include <QWidget>
class QLabel; class QComboBox; class QProgressBar; class QToolButton;
class QShowEvent; class QHideEvent;
namespace aster::ui
{
class FilePreviewPanel; class PlotView; class StyleNote;
class WorkflowPreviewPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit WorkflowPreviewPanel(WorkflowDocument *document, QWidget *parent = nullptr);
    WorkflowPreviewController *controller() const { return m_controller; }
    void setFile(const QString &path, const QJsonObject &options, bool ready);
    void setSuspended(bool suspended);
  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
  private:
    void updateState();
    void showOutput();
    WorkflowDocument *m_document;
    WorkflowPreviewController *m_controller;
    FilePreviewPanel *m_original;
    PlotView *m_generated, *m_resultPlot;
    QLabel *m_method, *m_file, *m_sourceHeading;
    StyleNote *m_status, *m_outputHint;
    QComboBox *m_outputs;
    QProgressBar *m_progress;
    QToolButton *m_fit, *m_legend, *m_retry;
    bool m_suspended = false;
    std::shared_ptr<const WorkflowPreviewResult> m_displayed;
};
}
#endif
