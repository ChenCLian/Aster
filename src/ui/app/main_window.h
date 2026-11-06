// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_UI_MAIN_WINDOW_H
#define ASTER_UI_MAIN_WINDOW_H
#include "ui/common/app_settings.h"
#include "ui/services/run_controller.h"
#include <QMainWindow>
#include <QSet>
class QStackedWidget;
class QPushButton;
class QProgressBar;
class QLabel;
class QSplitter;
namespace aster::ui
{
class WorkflowDocument;
class WorkflowCanvas;
class SourcePanel;
class ParameterEditor;
class TaskCatalog;
class CollapsiblePanel;
class ResultsPage;
class WorkflowPreviewPanel;
class MainWindow : public QMainWindow
{
    Q_OBJECT
  public:
    explicit MainWindow(AppSettings settings, bool persistSettings = true,
                        QWidget *parent = nullptr);
    ~MainWindow() override;
    WorkflowDocument *document() const
    {
        return m_document;
    }
    WorkflowCanvas *canvas() const
    {
        return m_canvas;
    }
    RunController *runner() const
    {
        return m_runner;
    }
    ResultsPage *results() const
    {
        return m_results;
    }
    void startRun();
    void showResults()
    {
        setPage(1);
    }
    void setPage(int page);
    void initializeWindowGeometry();
    bool restartRequested() const { return m_restartReady; }
    const AppSettings &settings() const { return m_settings; }

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    bool flushParameters();
    void selectNode(NodeId id);
    void sourceMenu(QPoint position);
    void childMenu(NodeId parent, ChildSlot slot, QPoint position);
    void addTask(QString method, NodeId parent, ChildSlot slot);
    void catalogActivate(QString method);
    void removeTask(NodeId id);
    void importFiles();
    void settingsDialog();
    void updateActions();
    void showMessage(const QString &message);
    void batchFinished(std::shared_ptr<const BatchResult> batch);
    AppSettings m_settings;
    bool m_persistSettings = true, m_closeWhenIdle = false;
    bool m_restartPending = false, m_restartReady = false;
    WorkflowDocument *m_document;
    RunController *m_runner;
    WorkflowCanvas *m_canvas;
    SourcePanel *m_sources;
    ParameterEditor *m_editor;
    TaskCatalog *m_catalog;
    ResultsPage *m_results;
    QStackedWidget *m_pages;
    QSplitter *m_taskSplitter;
    QWidget *m_taskActions;
    CollapsiblePanel *m_parameterPanel, *m_catalogPanel, *m_previewPanel;
    WorkflowPreviewPanel *m_preview;
    QPushButton *m_taskTab, *m_resultTab, *m_run, *m_cancel, *m_newSource, *m_undo, *m_redo,
        *m_remove, *m_settingsButton, *m_retry;
    QProgressBar *m_progress;
    QLabel *m_status;
    QLabel *m_notice;
    QLabel *m_zoom;
    QSet<QString> m_terminalSteps;
    QMap<RunId, std::shared_ptr<const RunResult>> m_unsaved;
};
} // namespace aster::ui
#endif
