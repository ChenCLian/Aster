// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QScopedValueRollback>
#include "ui/settings/settings_dialog.h"
#include "ui/settings/theme_manager.h"
#include "ui/settings/language_catalog.h"
#include "ui/app/main_window.h"
#include "ui/workflow/node_edit_dialog.h"
#include "ui/help/help_dialog.h"
#include "ui/services/record_directory_service.h"
#include "ui/common/presentation.h"
#include "ui/common/popup_placement.h"
#include "ui/common/output_picker.h"
#include "ui/workflow/workflow_document.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/panels/source_panel.h"
#include "ui/panels/workflow_preview_panel.h"
#include "ui/panels/pairing_panel.h"
#include "ui/panels/task_catalog.h"
#include "ui/panels/parameter_editor.h"
#include "ui/panels/collapsible_panel.h"
#include "ui/results/results_page.h"
#include "processing/scientific_registry.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QSplitter>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QStatusBar>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QCloseEvent>
#include <QScreen>
#include <QSettings>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QShortcut>
#include <QTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <numeric>
namespace aster::ui
{
MainWindow::MainWindow(AppSettings settings, bool persist, QWidget *p)
    : QMainWindow(p), m_settings(std::move(settings)), m_persistSettings(persist)
{
    setObjectName("AsterMainWindow");
    setWindowTitle("Aster");
    m_document = new WorkflowDocument(makeScientificRegistry(), this);
    m_runner = new RunController(this);
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *header = new QFrame;
    header->setObjectName("Header");
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(18, 9, 18, 9);
    hl->setSpacing(10);
    auto *brand = new QLabel("Aster");
    brand->setObjectName("Brand");
    hl->addWidget(brand);
    m_taskTab = new QPushButton(text("ui.workflow"));
    m_resultTab = new QPushButton(text("ui.results"));
    for (auto *tab : {m_taskTab, m_resultTab})
    {
        tab->setObjectName("Nav");
        tab->setCheckable(true);
        tab->setMinimumWidth(82);
        hl->addWidget(tab);
    }
    m_taskTab->setChecked(true);
    m_taskActions = new QWidget;
    auto *actions = new QHBoxLayout(m_taskActions);
    actions->setContentsMargins(10, 0, 0, 0);
    actions->setSpacing(6);
    m_newSource = new QPushButton(text("ui.data_source"));
    m_newSource->setObjectName("addSource");
    m_undo = new QPushButton(text("ui.undo"));
    m_undo->setObjectName("undoWorkflowEdit");
    m_redo = new QPushButton(text("ui.redo"));
    m_redo->setObjectName("redoWorkflowEdit");
    m_remove = new QPushButton(text("ui.delete"));
    m_remove->setObjectName("deleteNode");
    for (auto *button : {m_newSource, m_undo, m_redo, m_remove})
        actions->addWidget(button);
    hl->addWidget(m_taskActions);
    hl->addStretch();
    m_retry = new QPushButton(text("ui.retry_saving"));
    m_retry->hide();
    hl->addWidget(m_retry);
    m_cancel = new QPushButton(text("ui.cancel"));
    m_cancel->setObjectName("cancelRun");
    m_cancel->hide();
    hl->addWidget(m_cancel);
    m_run = new QPushButton(text("ui.run_workflow"));
    m_run->setObjectName("Primary");
    m_run->setProperty("role", "runWorkflow");
    m_run->setMinimumSize(148, 42);
    m_run->setIcon(runIcon());
    m_run->setIconSize(QSize(20, 20));
    connect(&ThemeManager::instance(), &ThemeManager::changed, this,
            [this] { m_run->setIcon(runIcon()); });
    hl->addWidget(m_run);
    m_settingsButton = new QPushButton(text("ui.settings"));
    m_settingsButton->setObjectName("openSettings");
    hl->addWidget(m_settingsButton);
    layout->addWidget(header);
    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setMinimumWidth(0);
    m_notice->setObjectName("Notice");
    m_notice->hide();
    layout->addWidget(m_notice);
    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);
    auto *taskPage = new QWidget;
    auto *tl = new QVBoxLayout(taskPage);
    tl->setContentsMargins(12, 12, 12, 10);
    tl->setSpacing(8);
    m_progress = new QProgressBar;
    m_progress->setObjectName("runProgress");
    m_progress->setTextVisible(false);
    m_progress->hide();
    tl->addWidget(m_progress);
    m_taskSplitter = new QSplitter;
    m_taskSplitter->setObjectName("taskSplitter");
    m_taskSplitter->setChildrenCollapsible(false);
    tl->addWidget(m_taskSplitter, 1);
    m_sources = new SourcePanel(m_document);
    m_taskSplitter->addWidget(m_sources);
    auto *canvasPanel = new QWidget;
    canvasPanel->setObjectName("Panel");
    canvasPanel->setMinimumWidth(300);
    auto *cv = new QVBoxLayout(canvasPanel);
    // Keep the opaque graphics viewport inside the panel's rounded border.
    cv->setContentsMargins(3, 3, 3, 3);
    cv->setSpacing(0);
    m_canvas = new WorkflowCanvas(m_document);
    cv->addWidget(m_canvas, 1);
    m_preview = new WorkflowPreviewPanel(m_document);
    m_previewPanel = new CollapsiblePanel(text("ui.preview_column"), m_preview, 520);
    m_previewPanel->setObjectName("previewSidePanel");
    m_previewPanel->setExpandedWidthRange(300, 720);
    m_previewPanel->setGripOnRight(true);
    m_taskSplitter->addWidget(m_previewPanel);
    m_previewPanel->setResizeTarget(canvasPanel);
    connect(m_sources, &SourcePanel::previewInputChanged, m_preview, &WorkflowPreviewPanel::setFile);
    connect(m_sources, &SourcePanel::previewSourceChanged,
            m_preview->controller(), &WorkflowPreviewController::setSource);
    auto *canvasBar = new QHBoxLayout;
    canvasBar->setContentsMargins(12, 7, 12, 7);
    canvasBar->addStretch();
    m_zoom = new QLabel("100%");
    m_zoom->setObjectName("Muted");
    canvasBar->addWidget(m_zoom);
    auto *fit = new QPushButton(text("ui.fit_3bc70c"));
    fit->setObjectName("fitWorkflow");
    canvasBar->addWidget(fit);
    cv->addLayout(canvasBar);
    m_taskSplitter->addWidget(canvasPanel);
    m_editor = new ParameterEditor;
    m_catalog = new TaskCatalog(m_document->model().registry());
    m_pairing = new PairingPanel(m_document);
    static_cast<QVBoxLayout *>(m_editor->layout())->insertWidget(2, m_pairing);
    connect(m_pairing, &PairingPanel::pairSelected, m_preview->controller(), &WorkflowPreviewController::selectPair);
    connect(m_pairing, &PairingPanel::pairSelected, m_canvas, &WorkflowCanvas::setPreviewPair);
    m_parameterPanel = new CollapsiblePanel(text("ui.step_settings"), m_editor, 300);
    m_parameterPanel->setObjectName("parameterSidePanel");
    m_catalogPanel = new CollapsiblePanel(text("ui.task_library"), m_catalog, 260);
    m_catalogPanel->setObjectName("catalogSidePanel");
    m_taskSplitter->addWidget(m_parameterPanel);
    m_taskSplitter->addWidget(m_catalogPanel);
    m_parameterPanel->setResizeTarget(canvasPanel, m_previewPanel);
    m_catalogPanel->setResizeTarget(canvasPanel, m_previewPanel);
    m_taskSplitter->setStretchFactor(1, 1);
    m_taskSplitter->setStretchFactor(2, 1);
    m_pages->addWidget(taskPage);
    m_results = new ResultsPage(m_document->model().registry());
    m_results->workspace()->setDefaultPlotNewPage(m_settings.defaultPlotNewPage);
    m_pages->addWidget(m_results);
    connect(m_results, &ResultsPage::message, this, &MainWindow::showMessage);
    m_results->setDatabase(m_settings.databasePath);
    connect(m_results, &ResultsPage::sourceRequested, this,
            [this](OutputReference ref)
            {
                if (m_runner->busy())
                {
                    showMessage(text("ui.wait_for_the_current_run"));
                    return;
                }
                auto r = m_document->addDatabaseSource(ref);
                if (!r.ok())
                {
                    showMessage(r.error);
                    return;
                }
                setPage(0);
                m_sources->refresh();
                selectNode(r.nodeId);
                showMessage(text("ui.database_source_added_connect_tasks_and_run"));
            });
    m_status = new QLabel(text("ui.ready_b796f2"));
    m_status->setObjectName("runStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(m_status, 1);
    auto *version = new QLabel(QCoreApplication::applicationVersion());
    statusBar()->addPermanentWidget(version);
    connect(m_taskTab, &QPushButton::clicked, this, [this] { setPage(0); });
    connect(m_resultTab, &QPushButton::clicked, this, [this] { setPage(1); });
    connect(m_newSource, &QPushButton::clicked, this,
            [this] { sourceMenu(m_newSource->mapToGlobal(QPoint(0, m_newSource->height()))); });
    connect(m_canvas, &WorkflowCanvas::insertRequested, this,
        [this](NodeId child, QPoint pos) { rewriteMenu(NodeRewriteKind::InsertBefore, child, pos); });
    connect(m_canvas, &WorkflowCanvas::replaceRequested, this,
        [this](NodeId node, QPoint pos) { rewriteMenu(NodeRewriteKind::Replace, node, pos); });
    connect(m_canvas, &WorkflowCanvas::insertMethodDropped, this,
        [this](QString method, NodeId child) { reviewRewrite(NodeRewriteKind::InsertBefore, child, method); });
    connect(m_canvas, &WorkflowCanvas::replaceMethodDropped, this,
        [this](QString method, NodeId node) { reviewRewrite(NodeRewriteKind::Replace, node, method); });
    connect(m_sources, &SourcePanel::importRequested, this, &MainWindow::importFiles);
    // Structural input actions must consume the current parameter draft before
    // removing its identity or taking a pairing-dialog revision snapshot.
    m_sources->setBeforeInputRemoval([this] { return !m_runner->busy() && flushParameters(); });
    m_pairing->setBeforePairEdit([this] { return !m_runner->busy() && flushParameters(); });
    connect(m_sources, &SourcePanel::moveInputsRequested, this, [this] {
        if (!m_runner->busy() && flushParameters()) m_sources->moveSelectedDatasets();
    });
    connect(m_sources, &SourcePanel::inputsMoved, this, [this](NodeId root) {
        selectNode(root);
        m_canvas->ensureVisible(m_canvas->nodeRect(root),24,24);
    });
    connect(m_sources, &SourcePanel::editRejected, this, &MainWindow::showMessage);
    connect(m_sources, &SourcePanel::readinessChanged, this, [this] { updateActions(); });
    connect(m_canvas, &WorkflowCanvas::rootRequested, this, &MainWindow::sourceMenu);
    connect(m_canvas, &WorkflowCanvas::nodeSelected, this, &MainWindow::selectNode);
    connect(m_canvas, &WorkflowCanvas::slotActivated, this, &MainWindow::childMenu);
    connect(m_canvas, &WorkflowCanvas::methodDropped, this, &MainWindow::addTask);
    connect(m_canvas, &WorkflowCanvas::sourceDropped, this, &MainWindow::catalogActivate);
    connect(m_canvas, &WorkflowCanvas::removeRequested, this, &MainWindow::removeTask);
    connect(m_remove, &QPushButton::clicked, this,
            [this] { removeTask(m_canvas->selectedNode()); });
    connect(m_catalog, &TaskCatalog::methodActivated, this, &MainWindow::catalogActivate);
    connect(m_editor, &ParameterEditor::edited, this,
            [this] { m_preview->controller()->setEditing(true); });
    connect(m_editor, &ParameterEditor::applyRequested, this, [this] { flushParameters(); });
    connect(m_sources, &SourcePanel::previewSourceChanged, this, [this] { refreshParameterScope(); });
    connect(m_pairing, &PairingPanel::pairSelected, this, [this] { refreshParameterScope(); });
    connect(m_editor, &ParameterEditor::applyAllRequested, this, [this] {
        if (m_runner->busy() || !flushParameters()) return;
        QScopedValueRollback<bool> updating(m_updatingEditor,true);
        const auto result=m_document->setScopedParameters(m_editor->nodeId(),m_editorItem,m_editor->parameters(),true);
        if (!result.ok()) m_editor->setError(result.error);
    });
    connect(m_preview->controller(), &WorkflowPreviewController::busyChanged, this, [this](bool busy) {
        if (!busy && m_closeWhenIdle) QTimer::singleShot(0, this, &QWidget::close);
    });
    connect(m_document, &WorkflowDocument::changed, this,
            [this]
            {
                m_canvas->rebuild();
                if (!m_document->node(m_editor->nodeId()))
                {
                    m_editor->showNode(nullptr, m_document->model().registry());
                    m_preview->controller()->setEditing(false);
                }
                if (!m_updatingEditor && !m_editor->dirty()) refreshParameterScope(true);
                updateActions();
            });
    connect(m_document, &WorkflowDocument::sourcesChanged, this,
            [this]
            {
                m_canvas->rebuild();
                if (!m_updatingEditor && !m_editor->dirty()) refreshParameterScope(true);
                updateActions();
            });
    connect(m_undo, &QPushButton::clicked, this, [this] { stepHistory(false); });
    connect(m_redo, &QPushButton::clicked, this, [this] { stepHistory(true); });
    connect(fit, &QPushButton::clicked, m_canvas, &WorkflowCanvas::fitWorkflow);
    connect(m_canvas, &WorkflowCanvas::zoomChanged, this,
            [this](int n) { m_zoom->setText(QString::number(n) + "%"); });
    connect(m_run, &QPushButton::clicked, this, &MainWindow::startRun);
    struct ActiveStep {
        QString file, method, phase;
        qint64 started = 0;
        int percent = -1;
    };
    struct PendingRunUi {
        QMap<QPair<NodeId, QString>, SourceFileState> files;
        QHash<QPair<RunId, NodeId>, ActiveStep> active;
        QElapsedTimer clock;
        int saved = 0;
        bool directory = false, cancelling = false;
    };
    const auto pendingUi = std::make_shared<PendingRunUi>();
    auto *runUiRefresh = new QTimer(this);
    runUiRefresh->setInterval(200);
    const auto flushRunUi = [this, pendingUi] {
        for (auto it = pendingUi->files.cbegin(); it != pendingUi->files.cend(); ++it)
            m_sources->setFileState(it.key().first, it.key().second, it.value());
        pendingUi->files.clear();
        m_progress->setValue(m_terminalSteps.size());
        if (pendingUi->directory) {
            m_results->recordsAdded();
            pendingUi->directory = false;
        }
        if (pendingUi->cancelling) return;
        auto status = text("ui.batch_live_counts").arg(m_terminalSteps.size())
                          .arg(m_progress->maximum()).arg(pendingUi->saved);
        // Show the oldest active step, including time spent in native algorithms
        // that have no percentage callback. The step count only advances on completion.
        const auto &active = pendingUi->active;
        auto oldest = std::min_element(active.cbegin(), active.cend(), [](const auto &a, const auto &b) {
            return a.started < b.started;
        });
        if (oldest != active.cend()) {
            status += "  ·  " + oldest->file + "  ·  " +
                (oldest->phase.isEmpty() ? oldest->method : oldest->phase);
            if (oldest->percent >= 0) status += QString(" %1%").arg(oldest->percent);
            status += text("ui.step_elapsed").arg((pendingUi->clock.elapsed() - oldest->started) / 1000);
        }
        if (status != m_status->text()) m_status->setText(status);
    };
    connect(runUiRefresh, &QTimer::timeout, this, flushRunUi);
    connect(m_cancel, &QPushButton::clicked, this,
            [this, pendingUi]
            {
                pendingUi->cancelling = true;
                m_runner->cancel();
                m_status->setText(text("ui.cancelling_waiting_for_the_current_algorithm"));
            });
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::settingsDialog);
    connect(m_retry, &QPushButton::clicked, this,
            [this] { m_runner->retry(m_unsaved.values(), m_settings.databasePath); });
    connect(m_runner, &RunController::eventReady, this,
            [this, pendingUi](const RunEvent &e)
            {
                const auto key = qMakePair(e.runId, e.nodeId);
                if (e.state == NodeState::Running) {
                    auto it = pendingUi->active.find(key);
                    if (it == pendingUi->active.end()) {
                        const auto *node = m_document->node(e.nodeId);
                        const auto label = node ? methodLabel(m_document->model().registry().find(node->method)->descriptor) : QString();
                        it = pendingUi->active.insert(key, {QFileInfo(e.sourceLabel).fileName(), label, {},
                                                           pendingUi->clock.elapsed(), -1});
                        pendingUi->files[{e.root, e.sourceLabel}] = SourceFileState::Running;
                    }
                    if (e.progressPercent >= 0) {
                        it->percent = e.progressPercent;
                        it->phase = translatedLabel(e.message, e.message);
                    }
                } else if (e.state != NodeState::Pending) {
                    pendingUi->active.remove(key);
                    m_terminalSteps.insert(key);
                }
            });
    // Fast batches produce many notifications between paints. Preserve every
    // terminal step, but coalesce their presentation and directory queries.
    connect(m_runner, &RunController::itemReady, this,
            [pendingUi](const BatchItem &item)
            {
                pendingUi->files[{item.root, item.sourceLabel}] =
                                        item.computation == RunState::Succeeded
                                            ? (item.persistence.saved()
                                                   ? SourceFileState::Saved
                                                   : SourceFileState::Unsaved)
                                        : item.computation == RunState::Cancelled
                                            ? SourceFileState::Cancelled
                                            : SourceFileState::Failed;
                pendingUi->directory |= item.persistence.saved();
                if (item.persistence.saved()) ++pendingUi->saved;
            });
    connect(m_runner, &RunController::batchReady, this,
            [this, pendingUi, runUiRefresh, flushRunUi](std::shared_ptr<const BatchResult> batch) {
                runUiRefresh->stop();
                flushRunUi();
                batchFinished(std::move(batch));
            });
    connect(m_runner, &RunController::busyChanged, this,
            [this, pendingUi, runUiRefresh](bool busy)
            {
                if (busy) {
                    *pendingUi = {};
                    pendingUi->clock.start();
                    runUiRefresh->start();
                    m_browsedRunResults = false;
                } else {
                    runUiRefresh->stop();
                }
                m_document->setEditingLocked(busy);
                m_results->setDatabaseMutationsEnabled(!busy);
                m_canvas->setEditable(!busy);
                m_sources->updateRunControls();
                if (!busy && !m_closeWhenIdle)
                    m_sources->showFileOptions();
                m_preview->setSuspended(busy || m_closeWhenIdle);
                m_editor->setEnabled(!busy);
                m_catalog->setEnabled(!busy);
                m_cancel->setVisible(busy);
                m_progress->setVisible(busy);
                updateActions();
                if (!busy && !m_results->exportBusy() && !m_results->databaseBusy() &&
                    m_closeWhenIdle)
                    QTimer::singleShot(0, this, &QWidget::close);
            });
    connect(m_results, &ResultsPage::recordChanging, this, [this] {
        if (m_runner->busy()) m_browsedRunResults = true;
    });
    connect(m_results, &ResultsPage::databaseMutationBusyChanged, this, [this] { updateActions(); });
    connect(m_results, &ResultsPage::databaseBusyChanged, this,
            [this](bool busy)
            {
                updateActions();
                if (!busy && !m_runner->busy() && !m_results->exportBusy() && m_closeWhenIdle)
                    QTimer::singleShot(0, this, &QWidget::close);
            });
    connect(m_results, &ResultsPage::exportBusyChanged, this,
            [this](bool busy)
            {
                if (!busy && !m_runner->busy() && !m_results->databaseBusy() && m_closeWhenIdle)
                    QTimer::singleShot(0, this, &QWidget::close);
            });
    auto *helpShortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    auto helpKeys = QKeySequence::keyBindings(QKeySequence::HelpContents);
    if (!helpKeys.contains(QKeySequence(Qt::Key_F1))) helpKeys.append(QKeySequence(Qt::Key_F1));
    helpShortcut->setKeys(helpKeys);
    connect(helpShortcut, &QShortcut::activated, this, [this] { openHelp(this, userGuideTopic()); });
    const auto bind = [](QPushButton *button, const QKeySequence &key) {
        button->setShortcut(key);
        button->setToolTip(shortcutLabel(button->text(), key));
        button->setAccessibleDescription(button->toolTip());
    };
    bind(m_taskTab, QKeySequence("Ctrl+1"));
    bind(m_resultTab, QKeySequence("Ctrl+2"));
    bind(m_settingsButton, QKeySequence("Ctrl+,"));
    m_previewPanel->setToggleShortcut(QKeySequence("Ctrl+Shift+1"));
    m_parameterPanel->setToggleShortcut(QKeySequence("Ctrl+Shift+2"));
    m_catalogPanel->setToggleShortcut(QKeySequence("Ctrl+Shift+3"));
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, m_taskActions);
    connect(undoShortcut, &QShortcut::activated, m_undo, &QPushButton::click);
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, m_taskActions);
    connect(redoShortcut, &QShortcut::activated, m_redo, &QPushButton::click);
    updateActions();
    initializeWindowGeometry();
    QTimer::singleShot(0, this, [this, canvasPanel]
    {
        m_catalogPanel->setCollapsed(width() < 1750);
        m_parameterPanel->setCollapsed(width() < 1250);
        const int settingsWidth = m_parameterPanel->isCollapsed() ? m_parameterPanel->minimumWidth() : 280;
        const int catalogWidth = m_catalogPanel->isCollapsed() ? m_catalogPanel->minimumWidth() : 260;
        const auto sizes = m_taskSplitter->sizes();
        // Splitter sizes exclude the handles, including their theme-dependent widths.
        const int available = std::accumulate(sizes.cbegin(), sizes.cend(), 0);
        const int workspaceWidth = available - settingsWidth - catalogWidth;
        constexpr int compactPreviewWidth = 380;
        const int filesWidth = std::clamp(workspaceWidth - canvasPanel->minimumWidth() - compactPreviewWidth,
                                          m_sources->minimumWidth(), 270);
        const int centerWidth = workspaceWidth - filesWidth;
        const int canvasReserve = std::max(canvasPanel->minimumWidth(), std::min(available / 4, 360));
        const int preferredPreview = std::max(compactPreviewWidth, width() / 3);
        const int previewWidth = std::clamp(std::min(preferredPreview, centerWidth - canvasReserve),
                                            m_previewPanel->minimumWidth(), m_previewPanel->maximumWidth());
        m_taskSplitter->setSizes({filesWidth, previewWidth, std::max(canvasPanel->minimumWidth(), centerWidth - previewWidth),
                                  settingsWidth, catalogWidth});
        m_canvas->fitWorkflow();
    });
}
MainWindow::~MainWindow()
{
    delete m_runner;
}
void MainWindow::showMessage(const QString &message)
{
    m_notice->setText(message);
    m_notice->setVisible(!message.isEmpty());
    statusBar()->showMessage(message, 12000);
}
void MainWindow::setPage(int page)
{
    if (page == 0)
        m_sources->syncPreviewInput();
    if (page == 1 && m_runner->busy()) m_browsedRunResults = true;
    m_pages->setCurrentIndex(page);
    m_taskTab->setChecked(page == 0);
    m_resultTab->setChecked(page == 1);
    m_taskActions->setVisible(page == 0);
    m_run->setVisible(page == 0);
}
void MainWindow::initializeWindowGeometry()
{
    auto *s = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect available = s ? s->availableGeometry() : QRect(0, 0, 1440, 900);
    setMinimumSize(std::min(920, available.width()), std::min(560, available.height()));
    const int w = std::min(1680, int(available.width() * .94)),
              h = std::min(int(available.height() * .90), int(w / 1.70));
    resize(w, std::max(minimumHeight(), h));
    if (m_persistSettings)
    {
        QSettings settings(AppSettings::configPath(), QSettings::IniFormat);
        auto bytes = settings.value("window/geometry").toByteArray();
        if (!bytes.isEmpty())
            restoreGeometry(bytes);
    }
    resize(std::min(width(), available.width()), std::min(height(), available.height()));
    move(available.center() - QPoint(width() / 2, height() / 2));
}
bool MainWindow::flushParameters()
{
    if (!m_editor->dirty())
        return true;
    QScopedValueRollback<bool> updating(m_updatingEditor,true);
    auto r = m_editorItem.isNull()
        ? m_document->setParameters(m_editor->nodeId(),m_editor->parameters())
        : m_document->setScopedParameters(m_editor->nodeId(),m_editorItem,m_editor->parameters());
    if (!r.ok())
    {
        m_editor->setError(r.error);
        return false;
    }
    m_editor->markClean();
    m_preview->controller()->setEditing(false);
    return true;
}
void MainWindow::refreshParameterScope(bool force)
{
    if (m_updatingEditor || !m_editor || !m_pairing) return;
    const auto id=m_editor->nodeId();
    const auto *node=m_document->node(id);
    if (!node) return;
    const auto joint=m_document->pairNodeFor(id);
    const auto root=m_document->rootFor(id);
    const auto *rootNode=m_document->node(root);
    if (!rootNode) return;
    const bool external=m_document->model().registry().find(rootNode->method)->descriptor.requiresSourceBinding;
    QUuid item;
    QString label;
    int count=0;
    bool editable=true;
    if (!joint.isNull()) {
        item=m_pairing->currentPair(); label=m_pairing->currentLabel();
        count=m_document->grouping().value(joint).size(); editable=!item.isNull();
    } else if (!external) {
        item=root; label=methodLabel(m_document->model().registry().find(rootNode->method)->descriptor); count=1;
    } else {
        count=m_document->sources().value(root).size();
        if (root==m_sources->root()) {
            const auto source=m_sources->currentSource();
            item=QUuid(source.provenance["dataset_id"].toString()); label=source.label;
            const auto path = source.provenance["path"].toString();
            if (!path.isEmpty()) {
                auto portablePath = path;
                portablePath.replace('\\', '/');
                label = portablePath.section('/', -1);
            }
        }
        editable=!item.isNull() || count==0;
    }
    if (label.isEmpty()) label=text(count==0 ? "ui.new_data_defaults" : "ui.select_input_for_parameters");
    if (!force && item==m_editorItem) return;
    if (!flushParameters()) return;
    QScopedValueRollback<bool> updating(m_updatingEditor,true);
    node=m_document->node(id); // Committing the previous scope can replace the graph.
    if (!node) return;
    const auto effective=resolvedNode(*node,item);
    m_editorItem=item;
    m_editor->showNode(&effective,m_document->model().registry());
    m_editor->setScope(label,!joint.isNull(),editable,count>1);
}
void MainWindow::selectNode(NodeId id)
{
    if (!flushParameters()) return;
    {
        QScopedValueRollback<bool> updating(m_updatingEditor,true);
        m_canvas->setSelectedNode(id);
        const auto root=m_document->rootFor(id);
        if (!root.isNull()) m_sources->selectRoot(root);
        m_pairing->selectNode(id);
        m_editor->showNode(m_document->node(id),m_document->model().registry());
        m_editorItem={};
    }
    refreshParameterScope(true);
    if (m_document->node(id)) m_preview->controller()->selectNode(id);
    updateActions();
}
void MainWindow::stepHistory(bool redo)
{
    if (!flushParameters()) return;
    const auto before=m_canvas->selectedNode();
    QUuid dataset;
    if (m_document->rootFor(before)==m_sources->root() && m_document->pairNodeFor(before).isNull())
        dataset=QUuid(m_sources->currentSource().provenance["dataset_id"].toString());
    const auto result=redo ? m_document->redo() : m_document->undo();
    if (!result.ok()) { showMessage(result.error); return; }
    auto selected=m_canvas->selectedNode();
    // Follow the same input through a tree move without storing another history
    // of datasets or numeric buffers. Joint-node selection remains independent.
    NodeId destination;
    if (!dataset.isNull())
        for (auto it=m_document->sources().cbegin();it!=m_document->sources().cend() && destination.isNull();++it)
            for (const auto &source:it.value())
                if (QUuid(source.provenance["dataset_id"].toString())==dataset) { destination=it.key(); break; }
    if (!destination.isNull()) {
        m_sources->selectDataset(destination,dataset);
        if (!m_document->node(selected) || m_document->rootFor(selected)!=destination) selected=destination;
    }
    selectNode(selected);
    if (selected!=before && m_document->node(selected))
        m_canvas->ensureVisible(m_canvas->nodeRect(selected),24,24);
}
void MainWindow::sourceMenu(QPoint pos)
{
    if (m_runner->busy() || !flushParameters())
        return;
    auto *menu = m_catalog->menu(
            this, [](const TaskDescriptor &d) { return !d.inputKind; },
            [this](QString method)
            {
                if (m_runner->busy() || !flushParameters()) return;
                auto r = m_document->addRoot(method);
                if (!r.ok())
                {
                    showMessage(r.error);
                    return;
                }
                selectNode(r.nodeId);
                m_canvas->fitWorkflow();
            });
    showMenuWithinWindow(menu, this, QRect(pos, QSize(1, 1)));
}
void MainWindow::childMenu(NodeId parent, ChildSlot slot, QPoint pos)
{
    if (m_runner->busy() || !flushParameters())
        return;
    selectNode(parent);
    auto *menu = m_catalog->menu(
            this,
            [this, parent, slot](const TaskDescriptor &d)
            {
                if (!d.inputKind)
                    return false;
                auto *n = m_document->node(parent);
                if (!n)
                    return false;
                const auto *p = m_document->model().registry().find(n->method);
                for (const auto &output : p->descriptor.outputs)
                    if (d.acceptsInput(output) &&
                        m_document->model().previewChild(parent, slot, d.key, {}, output.key).ok())
                        return true;
                return false;
            },
            [this, parent, slot](QString method) { addTask(method, parent, slot); });
    showMenuWithinWindow(menu, this, QRect(pos, QSize(1, 1)));
}
QRect MainWindow::taskAnchor(NodeId node) const
{
    auto *viewport = m_canvas->viewport();
    auto rect = m_canvas->mapFromScene(m_canvas->nodeRect(node)).boundingRect().intersected(viewport->rect());
    if (rect.isEmpty()) rect = QRect(viewport->rect().center(), QSize(1, 1));
    return QRect(viewport->mapToGlobal(rect.topLeft()), rect.size());
}
void MainWindow::addTask(QString method, NodeId parent, ChildSlot slot)
{
    if (m_runner->busy() || !flushParameters())
        return;
    auto *n = m_document->node(parent);
    auto *registration = m_document->model().registry().find(method);
    if (!n || !registration || !registration->descriptor.inputKind)
        return;
    if (m_document->model().childCount(parent) >= WorkflowChildLimit) {
        showMessage(text("ui.child_limit").arg(WorkflowChildLimit));
        return;
    }
    const auto *input = m_document->model().registry().find(n->method);
    QStringList ports;
    for (const auto &port : input->descriptor.outputs)
        if (registration->descriptor.acceptsInput(port))
            ports.push_back(port.key);
    const auto add = [this, parent, slot, method](const QString &port)
    {
        if (m_runner->busy() || !flushParameters()) return;
        auto r = m_document->addChild(parent, slot, method, {}, port);
        if (!r.ok())
        {
            showMessage(r.error);
            return;
        }
        selectNode(r.nodeId);
        m_canvas->ensureVisible(m_canvas->nodeRect(r.nodeId).adjusted(0, 0, 40, 0), 16, 16);
    };
    if (!registration->descriptor.requiredSecondPort.isEmpty() && ports.contains(registration->descriptor.requiredSecondPort))
    {
        add(registration->descriptor.requiredSecondPort);
        return;
    }
    if (ports.size() == 1)
    {
        add(ports.front());
        return;
    }
    if (ports.isEmpty())
    {
        showMessage(text("ui.input_and_output_types_are_incompatible"));
        return;
    }
    QVector<OutputChoice> choices;
    for (auto *old : findChildren<OutputPicker *>()) old->close();
    for (const auto &port : ports)
        choices.push_back({port, portLabel(input->descriptor, port), port == input->descriptor.defaultOutput});
    auto *picker = new OutputPicker(this,
        text("ui.choose_output_from").arg(methodLabel(input->descriptor)),
        text("ui.output_for_task").arg(methodLabel(registration->descriptor)), choices);
    connect(picker, &OutputPicker::chosen, this, add);
    connect(m_document, &WorkflowDocument::changed, picker, &QWidget::close);
    connect(m_document, &WorkflowDocument::sourcesChanged, picker, &QWidget::close);
    connect(m_runner, &RunController::busyChanged, picker, [picker](bool busy) { if (busy) picker->close(); });
    // Defer until the task menu/drop event has finished. Use the source node,
    // never the cursor at the far end of a cascading task menu.
    QTimer::singleShot(0, picker, [this, picker, parent] {
        if (!isVisible()) { picker->close(); return; }
        // A closing cascade can queue a focus-loss event. Restore the owner
        // before opening the next popup, so that event cannot dismiss it.
        activateWindow();
        QTimer::singleShot(0, picker, [this, picker, parent] { picker->openAt(taskAnchor(parent)); });
    });
}
void MainWindow::rewriteMenu(NodeRewriteKind kind, NodeId target, QPoint position)
{
    if (m_runner->busy() || !flushParameters()) return;
    const auto reason = nodeRewriteAvailability(m_document->model(), kind, target);
    if (!reason.isEmpty()) { showMessage(nodeRewriteMessage(reason)); return; }
    selectNode(target);
    const auto revision = m_document->revision();
    auto *menu = m_catalog->menu(this,
        [this, kind, target](const TaskDescriptor &d) {
            return inspectNodeRewrite(m_document->model(), kind, target, d.key).ok();
        },
        [this, kind, target, revision](QString method) {
            if (m_document->revision() == revision) reviewRewrite(kind, target, method);
        });
    if (menu->isEmpty()) { menu->deleteLater(); showMessage(text("ui.node_edit_no_method")); return; }
    connect(m_document, &WorkflowDocument::changed, menu, &QMenu::close);
    connect(m_document, &WorkflowDocument::sourcesChanged, menu, &QMenu::close);
    connect(m_runner, &RunController::busyChanged, menu, [menu](bool busy) { if (busy) menu->close(); });
    showMenuWithinWindow(menu, this, QRect(position, QSize(1, 1)));
}
void MainWindow::reviewRewrite(NodeRewriteKind kind, NodeId target, QString method)
{
    if (m_runner->busy() || !flushParameters()) return;
    const auto options = inspectNodeRewrite(m_document->model(), kind, target, method);
    if (!options.ok()) { showMessage(nodeRewriteMessage(options.error)); return; }
    for (auto *old : findChildren<NodeEditDialog *>()) old->reject();
    // A single producer may still expose several compatible ports. Skip the
    // review only when every connection, including downstream branches, is unique.
    bool unique = options.inputs.size() == 1;
    for (const auto &mapping : options.outputs)
        unique = unique && mapping.choices.size() == 1;
    if (unique) {
        NodeRewriteRequest request;
        request.kind = kind;
        request.target = target;
        request.method = method;
        request.inputOutput = options.inputs.front().key;
        for (const auto &mapping : options.outputs)
            request.childOutputs[mapping.child] = mapping.choices.front().key;
        const auto prepared = m_document->prepareRewrite(request);
        if (!prepared.ok()) { showMessage(nodeRewriteMessage(prepared.error)); return; }
        const auto committed = m_document->commitEdit(prepared.edit);
        if (!committed.ok()) { showMessage(nodeRewriteMessage(committed.error)); return; }
        selectNode(prepared.nodeId);
        statusBar()->showMessage(text("ui.node_edit_applied"), 8000);
        return;
    }
    auto *dialog = new NodeEditDialog(m_document, kind, target, method, this);
    connect(dialog, &NodeEditDialog::committed, this, [this](NodeId id) { selectNode(id); });
    connect(m_runner, &RunController::busyChanged, dialog, [dialog](bool busy) { if (busy) dialog->reject(); });
    // Let the closing cascade restore focus before opening the review form.
    QTimer::singleShot(0, dialog, [this, dialog, target] {
        if (!isVisible() || m_runner->busy()) { dialog->reject(); return; }
        activateWindow();
        QTimer::singleShot(0, dialog, [this, dialog, target] { dialog->openAt(taskAnchor(target)); });
    });
}
void MainWindow::catalogActivate(QString method)
{
    if (m_runner->busy() || !flushParameters())
        return;
    const auto *registration = m_document->model().registry().find(method);
    if (!registration)
        return;
    if (!registration->descriptor.inputKind)
    {
        auto r = m_document->addRoot(method);
        if (r.ok())
        {
            selectNode(r.nodeId);
            m_canvas->fitWorkflow();
        }
        else
            showMessage(r.error);
        return;
    }
    const auto id = m_canvas->selectedNode();
    if (id.isNull())
    {
        showMessage(text("ui.choose_parent_for_task"));
        return;
    }
    addTask(method, id, ChildSlot::Append);
}
void MainWindow::removeTask(NodeId id)
{
    if (m_runner->busy() || id.isNull() || !flushParameters())
        return;
    auto count = m_document->model().removalPreview(id).size();
    if (!count)
        return;
    if (QMessageBox::question(
            this, text("ui.delete_task_branch"),
            QString(
                text("ui.delete_this_task_and_its_descendants_1_nodes_you_can_undo_th"))
                .arg(count), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    auto r = m_document->removeSubtree(id);
    if (!r.ok())
        showMessage(r.error);
}
void MainWindow::importFiles()
{
    if (m_runner->busy() || !flushParameters())
        return;
    qInfo() << "[Aster import] opening file chooser";
    auto paths = QFileDialog::getOpenFileNames(this, text("ui.choose_input_files"), {},
                                               "Data (*.csv *.txt *.dat *.tsv);;All files (*)");
    qInfo() << "[Aster import] chooser returned; selected files=" << paths.size();
    if (paths.isEmpty())
        return;
    QElapsedTimer timer;
    timer.start();
    m_sources->addFiles(paths);
    selectNode(m_sources->root());
    m_canvas->fitWorkflow();
    qInfo() << "[Aster import] bind/layout_ms=" << timer.elapsed();
}
void MainWindow::startRun()
{
    if (m_results->databaseMutationBusy() || m_runner->busy() || !flushParameters())
        return;
    if (!m_sources->applyCurrentOptions())
    {
        showMessage(text("ui.wait_for_valid_file_columns"));
        return;
    }
    auto plan = m_document->plan();
    if (!plan.validation.ok())
    {
        showMessage(plan.validation.message());
        return;
    }
    int total = 0;
    QHash<NodeId, int> stepsPerRoot;
    for (const auto &r : plan.requests)
    {
        if (!stepsPerRoot.contains(r.root)) {
            QHash<NodeId, QVector<NodeId>> children;
            for (const auto &n : r.workflow->definition().nodes)
                if (n.input) children[n.input->parent].push_back(n.id);
            QVector<NodeId> queue{r.root};
            for (qsizetype i = 0; i < queue.size(); ++i) queue += children.value(queue[i]);
            stepsPerRoot.insert(r.root, queue.size());
        }
        total += stepsPerRoot.value(r.root);
    }
    for (const auto &batch : plan.batches) total += batch->jobs().size();
    m_sources->clearFileStates();
    m_terminalSteps.clear();
    m_progress->setRange(0, total);
    m_progress->setValue(0);
    m_notice->hide();
    m_status->setText(text("ui.running_7ef25a"));
    m_runner->start(plan, m_settings.databasePath, m_settings.concurrency);
}
void MainWindow::batchFinished(std::shared_ptr<const BatchResult> batch)
{
    int saved = 0, failed = 0;
    qint64 last = 0;
    for (const auto &item : batch->items)
    {
        if (item.persistence.saved())
        {
            ++saved;
            last = item.persistence.recordId;
            m_unsaved.remove(item.runId);
        }
        else if (item.unsavedResult)
            m_unsaved.insert(item.runId, item.unsavedResult);
        if (item.computation != RunState::Succeeded)
            ++failed;
    }
    if (!batch->error.isEmpty())
        showMessage(batch->error);
    else if (!m_unsaved.isEmpty())
        showMessage(text("ui.some_results_are_unsaved_and_retained_in_memory_check_the_da"));
    m_status->setText(
        QString(text("ui.batch_finished_1_records_saved_2_runs_not_fully_successful"))
            .arg(saved)
            .arg(failed));
    m_retry->setVisible(!m_unsaved.isEmpty());
    m_retry->setText(text("ui.retry_saving_d6b31c") + QString::number(m_unsaved.size()));
    if (batch->notStarted) m_status->setText(m_status->text() + text("ui.batch_not_started").arg(batch->notStarted));
    if (last && !m_browsedRunResults && !m_closeWhenIdle)
    {
        m_results->refresh(last);
        setPage(1);
    } else if (!m_closeWhenIdle) {
        m_results->refreshPendingIfVisible();
    }
    updateActions();
}
void MainWindow::updateActions()
{
    bool idle = !m_runner->busy() && !m_results->databaseMutationBusy();
    m_newSource->setEnabled(idle);
    m_settingsButton->setEnabled(idle);
    m_run->setEnabled(idle && m_sources->readyToRun() &&
                      !m_document->model().definition().nodes.isEmpty());
    m_undo->setEnabled(idle && m_document->canUndo());
    m_redo->setEnabled(idle && m_document->canRedo());
    m_remove->setEnabled(idle && !m_canvas->selectedNode().isNull());
    m_retry->setEnabled(idle);
}
void MainWindow::settingsDialog()
{
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto next = dialog.values();
    if (m_persistSettings)
    {
        const auto error = next.save();
        if (!error.isEmpty()) { QMessageBox::warning(this, text("ui.settings"), error); return; }
    }
    const bool changed = next.databasePath != m_settings.databasePath;
    // Compare with the displayed language: a previously deferred change may
    // already be saved without having been applied to the current widgets.
    const bool languageChanged = next.language != LanguageCatalog::instance().language();
    m_settings = next;
    m_results->workspace()->setDefaultPlotNewPage(m_settings.defaultPlotNewPage);
    ThemeManager::instance().apply(*qApp, m_settings.theme);
    if (changed) m_results->setDatabase(m_settings.databasePath);
    if (languageChanged)
    {
        QString message = text("ui.restart_language_prompt");
        if (!m_document->model().definition().nodes.isEmpty())
            message += "\n\n" + text("ui.restart_workflow_notice");
        QMessageBox prompt(QMessageBox::Question, text("ui.restart_aster"), message,
                           QMessageBox::Yes | QMessageBox::No, this);
        prompt.setObjectName("languageRestartPrompt");
        prompt.setDefaultButton(QMessageBox::No);
        if (prompt.exec() == QMessageBox::Yes)
        {
            m_restartPending = true;
            QTimer::singleShot(0, this, &QWidget::close);
        }
    }
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    m_preview->setSuspended(true);
    if (m_preview->controller()->busy() && !m_runner->busy() && !m_results->exportBusy())
    {
        m_closeWhenIdle = true;
        m_status->setText(text("ui.waiting_for_background_work_to_finish"));
        event->ignore();
        return;
    }
    if (!m_runner->busy() && !m_results->exportBusy() && m_results->databaseBusy())
    {
        m_closeWhenIdle = true;
        m_status->setText(
            text("ui.closing_after_database_operations_finish"));
        event->ignore();
        return;
    }
    if (m_runner->busy() || m_results->exportBusy())
    {
        if (!m_closeWhenIdle && ((m_runner->cancelling() && !m_results->exportBusy()) ||
            QMessageBox::question(
                this, text("ui.work_is_running"),
                text("ui.cancel_active_computation_export_and_close_after_workers_fin")) ==
                QMessageBox::Yes))
        {
            m_closeWhenIdle = true;
            m_runner->cancel();
            m_results->cancelExports();
            m_status->setText(
                text("ui.waiting_for_background_work_to_finish"));
            // A fast batch may have finished while the confirmation dialog
            // was running its nested event loop. Do not wait for a second
            // busyChanged(false) signal that will never arrive.
            if (!m_runner->busy() && !m_results->exportBusy() && !m_results->databaseBusy())
                QTimer::singleShot(0, this, &QWidget::close);
        }
        else if (!m_closeWhenIdle)
        {
            m_restartPending = false;
            // The user declined closing. Export completion does not emit a
            // runner idle signal, so restore preview availability here.
            m_preview->setSuspended(m_runner->busy());
        }
        event->ignore();
        return;
    }
    if (!m_unsaved.isEmpty() &&
        QMessageBox::warning(this, text("ui.unsaved_results"),
                             text("ui.some_results_have_not_been_stored_closing_discards_these_in_"),
                             QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
    {
        m_closeWhenIdle = false;
        m_restartPending = false;
        m_preview->setSuspended(m_runner->busy());
        event->ignore();
        return;
    }
    if (m_persistSettings)
    {
        QSettings settings(AppSettings::configPath(), QSettings::IniFormat);
        settings.setValue("window/geometry", saveGeometry());
        m_settings.save();
    }
    m_restartReady = m_restartPending;
    event->accept();
}
} // namespace aster::ui
