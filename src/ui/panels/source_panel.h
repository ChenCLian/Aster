// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_SOURCE_PANEL_H
#define ASTER_UI_SOURCE_PANEL_H
#include "ui/workflow/workflow_document.h"
#include <QWidget>
#include <functional>
#include <QQueue>
class QComboBox;
class QListWidget;
class QLabel;
class QThread;
class QToolButton;
namespace aster::ui
{
class FileOptionsEditor;
class FileListItem;
enum class SourceFileState { Pending, Running, Saved, Unsaved, Cancelled, Failed };
class SourcePanel : public QWidget
{
    Q_OBJECT
  public:
    explicit SourcePanel(WorkflowDocument *document, QWidget *parent = nullptr);
    ~SourcePanel() override;
    void refresh();
    void selectRoot(NodeId root);
    void selectDataset(NodeId root, QUuid dataset);
    void moveSelectedDatasets();
    void setBeforeInputRemoval(std::function<bool()> guard) { m_beforeInputRemoval = std::move(guard); }
    NodeId root() const;
    SourceItem currentSource() const;
    void addFiles(const QStringList &files);
    void setFileState(NodeId root, const QString &path, SourceFileState state);
    void clearFileStates();
    bool applyCurrentOptions();
    bool readyToRun() const;
    void syncPreviewInput();
    void updateRunControls();
    void showFileOptions();
  signals:
    void batchOptionsFinished(QStringList applied, QStringList skipped);
    void importRequested();
    void moveInputsRequested();
    void inputsMoved(NodeId destinationRoot);
    void editRejected(QString error);
    void sourceRequested();
    void readinessChanged(bool ready);
    void previewInputChanged(QString path, QJsonObject options, bool ready);
    void previewSourceChanged(NodeId root, SourceItem source, bool ready);

  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    std::function<bool()> m_beforeInputRemoval;
    QToolButton *m_optionsToggle;
    struct OptionsEdit {
        NodeId root;
        QStringList paths;
        QMap<QString, QJsonObject> before;
        QMap<QString, QUuid> identities;
        QJsonObject options;
    };
    void captureOptionsSelection();
    void startOptionsEdit();
    void updateSelectionHint();
    OptionsEdit m_optionsEdit;
    QQueue<OptionsEdit> m_optionsQueue;
    NodeId m_optionsRoot;
    bool m_committingOptions = false;
    QThread *m_optionsWorker = nullptr;
    CancellationToken m_optionsCancel;
    void notifyPreviewSource();
    FileOptionsEditor *m_options;
    QHash<QPair<NodeId, QUuid>, SourceFileState> m_states;
    QHash<NodeId, QMultiHash<QString, QUuid>> m_statusDatasets;
    QMultiHash<QString, FileListItem *> m_visibleFiles;
    WorkflowDocument *m_document;
    QComboBox *m_roots;
    QListWidget *m_files;
    QLabel *m_count;
    QLabel *m_selectionHint;
};
} // namespace aster::ui
#endif
