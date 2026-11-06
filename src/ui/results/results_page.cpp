// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QScreen>
#include "ui/common/error_presenter.h"
#include <QApplication>
#include <QClipboard>
#include "io/exports/export_path.h"
#include "ui/exports/figure_export_dialog.h"
#include "ui/settings/theme_manager.h"
#include "ui/results/results_page.h"
#include "ui/common/source_provenance.h"
#include "ui/results/record_directory_panel.h"
#include <QStyle>
#include "ui/common/presentation.h"
#include "model/workflow_codec.h"
#include "ui/panels/plot_style_editor.h"
#include "ui/panels/result_export_panel.h"
#include <QTabWidget>
#include <QTabBar>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ui/common/section_splitter.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QFileInfo>
#include <QFileDialog>
#include <QColorDialog>
#include <QMenu>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QDialog>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QToolButton>
#include <algorithm>
namespace aster::ui
{
ResultsPage::ResultsPage(TaskRegistry registry, QWidget *p)
    : QWidget(p), m_registry(std::move(registry)), m_loader(new ResultLoader(this)),
      m_exporter(new ExportController(this)), m_figureExporter(new FigureExportController(this))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 10);
    outer->setSpacing(10);
    auto *bar = new QHBoxLayout;
    m_title = new QLabel(text("ui.results_workspace"));
    m_title->setObjectName("PageTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(0);
    m_title->setWordWrap(true);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(m_title, 1);
    auto *reload = new QPushButton(text("ui.refresh"));
    bar->addWidget(reload);
    connect(reload, &QPushButton::clicked, this, [this] { refresh(m_recordId); });
    outer->addLayout(bar);
    m_splitter = new QSplitter;
    m_splitter->setObjectName("resultsSplitter");
    m_splitter->setChildrenCollapsible(false);
    outer->addWidget(m_splitter, 1);
    m_directory = new RecordDirectoryPanel;
    m_splitter->addWidget(m_directory);
    connect(m_directory, &RecordDirectoryPanel::recordActivated, this, &ResultsPage::openRecord);
    connect(m_directory, &RecordDirectoryPanel::message, this, &ResultsPage::message);
    connect(m_directory, &RecordDirectoryPanel::busyChanged, this, [this](bool busy) {
        m_directoryBusy = busy; emit databaseBusyChanged(busy);
    });
    connect(m_directory, &RecordDirectoryPanel::mutationBusyChanged, this, [this](bool busy) {
        m_directoryMutating = busy; emit databaseMutationBusyChanged(busy);
    });
    connect(m_directory, &RecordDirectoryPanel::metadataEdited, this, [this](qint64 id) {
        if (id == m_recordId) openRecord(id);
    });
    connect(m_directory, &RecordDirectoryPanel::recordsRemoved, this, [this](const RecordIdentities &records) {
        for (auto it = records.cbegin(); it != records.cend(); ++it)
            m_workspace->forgetRecord(QFileInfo(m_path).absoluteFilePath(), it.key());
        if (records.contains(m_recordId) || records.contains(m_requestedRecordId))
        {
            m_loader->invalidate(); m_record.reset(); m_recordId = m_requestedRecordId = 0;
            m_nodeIds.clear(); m_steps->setRowCount(0); m_ports->clear(); m_details->clear();
            m_stepNotice->hide(); m_errorDetailsButton->hide(); m_focusReference = {};
            m_selectedNode = {}; m_title->setText(text("ui.results_workspace"));
            updateOutputActions();
            syncPlotSelection(m_workspace->currentReference());
        }
    });
    auto *center = new SectionSplitter(Qt::Vertical);
    center->setChildrenCollapsible(false);
    center->setObjectName("plotStepsSplitter");
    auto *chart = new QWidget;
    chart->setObjectName("Panel");
    chart->setProperty("panelRole", "plot");
    chart->setMinimumSize(260, 220);
    auto *cl = new QVBoxLayout(chart);
    cl->setContentsMargins(14, 10, 12, 8);
    cl->setSpacing(6);
    auto *chartBar = new QHBoxLayout;
    m_subtitle = new QLabel(text("ui.plots_and_numerical_output"));
    m_subtitle->setObjectName("SectionTitle");
    m_subtitle->setMinimumWidth(0);
    m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    chartBar->addWidget(m_subtitle, 1);
    auto *fit = new QPushButton(text("ui.fit"));
    auto *clear = new QPushButton(text("ui.clear"));
    clear->setObjectName("clearPlot");
    chartBar->addWidget(fit);
    chartBar->addWidget(clear);
    cl->addLayout(chartBar);
    m_stepNotice = new QLabel;
    m_stepNotice->setObjectName("StepError");
    m_stepNotice->setWordWrap(true);
    m_stepNotice->setTextFormat(Qt::PlainText);
    m_stepNotice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stepNotice->hide();
    auto *noticeBar = new QHBoxLayout;
    noticeBar->addWidget(m_stepNotice, 1);
    m_errorDetailsButton = new QPushButton(text("diagnostic.view_details"));
    m_errorDetailsButton->setObjectName("viewErrorDetails");
    m_errorDetailsButton->hide();
    noticeBar->addWidget(m_errorDetailsButton, 0, Qt::AlignTop);
    cl->addLayout(noticeBar);
    connect(m_errorDetailsButton, &QPushButton::clicked, this, &ResultsPage::showErrorDetails);
    m_workspace = new PlotWorkspace;
    m_plot = m_workspace->currentView();
    connect(m_workspace, &PlotWorkspace::selectionChanged, this, &ResultsPage::syncPlotSelection);
    cl->addWidget(m_workspace, 1);
    connect(m_workspace, &PlotWorkspace::message, this, &ResultsPage::message);
    connect(fit, &QPushButton::clicked, this,
            [this]
            {
                if (m_plot)
                    m_plot->fit();
            });
    clear->setText(text("ui.remove_subplot"));
    connect(clear, &QPushButton::clicked, m_workspace, &PlotWorkspace::closeSubplot);
    auto *addPlot = new QToolButton;
    m_addPlot = addPlot;
    addPlot->setText(text("ui.add_plot"));
    addPlot->setPopupMode(QToolButton::InstantPopup);
    addPlot->setObjectName("addResultPlot");
    auto *addMenu = new QMenu(addPlot);
    addPlot->setMenu(addMenu);
    chartBar->addWidget(addPlot);
    for (auto target : {PlotWorkspace::Target::Subplot, PlotWorkspace::Target::Page,
                        PlotWorkspace::Target::Overlay})
    {
        auto label = target == PlotWorkspace::Target::Subplot ? text("ui.new_subplot")
                     : target == PlotWorkspace::Target::Page
                         ? text("ui.new_page")
                         : text("ui.overlay_on_selected_subplot");
        addMenu->addAction(label, this,
                           [this, target]
                           {
                               m_workspace->open(
                                   reference(m_selectedNode, m_ports->currentData().toString()),
                                   target);
                           });
    }
    center->addWidget(chart);
    auto *stepsPanel = new QWidget;
    stepsPanel->setObjectName("Panel");
    stepsPanel->setProperty("panelRole", "activity");
    stepsPanel->setMinimumHeight(120);
    auto *sl = new QVBoxLayout(stepsPanel);
    sl->setContentsMargins(12, 10, 12, 8);
    auto *stepsTitle = new QLabel(text("ui.steps_in_this_execution"));
    stepsTitle->setObjectName("SectionTitle");
    sl->addWidget(stepsTitle);
    m_steps = new QTableWidget(0, 5);
    m_steps->setObjectName("resultSteps");
    m_steps->setHorizontalHeaderLabels({text("ui.status"), text("ui.task"),
                                        text("ui.outputs_input"), text("ui.plot"),
                                        text("ui.export_csv_short")});
    m_steps->verticalHeader()->hide();
    m_steps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_steps->setSelectionMode(QAbstractItemView::SingleSelection);
    m_steps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_steps->setShowGrid(false);
    m_steps->verticalHeader()->setDefaultSectionSize(48);
    m_steps->setColumnWidth(0, 65);
    m_steps->setColumnWidth(1, 145);
    m_steps->setColumnWidth(3, 76);
    m_steps->setColumnWidth(4, 100);
    m_steps->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    sl->addWidget(m_steps, 1);
    center->addWidget(stepsPanel);
    center->setSizes({580, 180});
    m_splitter->addWidget(center);
    connect(m_steps, &QTableWidget::cellClicked, this, [this](int row, int) { selectStep(row); });
    auto *right = new QWidget;
    right->setObjectName("Panel");
    right->setProperty("panelRole", "inspector");
    right->setMinimumWidth(275);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(10, 10, 10, 10);
    rl->setSpacing(10);
    auto *detailsTabs = new QTabWidget;
    detailsTabs->setObjectName("resultInspectorTabs");
    detailsTabs->setDocumentMode(true);
    detailsTabs->tabBar()->setObjectName("inspectorPrimaryNav");
    detailsTabs->tabBar()->setExpanding(true);
    detailsTabs->tabBar()->setUsesScrollButtons(false);
    auto *stylePage = new QWidget;
    auto *styleLayout = new QVBoxLayout(stylePage);
    styleLayout->setContentsMargins(0, 8, 0, 0);
    auto *target = new QFrame;
    target->setObjectName("inspectorTarget");
    target->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *targetLayout = new QVBoxLayout(target);
    targetLayout->setContentsMargins(10, 8, 10, 8);
    targetLayout->setSpacing(3);
    auto *targetTitle = new QLabel(text("ui.editing_target"));
    targetTitle->setObjectName("Muted");
    targetLayout->addWidget(targetTitle);
    m_plotTarget = new QLabel;
    m_plotTarget->setObjectName("activeStyleTarget");
    m_plotTarget->setTextFormat(Qt::PlainText);
    m_plotTarget->setWordWrap(true);
    m_plotTarget->setMinimumWidth(0);
    m_plotTarget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    targetLayout->addWidget(m_plotTarget);
    styleLayout->addWidget(target);
    m_styleEditor = new PlotStyleEditor(m_plot);
    auto *styleEditor = m_styleEditor;
    styleLayout->addWidget(styleEditor, 1);
    styleLayout->addStretch();
    detailsTabs->addTab(stylePage, text("ui.inspector_plot"));
    detailsTabs->setTabToolTip(0, text("ui.appearance"));
    connect(m_workspace, &PlotWorkspace::currentViewChanged, styleEditor,
            &PlotStyleEditor::setView);
    m_exportPanel = new ResultExportPanel;
    detailsTabs->addTab(m_exportPanel, text("ui.export_tab"));
    m_ports = m_exportPanel->ports();
    connect(m_exportPanel, &ResultExportPanel::outputActivated, this, &ResultsPage::showPort);
    connect(m_ports, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { updateOutputActions(); });
    connect(m_exportPanel, &ResultExportPanel::cancelRequested, this, &ResultsPage::cancelExports);
    connect(m_exporter, &ExportController::progressChanged, m_exportPanel,
            &ResultExportPanel::setProgress);
    const auto updateExportBusy = [this]
    {
        const bool busy = exportBusy();
        m_exportPanel->setBusy(busy, m_figureExporter->busy());
        updateOutputActions();
        emit exportBusyChanged(busy);
    };
    connect(m_exporter, &ExportController::busyChanged, this, updateExportBusy);
    connect(m_figureExporter, &FigureExportController::busyChanged, this, updateExportBusy);
    const auto finished = [this](QString path, QString error, bool cancelled)
    {
        const auto status = cancelled ? text("ui.export_cancelled")
                          : !error.isEmpty() ? error : text("ui.exported_local") + path;
        m_exportPanel->setStatus(status);
        emit message(status);
    };
    connect(m_figureExporter, &FigureExportController::finished, this, finished);
    connect(m_exporter, &ExportController::finished, this, finished);
    connect(m_workspace, &PlotWorkspace::outputReady, this, &ResultsPage::updateOutputActions);
    connect(m_workspace, &PlotWorkspace::currentViewChanged, this,
            [this](PlotView *view)
            {
                m_plot = view;
                updateOutputActions();
            });
    connect(m_exportPanel, &ResultExportPanel::exportDataRequested, this,
            [this]
            {
                // Freeze the selected step output, never a previously opened plot.
                const auto ref = selectedOutput();
                if (!ref.valid() || exportBusy()) return;
                const auto name = QString("ID_%1_%2_%3.csv")
                    .arg(ref.recordId)
                    .arg(ref.nodeId.toString(QUuid::WithoutBraces).left(8), ref.port);
                auto path = QFileDialog::getSaveFileName(this, text("ui.export_numerical_output"),
                                                         name, "CSV (*.csv)");
                if (path.isEmpty()) return;
                if (QFileInfo(path).suffix().isEmpty()) path += ".csv";
                if (conflictsWithDatabase(path, {m_path, ref.databasePath}))
                {
                    emit message(text("ui.choose_an_export_file_other_than_the_database"));
                    return;
                }
                m_exporter->startOutput(ref, path);
            });
    connect(m_exportPanel, &ResultExportPanel::exportFigureRequested, this,
            [this]
            {
                if (exportBusy() || !m_plot || !m_plot->hasPlot()) return;
                FigureExportDialog dialog(m_workspace->subplotCount() > 1, this);
                if (dialog.exec() != QDialog::Accepted) return;
                auto options = dialog.options();
                const auto cells = m_workspace->figureCells(dialog.wholePage());
                const auto ref = m_workspace->currentReference();
                if (cells.isEmpty())
                {
                    emit message(text("ui.wait_for_plots_to_load_use_csv_for_tables"));
                    return;
                }
                const bool pdf = options.format == FigureOptions::Format::Pdf;
                options.path = QFileDialog::getSaveFileName(
                    this, text("ui.export_figure"),
                    QString("ID_%1.%2").arg(ref.recordId).arg(pdf ? "pdf" : "png"),
                    pdf ? "PDF (*.pdf)" : "PNG (*.png)");
                if (options.path.isEmpty()) return;
                if (QFileInfo(options.path).suffix().isEmpty())
                    options.path += pdf ? ".pdf" : ".png";
                m_figureExporter->start(cells, options);
            });
    connect(m_exportPanel, &ResultExportPanel::reuseRequested, this,
            [this]
            {
                const auto ref = selectedOutput();
                if (ref.valid()) emit sourceRequested(ref);
            });
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_details = new QLabel;
    m_details->setObjectName("Surface");
    m_details->setWordWrap(true);
    m_details->setAlignment(Qt::AlignTop);
    m_details->setTextFormat(Qt::RichText);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_details->setMinimumWidth(0);
    m_details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    scroll->setWidget(m_details);
    detailsTabs->addTab(scroll, text("ui.inspector_sources"));
    detailsTabs->setTabToolTip(2, text("ui.provenance"));
    rl->addWidget(detailsTabs, 1);
    updateOutputActions();
    m_splitter->addWidget(right);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({220, 900, 310});
    connect(m_loader, &ResultLoader::ready, this, &ResultsPage::showRecord);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this]
    {
        if (!m_record)
            return;
        for (int i = 0; i < m_nodeIds.size(); ++i)
        {
            if (auto *item = m_steps->item(i, 0))
                item->setForeground(stateColor(m_record->run.nodes.value(m_nodeIds[i]).state));
            if (auto *item = m_steps->item(i, 2))
                if (item->data(Qt::UserRole).toBool())
                    item->setBackground(themeColor("selection"));
        }
        const auto port = m_ports->currentData();
        selectStep(m_nodeIds.indexOf(m_selectedNode), false);
        m_ports->setCurrentIndex(m_ports->findData(port));
    });
}
bool ResultsPage::startCsvExport(const QString &path)
{
    if (conflictsWithDatabase(path, {m_path}))
        return false;
    const auto ref = selectedOutput();
    if (ref.valid())
        return m_exporter->startOutput(ref, path);
    // Keep the standalone in-memory API available, but never fall back to an
    // older visible plot when a database step has no output.
    if (!m_record && m_recordId == 0 && !m_workspace->currentReference().valid() && m_plot)
        return m_exporter->start(m_plot->currentData(), path);
    return false;
}
int ResultsPage::recordCount() const
{
    return m_directory->recordCount();
}
void ResultsPage::setDatabase(const QString &p)
{
    m_loader->invalidate();
    m_path = p;
    m_record.reset();
    m_recordId = 0;
    m_requestedRecordId = 0;
    m_nodeIds.clear();
    m_steps->setRowCount(0);
    m_ports->clear();
    m_details->clear();
    m_stepNotice->hide();
    m_errorDetailsButton->hide();
    m_title->setText(text("ui.results_workspace"));
    updateOutputActions();
    m_workspace->reset();
    m_directory->setDatabase(p);
}
void ResultsPage::setDatabaseMutationsEnabled(bool enabled)
{
    m_directory->setMutationsEnabled(enabled);
}
void ResultsPage::exportNode(NodeId id, QString port)
{
    if (!m_record || exportBusy())
    {
        emit message(text("ui.wait_for_the_current_export"));
        return;
    }
    const auto ref = reference(id, port);
    if (!ref.valid())
        return;
    const auto path =
        QFileDialog::getSaveFileName(this, text("ui.export_node_values"),
                                     QString("ID_%1_%2_%3.csv")
                                         .arg(m_recordId)
                                         .arg(id.toString(QUuid::WithoutBraces).left(8), port),
                                     "CSV (*.csv)");
    if (path.isEmpty())
        return;
    if (conflictsWithDatabase(path, {m_path}))
    {
        emit message(text("ui.cannot_overwrite_database"));
        return;
    }
    m_exporter->startOutput(ref, path);
}
void ResultsPage::refresh(qint64 selected)
{
    m_directory->refresh();
    if (selected)
        openRecord(selected);
}
void ResultsPage::openRecord(qint64 id)
{
    m_stepNotice->hide();
    m_focusReference = {};
    m_requestedRecordId = id;
    // Keep open plot pages while switching the result directory.
    m_ports->clear();
    m_steps->setRowCount(0);
    m_details->clear();
    m_nodeIds.clear();
    m_selectedNode = {};
    m_record.reset();
    m_recordId = 0;
    m_directory->setCurrentRecord(id);
    m_loader->request(m_path, id, m_directory->filter());
}
void ResultsPage::showRecord(qint64 id, std::shared_ptr<const LoadResult> result)
{
    if (!result->ok())
    {
        // Loading a missing record must not destroy existing figures.
        emit message(result->error);
        return;
    }
    m_record = std::move(result);
    m_recordId = id;
    const auto &run = m_record->run;
    auto definition = workflowFromJson(run.workflowSnapshot).definition;
    m_title->setText(QString("ID %1  ·  %2")
                         .arg(id)
                         .arg(m_record->displayName.isEmpty()
                                  ? QFileInfo(run.sourceLabel).fileName()
                                  : m_record->displayName));
    m_steps->setRowCount(definition.nodes.size());
    m_nodeIds.clear();
    int preferred = 0;
    for (int i = 0; i < definition.nodes.size(); ++i)
    {
        const auto &n = definition.nodes[i];
        m_nodeIds.push_back(n.id);
        const auto node = run.nodes.value(n.id);
        const auto *registration = m_registry.find(n.method);
        auto *state = new QTableWidgetItem(stateLabel(node.state));
        state->setForeground(stateColor(node.state));
        m_steps->setItem(i, 0, state);
        m_steps->setItem(i, 1,
                         new QTableWidgetItem(
                             QString::number(i + 1) + " · " +
                             (registration ? methodLabel(registration->descriptor) : n.method)));
        auto *detail = new QTableWidgetItem(
            node.state == NodeState::Succeeded
                ? QString::number(m_record->outputs.value(n.id).size()) + text("ui.outputs")
                : resultError(node));
        if(node.state==NodeState::Succeeded && node.report.contains("peak_count"))
            detail->setText(detail->text()+QString(text("ui.detected_peaks_count")).arg(node.report["peak_count"].toInt()));
        QString parentText = text("ui.source");
        if (n.input)
        {
            for (int j = 0; j < definition.nodes.size(); ++j)
                if (definition.nodes[j].id == n.input->parent)
                    parentText = QString(text("ui.from_step_1_2"))
                                     .arg(j + 1)
                                     .arg(portLabel(n.input->output));
        }
        QStringList matches;
        for (const auto &peak : m_record->matchingPeaks)
            if (peak.node == n.id)
                matches << QString::number(peak.frequency, 'g', 8) + " " + peak.unit;
        detail->setText(detail->text() + "\n" + parentText);
        QStringList warnings;
        for (const auto &warning : node.warnings)
            warnings << translatedLabel(warning, warning);
        detail->setToolTip(presentError(node).details + "\n" + warnings.join("\n") + "\n" +
                           matches.join("\n"));
        if (!matches.isEmpty())
        {
            detail->setData(Qt::UserRole, true);
            detail->setBackground(themeColor("selection"));
            detail->setText(detail->text() + text("ui.peak_match"));
        }
        auto *plotButton = new QToolButton;
        plotButton->setText(text("ui.plot"));
        plotButton->setObjectName("plotNode_" + QString::number(i));
        plotButton->setPopupMode(QToolButton::MenuButtonPopup);
        auto *plotMenu = new QMenu(plotButton);
        plotButton->setMenu(plotMenu);
        connect(plotButton, &QToolButton::clicked, this, [this, i] { selectStep(i); });
        auto *saveButton = new QToolButton;
        saveButton->setText(text("ui.export_csv_short"));
        saveButton->setToolTip(text("ui.export_node_values_database_results_are_already_saved"));
        saveButton->setObjectName("saveNode_" + QString::number(i));
        saveButton->setPopupMode(QToolButton::InstantPopup);
        auto *saveMenu = new QMenu(saveButton);
        saveButton->setMenu(saveMenu);
        for (const auto &output : m_record->outputs.value(n.id))
        {
            const auto key = output.port;
            const auto outputName = registration ? portLabel(registration->descriptor, key) : portLabel(key);
            auto *outputMenu = plotMenu->addMenu(outputName);
            for (auto target : {PlotWorkspace::Target::Current, PlotWorkspace::Target::Subplot,
                                PlotWorkspace::Target::Page, PlotWorkspace::Target::Overlay})
            {
                QString label = target == PlotWorkspace::Target::Current
                                    ? text("ui.current_subplot")
                                : target == PlotWorkspace::Target::Subplot ? text("ui.new_subplot")
                                : target == PlotWorkspace::Target::Page
                                    ? text("ui.new_page")
                                    : text("ui.overlay_on_selected_subplot");
                outputMenu->addAction(label, this,
                                      [this, i, key, target]
                                      {
                                          selectStep(i, false);
                                          m_ports->setCurrentIndex(m_ports->findData(key));
                                          auto ref = reference(m_selectedNode, key);
                                          m_workspace->open(ref, target);
                                      });
            }
            outputMenu->addSeparator();
            outputMenu->addAction(text("ui.use_as_source"), this, [this, nodeId = n.id, key]
                                  { emit sourceRequested(reference(nodeId, key)); });
            saveMenu->addAction(outputName + " → CSV", this,
                                [this, nodeId = n.id, key] { exportNode(nodeId, key); });
        }
        plotButton->setEnabled(!m_record->outputs.value(n.id).isEmpty());
        saveButton->setEnabled(!m_record->outputs.value(n.id).isEmpty());
        m_steps->setCellWidget(i, 3, plotButton);
        m_steps->setCellWidget(i, 4, saveButton);
        m_steps->setItem(i, 2, detail);
        if (n.method == "mtm" && node.state == NodeState::Succeeded)
            preferred = i;
    }
    if (!m_nodeIds.isEmpty())
    {
        if (m_focusReference.valid() && m_focusReference.recordId == id)
        {
            auto focus = m_focusReference;
            m_focusReference = {};
            selectStep(m_nodeIds.indexOf(focus.nodeId), false);
            m_ports->setCurrentIndex(m_ports->findData(focus.port));
        }
        else
            selectStep(preferred);
    }
    emit recordLoaded(id);
}
void ResultsPage::selectStep(int row, bool display)
{
    if (!m_record || row < 0 || row >= m_nodeIds.size())
        return;
    m_steps->selectRow(row);
    m_selectedNode = m_nodeIds[row];
    const auto &run = m_record->run;
    const auto node = run.nodes.value(m_selectedNode);
    m_errorDetailsButton->setVisible(node.state == NodeState::Failed ||
                                     node.state == NodeState::Blocked ||
                                     node.state == NodeState::Cancelled);
    const auto definition = workflowFromJson(run.workflowSnapshot).definition;
    NodeDefinition selected;
    for (const auto &n : definition.nodes)
        if (n.id == m_selectedNode)
        {
            selected = n;
            break;
        }
    const auto *registration = m_registry.find(selected.method);
    const bool globalSummary = selected.method == "ar1_global" && node.state == NodeState::Succeeded;
    if (m_stepNotice->property("globalTestSummary").toBool() != globalSummary)
    {
        m_stepNotice->setProperty("globalTestSummary", globalSummary);
        m_stepNotice->style()->unpolish(m_stepNotice);
        m_stepNotice->style()->polish(m_stepNotice);
    }
    m_subtitle->setText(registration ? methodLabel(registration->descriptor) : selected.method);
    m_ports->clear();
    for (const auto &output : m_record->outputs.value(m_selectedNode))
        m_ports->addItem(registration ? portLabel(registration->descriptor, output.port)
                                      : portLabel(output.port), output.port);
    if (registration)
    {
        int i = m_ports->findData(registration->descriptor.defaultOutput);
        if (i >= 0)
            m_ports->setCurrentIndex(i);
    }
    QString info = "<p><b>" + stateLabel(node.state).toHtmlEscaped() + "</b><br>" +
                   run.startedAt.toLocalTime().toString("yyyy-MM-dd hh:mm:ss") + "</p>";
    if (!node.error.isEmpty())
        info += "<p style='color:" + themeColor("error").name() + "'>" +
                resultError(node).toHtmlEscaped() + "</p>";
    for (const auto &warning : node.warnings)
        info += "<p style='color:" + themeColor("warning").name() + "'>" + translatedLabel(warning,warning).toHtmlEscaped() +
                "</p>";
    for (auto it = selected.parameters.begin(); it != selected.parameters.end(); ++it)
    {
        QString name = it.key();
        if (registration)
            for (const auto &p : registration->descriptor.parameters)
                if (p.key == it.key())
                {
                    name = parameterLabel(p);
                    break;
                }
        QString value = it->isBool()     ? (it->toBool() ? "true" : "false")
                        : it->isDouble() ? QString::number(it->toDouble(), 'g', 10)
                                         : it->toString();
        info += "<p>" + name.toHtmlEscaped() + "<br><b>" + value.toHtmlEscaped() + "</b></p>";
    }
    info += "<p>" + text("ui.source_f59a85") + run.sourceLabel.toHtmlEscaped() + "</p>";
    if (!m_record->notes.isEmpty())
        info += "<p>" + m_record->notes.toHtmlEscaped() + "</p>";
    if (selected.method == "file_source")
    {
        auto effective = node.report;
        effective.remove("diagnostic");
        effective.remove("input_error");
        info += "<p>" + text("ui.effective_file_configuration") + "<br>" +
                QString::fromUtf8(QJsonDocument(effective).toJson(QJsonDocument::Indented))
                    .toHtmlEscaped()
                    .replace("\n", "<br>") +
                "</p>";
    }
    info += sourceProvenanceHtml(node.report);
    m_details->setText(info);
    updateOutputActions();
    if (m_record->outputs.value(m_selectedNode).isEmpty())
    {
        QString explanation = resultError(node);
        auto parent = selected.input;
        while (node.state == NodeState::Blocked && parent)
        {
            const auto upstream = run.nodes.value(parent->parent);
            auto found = std::find_if(definition.nodes.cbegin(), definition.nodes.cend(),
                                      [&](const NodeDefinition &n) { return n.id == parent->parent; });
            if (found == definition.nodes.cend()) break;
            if (upstream.state == NodeState::Failed)
            {
                const auto *entry = m_registry.find(found->method);
                explanation += "\n" + text("ui.first_failure_summary")
                    .arg(entry ? methodLabel(entry->descriptor) : found->method)
                    .arg(resultError(upstream));
                break;
            }
            parent = found->input;
        }
        auto notice = text("ui.no_step_output") + "\n" + explanation;
        if (m_plot && m_plot->hasOutput())
            notice += "\n" + text("ui.existing_plots_retained");
        m_stepNotice->setText(notice);
        m_stepNotice->show();
        if (m_plot && !m_plot->hasOutput()) m_plot->clear(notice);
        return;
    }
    m_stepNotice->hide();
    if(selected.method=="ar1_global" && node.state==NodeState::Succeeded){
        const auto report=node.report;
        const auto status=text(!report.value("global_test_available").toBool()?"ui.ar1_inconclusive":
            report.value("reject_global_null").toBool()?"ui.ar1_reject":"ui.ar1_not_reject");
        m_stepNotice->setText(text("ui.ar1_summary").arg(QString::number(report.value("global_p").toDouble(),'g',5)).arg(status));
        m_stepNotice->show();
    }
    if (display)
        showPort();
}
QString ResultsPage::selectedErrorDetails() const
{
    if (!m_record || !m_record->run.nodes.contains(m_selectedNode)) return {};
    const auto &run = m_record->run;
    const auto node = run.nodes.value(m_selectedNode);
    const auto heading = QString("ID %1 · %2\n").arg(m_recordId).arg(m_subtitle->text()) +
                         text("diagnostic.source") + ": " + run.sourceLabel + "\n";
    QString details = heading + presentError(node).details;
    if (node.state != NodeState::Blocked) return details;
    // Resolve once to the nearest actual failure, including pre-diagnostic records.
    const auto definition = workflowFromJson(run.workflowSnapshot).definition;
    auto current = m_selectedNode;
    QSet<NodeId> visited;
    while (!visited.contains(current))
    {
        visited.insert(current);
        const auto found = std::find_if(definition.nodes.begin(), definition.nodes.end(),
            [&](const NodeDefinition &value) { return value.id == current; });
        if (found == definition.nodes.end() || !found->input) break;
        current = found->input->parent;
        const auto upstream = run.nodes.value(current);
        if (upstream.state == NodeState::Failed || upstream.state == NodeState::Cancelled)
        {
            details = heading + "\n" + text("diagnostic.upstream_cause") + "\n" +
                      presentError(upstream).details + "\n\n" +
                      text("diagnostic.current_step") + "\n" + presentError(node).details;
            break;
        }
    }
    return details;
}
void ResultsPage::showErrorDetails()
{
    const auto details = selectedErrorDetails();
    if (details.isEmpty() || !m_record) return;
    const auto presentation = presentError(m_record->run.nodes.value(m_selectedNode));
    QDialog dialog(this);
    dialog.setWindowTitle(text("diagnostic.details_title"));
    dialog.setObjectName("errorDetailsDialog");
    dialog.setSizeGripEnabled(true);
    auto *layout = new QVBoxLayout(&dialog);
    auto *summary = new QLabel(presentation.summary);
    summary->setObjectName("SectionTitle");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *suggestion = new QLabel(presentation.suggestion);
    suggestion->setObjectName("Muted");
    suggestion->setTextFormat(Qt::PlainText);
    suggestion->setWordWrap(true);
    suggestion->setVisible(!presentation.suggestion.isEmpty());
    layout->addWidget(suggestion);
    auto *body = new QPlainTextEdit(details);
    body->setObjectName("diagnosticText");
    body->setReadOnly(true);
    layout->addWidget(body, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *copy = buttons->addButton(text("diagnostic.copy_details"), QDialogButtonBox::ActionRole);
    copy->setObjectName("copyErrorDetails");
    connect(copy, &QPushButton::clicked, &dialog,
            [details] { QApplication::clipboard()->setText(details); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    const auto available = screen()->availableGeometry().size();
    dialog.resize(qMin(680, available.width() * 9 / 10),
                  qMin(520, available.height() * 9 / 10));
    dialog.exec();
}
void ResultsPage::syncPlotSelection(const OutputReference &ref)
{
    if (!ref.valid() || ref.databasePath != QFileInfo(m_path).absoluteFilePath())
        return;
    if (m_record && m_recordId == ref.recordId)
    {
        selectStep(m_nodeIds.indexOf(ref.nodeId), false);
        m_ports->setCurrentIndex(m_ports->findData(ref.port));
    }
    else
    {
        if (m_requestedRecordId != ref.recordId || m_record)
            openRecord(ref.recordId);
        m_focusReference = ref;
    }
}
OutputReference ResultsPage::reference(NodeId node, const QString &port) const
{
    if (!m_record)
        return {};
    for (const auto &out : m_record->outputs.value(node))
        if (out.port == port)
            return {QFileInfo(m_path).absoluteFilePath(),
                    m_recordId,
                    m_record->run.runId,
                    node,
                    port,
                    out.kind};
    return {};
}
OutputReference ResultsPage::selectedOutput() const
{
    return reference(m_selectedNode, m_ports->currentData().toString());
}
void ResultsPage::updateOutputActions()
{
    const auto selected = selectedOutput();
    const auto plotted = m_workspace->currentReference();
    const bool hasFigure = m_plot && m_plot->hasPlot() && plotted.valid();
    const auto selectedLabel = m_record
        ? QString("ID %1 · %2").arg(m_recordId).arg(m_subtitle->text())
        : text("ui.select_step_for_export");
    const auto plotLabel = hasFigure
        ? text("ui.active_figure_target").arg(plotted.recordId).arg(portLabel(plotted.port))
        : text("ui.no_active_figure");
    m_plotTarget->setText(hasFigure
        ? QString("ID %1 · %2").arg(plotted.recordId).arg(portLabel(plotted.port))
        : text("ui.no_active_figure"));
    m_styleEditor->setVisible(hasFigure);
    m_exportPanel->setTargets(selectedLabel + (selected.valid() ? QString() :
                                "\n" + text("ui.no_step_output")), selected.valid(),
                              plotLabel, hasFigure);
    m_addPlot->setEnabled(selected.valid());
}
void ResultsPage::showPort()
{
    updateOutputActions();
    const auto ref = selectedOutput();
    if (ref.valid()) m_workspace->open(ref);
}
} // namespace aster::ui
