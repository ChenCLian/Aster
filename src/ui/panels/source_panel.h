#ifndef ASTER_UI_SOURCE_PANEL_H
#define ASTER_UI_SOURCE_PANEL_H
#include "ui/workflow/workflow_document.h"
#include <QWidget>
class QComboBox;
class QListWidget;
class QLabel;
namespace aster::ui
{
class FileOptionsEditor;
class SourcePanel : public QWidget
{
    Q_OBJECT
  public:
    explicit SourcePanel(WorkflowDocument *document, QWidget *parent = nullptr);
    void refresh();
    void selectRoot(NodeId root);
    NodeId root() const;
    void addFiles(const QStringList &files);
    void setFileState(NodeId root, const QString &path, const QString &state);
    void clearFileStates();
    bool applyCurrentOptions();
    bool readyToRun() const;
  signals:
    void importRequested();
    void sourceRequested();
    void readinessChanged(bool ready);
    void previewInputChanged(QString path, QJsonObject options, bool ready);
    void previewSourceChanged(NodeId root, SourceItem source, bool ready);

  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    void showFileOptions();
    void notifyPreviewSource();
    FileOptionsEditor *m_options;
    QHash<QString, QString> m_states;
    WorkflowDocument *m_document;
    QComboBox *m_roots;
    QListWidget *m_files;
    QLabel *m_count;
};
} // namespace aster::ui
#endif
