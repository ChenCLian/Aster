// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
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
    m_redo = new QPushButton(text("ui.redo"));
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
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    m_canvas = new WorkflowCanvas(m_document);
    cv->addWidget(m_canvas, 1);
    m_preview = new WorkflowPreviewPanel(m_document);
    m_previewPanel = new CollapsiblePanel(text("ui.preview_column"), m_preview, 400);
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
    m_parameterPanel->setResizeTarget(canvasPanel);
    m_catalogPanel->setResizeTarget(canvasPanel);
    m_taskSplitter->setStretchFactor(2, 1);
    m_taskSplitter->setSizes({270, 400, 600, 280, 260});
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
                updateActions();
            });
    connect(m_document, &WorkflowDocument::sourcesChanged, this,
            [this]
            {
                m_canvas->rebuild();
                updateActions();
            });
    connect(m_undo, &QPushButton::clicked, this,
            [this]
            {
                if (flushParameters())
                {
                    auto r = m_document->undo();
                    if (!r.ok())
                        showMessage(r.error);
                    selectNode(m_canvas->selectedNode());
                }
            });
    connect(m_redo, &QPushButton::clicked, this,
            [this]
            {
                if (flushParameters())
                {
                    auto r = m_document->redo();
                    if (!r.ok())
                        showMessage(r.error);
                    selectNode(m_canvas->selectedNode());
                }
            });
    connect(fit, &QPushButton::clicked, m_canvas, &WorkflowCanvas::fitWorkflow);
    connect(m_canvas, &WorkflowCanvas::zoomChanged, this,
            [this](int n) { m_zoom->setText(QString::number(n) + "%"); });
    connect(m_run, &QPushButton::clicked, this, &MainWindow::startRun);
    struct PendingRunUi {
        QMap<QPair<NodeId, QString>, SourceFileState> files;
        QString status;
        bool progress = false, directory = false, cancelling = false;
    };
    const auto pendingUi = std::make_shared<PendingRunUi>();
    auto *runUiRefresh = new QTimer(this);
    runUiRefresh->setSingleShot(true);
    runUiRefresh->setInterval(100);
    const auto flushRunUi = [this, pendingUi] {
        // Update all states before the progress bar requests a synchronous paint.
        for (auto it = pendingUi->files.cbegin(); it != pendingUi->files.cend(); ++it)
            m_sources->setFileState(it.key().first, it.key().second, it.value());
        pendingUi->files.clear();
        if (pendingUi->progress) m_progress->setValue(m_terminalSteps.size());
        pendingUi->progress = false;
        if (!pendingUi->status.isEmpty()) m_status->setText(pendingUi->status);
        pendingUi->status.clear();
        if (pendingUi->directory) m_results->refresh();
        pendingUi->directory = false;
    };
    connect(runUiRefresh, &QTimer::timeout, this, flushRunUi);
    connect(m_cancel, &QPushButton::clicked, this,
            [this, pendingUi]
            {
                pendingUi->cancelling = true;
                pendingUi->status.clear();
                m_runner->cancel();
                m_status->setText(text("ui.cancelling_waiting_for_the_current_algorithm"));
            });
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::settingsDialog);
    connect(m_retry, &QPushButton::clicked, this,
            [this] { m_runner->retry(m_unsaved.values(), m_settings.databasePath); });
    connect(m_runner, &RunController::eventReady, this,
            [this, pendingUi, runUiRefresh](const RunEvent &e)
            {
                if (e.state == NodeState::Running && e.progressPercent < 0)
                    pendingUi->files[{e.root, e.sourceLabel}] = SourceFileState::Running;
                if (e.state == NodeState::Running && e.progressPercent >= 0 && !pendingUi->cancelling)
                    pendingUi->status = text("ui.task_progress").arg(e.sourceLabel)
                                          .arg(translatedLabel(e.message, e.message)).arg(e.progressPercent);
                if (e.state != NodeState::Pending && e.state != NodeState::Running)
                {
                    m_terminalSteps.insert(e.runId.toString() + e.nodeId.toString());
                    pendingUi->progress = true;
                    if (!pendingUi->cancelling)
                        pendingUi->status = QString(text("ui.running_1_2_steps_finished"))
                                          .arg(m_terminalSteps.size())
                                          .arg(m_progress->maximum());
                }
                if (e.state != NodeState::Pending && !runUiRefresh->isActive()) runUiRefresh->start();
            });
    // Fast batches produce many notifications between paints. Preserve every
    // terminal step, but coalesce their presentation and directory queries.
    connect(m_runner, &RunController::itemReady, this,
            [pendingUi, runUiRefresh](const BatchItem &item)
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
                if (!runUiRefresh->isActive()) runUiRefresh->start();
            });
    connect(m_runner, &RunController::batchReady, this,
            [this, pendingUi, runUiRefresh, flushRunUi](std::shared_ptr<const BatchResult> batch) {
                runUiRefresh->stop();
                // batchFinished refreshes the last saved result. If work threw
                // after some itemReady signals, keep those records visible too.
                if (std::any_of(batch->items.cbegin(), batch->items.cend(),
                                [](const BatchItem &item) { return item.persistence.saved(); }))
                    pendingUi->directory = false;
                flushRunUi();
                batchFinished(std::move(batch));
            });
    connect(m_runner, &RunController::busyChanged, this,
            [this, pendingUi, runUiRefresh](bool busy)
            {
                if (busy) { runUiRefresh->stop(); *pendingUi = {}; }
                m_document->setEditingLocked(busy);
                m_results->setDatabaseMutationsEnabled(!busy);
                m_canvas->setEditable(!busy);
                m_sources->setEnabled(!busy);
                if (!busy && !m_closeWhenIdle)
                    m_sources->syncPreviewInput();
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
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, m_undo, &QPushButton::click);
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, m_redo, &QPushButton::click);
    updateActions();
    initializeWindowGeometry();
    QTimer::singleShot(0, this, [this]
    {
        m_catalogPanel->setCollapsed(width() < 1750);
        m_parameterPanel->setCollapsed(width() < 1250);
        const int side = (m_parameterPanel->isCollapsed() ? 28 : 280) +
                         (m_catalogPanel->isCollapsed() ? 28 : 260);
        m_taskSplitter->setSizes({270, 380, std::max(300, width() - 680 - side),
                                  m_parameterPanel->isCollapsed() ? 28 : 280,
                                  m_catalogPanel->isCollapsed() ? 28 : 260});
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
    auto r = m_document->setParameters(m_editor->nodeId(), m_editor->parameters());
    if (!r.ok())
    {
        m_editor->setError(r.error);
        return false;
    }
    m_editor->markClean();
    m_preview->controller()->setEditing(false);
    return true;
}
void MainWindow::selectNode(NodeId id)
{
    if (!flushParameters())
        return;
    m_canvas->setSelectedNode(id);
    auto *n = m_document->node(id);
    m_editor->showNode(n, m_document->model().registry());
    m_pairing->selectNode(id);
    if (n)
    {
        auto *root = n;
        while (root->input) root = m_document->node(root->input->parent);
        m_sources->selectRoot(root->id);
        m_preview->controller()->selectNode(id);
    }
    updateActions();
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
                    if (output.kind == *d.inputKind &&
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
    const auto *input = m_document->model().registry().find(n->method);
    QStringList ports;
    for (const auto &port : input->descriptor.outputs)
        if (port.kind == *registration->descriptor.inputKind)
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
    };
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
        showMessage(text("ui.select_a_parent_task_then_drag_into_a_slot_or_choose_a_direc"));
        return;
    }
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (auto slot : {ChildSlot::Left, ChildSlot::Right, ChildSlot::Down})
    {
        auto *a = menu->addAction(slot == ChildSlot::Left    ? text("ui.add_left")
                                  : slot == ChildSlot::Right ? text("ui.add_right")
                                                             : text("ui.add_below"));
        const auto *n = m_document->node(id);
        bool valid = false;
        for (const auto &p : m_document->model().registry().find(n->method)->descriptor.outputs)
            if (p.kind == *registration->descriptor.inputKind &&
                m_document->model().previewChild(id, slot, method, {}, p.key).ok())
                valid = true;
        a->setEnabled(valid);
        connect(a, &QAction::triggered, menu,
                [this, method, id, slot] { addTask(method, id, slot); });
    }
    showMenuWithinWindow(menu, this, taskAnchor(id));
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
    for (const auto &r : plan.requests)
    {
        QHash<NodeId, QVector<NodeId>> children;
        for (const auto &n : r.workflow->definition().nodes)
            if (n.input)
                children[n.input->parent].push_back(n.id);
        QVector<NodeId> queue{r.root};
        for (qsizetype i = 0; i < queue.size(); ++i)
            queue += children.value(queue[i]);
        total += queue.size();
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
    if (last)
    {
        m_results->refresh(last);
        setPage(1);
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
        if (!m_closeWhenIdle &&
            QMessageBox::question(
                this, text("ui.work_is_running"),
                text("ui.cancel_active_computation_export_and_close_after_workers_fin")) ==
                QMessageBox::Yes)
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
