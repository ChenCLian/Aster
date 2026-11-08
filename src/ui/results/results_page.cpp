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
#include "ui/results/plot_recipe.h"
#include "ui/common/source_provenance.h"
#include "ui/results/record_directory_panel.h"
#include <QStyle>
#include "ui/common/presentation.h"
#include "ui/common/popup_placement.h"
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
#include <QTableView>
#include <QStandardItemModel>
#include <QListWidget>
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
#include <QShowEvent>
#include <functional>
#include <algorithm>
namespace aster::ui
{
namespace
{
class ResultSectionSplitter final : public SectionSplitter
{
  public:
    ResultSectionSplitter() : SectionSplitter(Qt::Vertical) {}
    std::function<int()> preferredOutputHeight;
  protected:
    void showEvent(QShowEvent *event) override
    {
        SectionSplitter::showEvent(event);
        if (!m_initialized && count() == 2 && preferredOutputHeight)
        {
            m_initialized = true;
            const int available = height() - handleWidth();
            // Show several output rows without making the plot a narrow strip
            // on a short display. Subsequent sizes belong to the user.
            const int bottom = std::min(preferredOutputHeight(),
                                        std::max(220, std::min(available * 45 / 100,
                                                               available - widget(0)->minimumHeight())));
            setSizes({std::max(0, available - bottom), bottom});
        }
    }
  private:
    bool m_initialized = false;
};
}

ResultsPage::ResultsPage(TaskRegistry registry, QWidget *p)
    : QWidget(p), m_registry(std::move(registry)), m_loader(new ResultLoader(this)),
      m_exporter(new ExportController(this)), m_figureExporter(new FigureExportController(this))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 6, 10, 8);
    outer->setSpacing(6);
    auto *bar = new QHBoxLayout;
    m_title = new QLabel(text("ui.results_workspace"));
    m_title->setObjectName("PageTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(0);
    m_title->setWordWrap(true);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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
            emit recordChanging();
            m_checked.clear(); m_outputModel->removeRows(0, m_outputModel->rowCount());
            m_loader->invalidate(); m_record.reset(); m_recordId = m_requestedRecordId = 0;
            m_nodeIds.clear(); m_primaryPorts.clear();
            m_steps->setRowCount(0); m_ports->clear(); m_details->clear();
            m_stepNotice->hide(); m_errorDetailsButton->hide();
            m_selectedNode = {}; m_title->setText(text("ui.results_workspace"));
            updateOutputActions();
            syncPlotSelection(m_workspace->currentReference());
        }
    });
    auto *center = new ResultSectionSplitter;
    center->setChildrenCollapsible(false);
    center->setObjectName("plotStepsSplitter");
    auto *chart = new QWidget;
    chart->setObjectName("Panel");
    chart->setProperty("panelRole", "plot");
    chart->setMinimumSize(260, 220);
    auto *cl = new QVBoxLayout(chart);
    cl->setContentsMargins(10, 6, 10, 6);
    cl->setSpacing(6);
    auto *chartBar = new QHBoxLayout;
    m_subtitle = new QLabel(text("ui.plots_and_numerical_output"));
    m_subtitle->setObjectName("SectionTitle");
    m_subtitle->setMinimumWidth(0);
    m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *figureTitle = new QLabel(text("ui.plot_pages"));
    figureTitle->setObjectName("SectionTitle");
    chartBar->addWidget(figureTitle, 1);
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
    center->addWidget(chart);
    auto *stepsPanel = new QWidget;
    stepsPanel->setObjectName("Panel");
    stepsPanel->setProperty("panelRole", "activity");
    stepsPanel->setMinimumHeight(220);
    auto *sl = new QVBoxLayout(stepsPanel);
    sl->setContentsMargins(10, 6, 10, 6);
    sl->setSpacing(4);
    auto *browserHeader = new QHBoxLayout;
    auto *stepsTitle = new QLabel(text("ui.steps_in_this_execution"));
    stepsTitle->setObjectName("SectionTitle");
    browserHeader->addWidget(stepsTitle);
    browserHeader->addStretch();
    m_checkedButton = new QToolButton;
    m_checkedButton->setObjectName("inspectCheckedOutputs");
    m_checkedButton->setToolTip(text("ui.review_checked_help"));
    browserHeader->addWidget(m_checkedButton);
    auto *collapse = new QToolButton;
    collapse->setObjectName("toggleResultOutputs");
    collapse->setCheckable(true);
    collapse->setChecked(true);
    collapse->setText(text("ui.collapse_outputs"));
    browserHeader->addWidget(collapse);
    sl->addLayout(browserHeader);
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(4);
    auto *browser = new SectionSplitter(Qt::Horizontal);
    browser->setObjectName("stepOutputSplitter");
    browser->setChildrenCollapsible(false);
    m_steps = new QTableWidget(0, 3);
    m_steps->setObjectName("resultSteps");
    m_steps->setHorizontalHeaderLabels({text("ui.status"), text("ui.task"), text("ui.outputs_input")});
    m_steps->verticalHeader()->hide();
    m_steps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_steps->setSelectionMode(QAbstractItemView::SingleSelection);
    m_steps->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_steps->setShowGrid(false);
    m_steps->setWordWrap(false);
    m_steps->verticalHeader()->setDefaultSectionSize(32);
    m_steps->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_steps->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_steps->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_steps->setMinimumWidth(180);
    browser->addWidget(m_steps);
    auto *outputsPanel = new QWidget;
    auto *outputsLayout = new QVBoxLayout(outputsPanel);
    outputsLayout->setSpacing(3);
    outputsLayout->setContentsMargins(0, 0, 0, 0);
    outputsLayout->addWidget(m_subtitle);
    m_outputs = new QTableView;
    m_outputs->setObjectName("resultOutputs");
    m_outputModel = new QStandardItemModel(this);
    m_outputModel->setHorizontalHeaderLabels({text("ui.output"), text("ui.output_kind")});
    m_outputs->setModel(m_outputModel);
    m_outputs->verticalHeader()->hide();
    m_outputs->verticalHeader()->setDefaultSectionSize(30);
    m_outputs->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_outputs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_outputs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outputs->setShowGrid(false);
    m_outputs->setWordWrap(false);
    m_outputs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_outputs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    outputsLayout->addWidget(m_outputs, 1);
    browser->addWidget(outputsPanel);
    browser->setSizes({340, 440});
    bodyLayout->addWidget(browser, 1);
    auto *actions = new QHBoxLayout;
    m_destination = new QLabel;
    m_destination->setObjectName("plotDestination");
    m_destination->setTextFormat(Qt::PlainText);
    m_destination->setWordWrap(true);
    m_destination->setMinimumWidth(0);
    m_destination->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    actions->addWidget(m_destination, 1);
    m_plotChecked = new QPushButton(text("ui.add_selected_subplots"));
    m_plotChecked->setObjectName("plotCheckedOutputs");
    actions->addWidget(m_plotChecked);
    m_overlayChecked = new QPushButton(text("ui.overlay_selected"));
    m_overlayChecked->setObjectName("overlayCheckedOutputs");
    actions->addWidget(m_overlayChecked);
    m_addPlot = new QToolButton;
    m_addPlot->setObjectName("addResultPlot");
    m_addPlot->setText(text("ui.more_plot_actions"));
    m_addPlot->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_addPlot);
    configureMenu(menu);
    m_pageAction = menu->addAction(text("ui.new_page"), this,
        [this] { plotChecked(PlotWorkspace::Target::Page); });
    m_replaceAction = menu->addAction(text("ui.replace_selected_subplot"), this,
        [this] { plotChecked(PlotWorkspace::Target::Replace); });
    menu->addAction(text("ui.plot_separately"), this,
        [this] { plotChecked(PlotWorkspace::Target::Separate); });
    m_addPlot->setMenu(menu);
    actions->addWidget(m_addPlot);
    bodyLayout->addLayout(actions);
    sl->addWidget(body, 1);
    connect(m_plotChecked, &QPushButton::clicked, this,
        [this] { plotChecked(PlotWorkspace::Target::Subplot); });
    connect(m_overlayChecked, &QPushButton::clicked, this,
        [this] { plotChecked(PlotWorkspace::Target::Overlay); });
    connect(m_checkedButton, &QToolButton::clicked, this, &ResultsPage::inspectChecked);
    connect(collapse, &QToolButton::toggled, this,
        [body, collapse, stepsPanel, center, browserHeader, sl](bool expanded) {
        if (!expanded) center->setProperty("expandedOutputSizes", QVariant::fromValue(center->sizes()));
        collapse->setText(text(expanded ? "ui.collapse_outputs" : "ui.expand_outputs"));
        body->setVisible(expanded);
        // Font metrics and theme padding, not a hard-coded 44 px: the latter
        // clipped Chinese text at common Windows scaling/font settings.
        const int collapsedHeight = browserHeader->sizeHint().height() +
            sl->contentsMargins().top() + sl->contentsMargins().bottom();
        stepsPanel->setMinimumHeight(expanded ? 220 : collapsedHeight);
        stepsPanel->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : collapsedHeight);
        if (expanded) center->setSizes(center->property("expandedOutputSizes").value<QList<int>>());
        else center->setSizes({std::max(0, center->height() - center->handleWidth() - collapsedHeight),
                               collapsedHeight});
    });
    connect(m_outputModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (item->column() != 0) return;
        const auto ref = item->data(Qt::UserRole).value<OutputReference>();
        if (!ref.valid()) return;
        if (item->checkState() == Qt::Checked) m_checked.insert(ref.key(), ref);
        else m_checked.remove(ref.key());
        updateOutputActions();
    });
    connect(m_outputs->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        [this](const QModelIndex &index) {
            if (index.isValid() && m_ports)
                m_ports->setCurrentIndex(m_ports->findData(m_outputModel->item(index.row(), 0)->data(Qt::UserRole).value<OutputReference>().port));
        });
    center->addWidget(stepsPanel);
    center->preferredOutputHeight = [this, sl, browserHeader, bodyLayout, outputsLayout, actions] {
        m_steps->ensurePolished(); m_outputs->ensurePolished();
        const int rows = 5 * std::max(m_steps->verticalHeader()->defaultSectionSize(),
                                      m_outputs->verticalHeader()->defaultSectionSize());
        const int tables = std::max(m_steps->horizontalHeader()->sizeHint().height(),
            m_subtitle->sizeHint().height() + outputsLayout->spacing() +
            m_outputs->horizontalHeader()->sizeHint().height());
        return sl->contentsMargins().top() + sl->contentsMargins().bottom() + sl->spacing() +
            browserHeader->sizeHint().height() + bodyLayout->spacing() + actions->sizeHint().height() +
            tables + rows + 2 * m_outputs->frameWidth() + 4;
    };
    center->setSizes({640, 340});
    center->setStretchFactor(0, 1);
    center->setStretchFactor(1, 0);
    m_splitter->addWidget(center);
    connect(m_steps->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        [this](const QModelIndex &index) { if (index.isValid()) selectStep(index.row(), true); });
    // Also reopen the default output when the already-current row is clicked.
    connect(m_steps, &QTableWidget::clicked, this, [this](const QModelIndex &) { showPort(); });
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
    
    connect(m_ports, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] {
                QSignalBlocker block(m_outputs->selectionModel());
                for (int row = 0; row < m_outputModel->rowCount(); ++row)
                    if (m_outputModel->item(row, 0)->data(Qt::UserRole).value<OutputReference>().port == m_ports->currentData().toString())
                        m_outputs->setCurrentIndex(m_outputModel->index(row, 0));
                updateOutputActions();
            });
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
    scroll->setObjectName("resultSourceDetailsContainer");
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
    emit recordChanging();
    m_loader->invalidate();
    m_checked.clear();
    m_outputModel->removeRows(0, m_outputModel->rowCount());
    m_path = p;
    m_record.reset();
    m_recordId = 0;
    m_requestedRecordId = 0;
    m_defaultPlotOnLoad = false;
    m_nodeIds.clear();
    m_primaryPorts.clear();
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
void ResultsPage::refresh(qint64 selected)
{
    m_directory->refresh();
    if (selected)
        openRecord(selected);
}
void ResultsPage::openRecord(qint64 id)
{
    emit recordChanging();
    emit message({});
    m_stepNotice->hide();
    m_checked.clear();
    m_outputModel->removeRows(0, m_outputModel->rowCount());
    m_requestedRecordId = id;
    // Keep open plot pages while switching the result directory.
    m_ports->clear();
    m_steps->setRowCount(0);
    m_details->clear();
    m_nodeIds.clear();
    m_primaryPorts.clear();
    m_selectedNode = {};
    m_record.reset();
    m_recordId = 0;
    m_directory->setCurrentRecord(id);
    updateOutputActions();
    m_defaultPlotOnLoad = true;
    m_loader->request(m_path, id, m_directory->filter());
}
void ResultsPage::showRecord(qint64 id, std::shared_ptr<const LoadResult> result)
{
    if (!result->ok())
    {
        // Loading a missing record must not destroy existing figures.
        emit message(translatedLabel(result->error, result->error));
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
    QSignalBlocker stepBlock(m_steps->selectionModel());
    m_steps->setRowCount(definition.nodes.size());
    m_nodeIds.clear();
    m_primaryPorts.clear();
    m_nodeMethods.clear();
    int preferred = -1;
    int fallback = 0;
    for (int i = 0; i < definition.nodes.size(); ++i)
    {
        const auto &n = definition.nodes[i];
        m_nodeIds.push_back(n.id);
        m_nodeMethods.insert(n.id, n.method);
        const auto node = run.nodes.value(n.id);
        const auto *registration = m_registry.find(n.method);
        if (registration) m_primaryPorts.insert(n.id, registration->descriptor.defaultOutput);
        auto *state = new QTableWidgetItem(stateLabel(node.state));
        state->setToolTip(state->text());
        state->setForeground(stateColor(node.state));
        m_steps->setItem(i, 0, state);
        m_steps->setItem(i, 1,
                         new QTableWidgetItem(
                             QString::number(i + 1) + " · " +
                             (registration ? methodLabel(registration->descriptor) : n.method)));
        m_steps->item(i, 1)->setToolTip(m_steps->item(i, 1)->text());
        auto *detail = new QTableWidgetItem(QString::number(m_record->outputs.value(n.id).size()));
        QString parentText = text("ui.source");
        if (n.input)
        {
            for (int j = 0; j < definition.nodes.size(); ++j)
                if (definition.nodes[j].id == n.input->parent)
                    parentText = QString(text("ui.from_step_1_2"))
                                     .arg(j + 1)
                                     .arg(portLabel(n.input->output));
        }
        if (!n.input && run.provenance.contains("batch_pair"))
        {
            const auto pair = run.provenance["batch_pair"].toObject();
            QStringList inputs;
            for (const auto &side : {"A", "B"})
            {
                const auto member = pair[side].toObject();
                inputs << QString(side) + ": " + QFileInfo(member["label"].toString()).fileName() +
                    " · " + portLabel(member["output"].toString());
            }
            parentText = inputs.join("\n");
        }
        QStringList matches;
        for (const auto &peak : m_record->matchingPeaks)
            if (peak.node == n.id)
                matches << QString::number(peak.frequency, 'g', 8) + " " + peak.unit;
        detail->setToolTip(parentText);
        QStringList warnings;
        for (const auto &warning : node.warnings)
            warnings << translatedLabel(warning, warning);
        detail->setToolTip(parentText + "\n" + presentError(node).details + "\n" + warnings.join("\n") + "\n" +
                           matches.join("\n"));
        if (!matches.isEmpty())
        {
            detail->setData(Qt::UserRole, true);
            auto font = detail->font(); font.setBold(true); detail->setFont(font);
            detail->setText(detail->text() + text("ui.peak_match"));
        }
        m_steps->setItem(i, 2, detail);
        const auto outputs = orderedOutputs(n.id);
        if (node.state == NodeState::Succeeded && !outputs.isEmpty())
        {
            fallback = i;
            if (outputs.front().kind != "Table" || outputs.front().port == "cross_spectrum")
                preferred = i;
        }
    }
    const bool display = m_defaultPlotOnLoad;
    m_defaultPlotOnLoad = false;
    if (!m_nodeIds.isEmpty()) selectStep(preferred >= 0 ? preferred : fallback, display);
    emit recordLoaded(id);
}
void ResultsPage::selectStep(int row, bool display)
{
    if (!m_record || row < 0 || row >= m_nodeIds.size())
        return;
    QSignalBlocker stepBlock(m_steps->selectionModel());
    // currentRowChanged fires inside the view's mouse-selection transaction.
    // Re-selecting here can merge partial rows with its still-pending selection.
    if (m_steps->currentRow() != row)
        m_steps->selectionModel()->setCurrentIndex(m_steps->model()->index(row, 1),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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
    m_subtitle->setToolTip(m_subtitle->text());
    QSignalBlocker portBlock(m_ports);
    m_ports->clear();
    for (const auto &output : orderedOutputs(m_selectedNode))
        m_ports->addItem(registration ? portLabel(registration->descriptor, output.port)
                                      : portLabel(output.port), output.port);
    if (registration)
    {
        int i = m_ports->findData(registration->descriptor.defaultOutput);
        if (i >= 0)
            m_ports->setCurrentIndex(i);
    }
    rebuildOutputs();
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
        QString value = it->isBool() ? text(it->toBool() ? "diagnostic.yes" : "diagnostic.no")
                        : it->isDouble() ? QString::number(it->toDouble(), 'g', 10)
                                         : it->toString();
        if (registration)
            for (const auto &p : registration->descriptor.parameters)
                if (p.key == it.key())
                {
                    name = parameterLabel(p, selected.method);
                    if (!p.choices.isEmpty()) value = choiceLabel(p.key, it->toString());
                    break;
                }
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
    const auto pair = run.provenance["batch_pair"].toObject();
    if (!pair.isEmpty())
    {
        for (const auto &side : {"A", "B"})
        {
            const auto member = pair[side].toObject();
            info += "<p><b>" + QString(side) + "</b><br>" + member["label"].toString().toHtmlEscaped() +
                "<br>" + portLabel(member["output"].toString()).toHtmlEscaped() + "</p>";
        }
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
void ResultsPage::syncPlotSelection(const OutputReference &)
{
    // Explicit target selection while metadata is loading takes precedence over
    // a late automatic default plot. Checking outputs never changes the target.
    m_defaultPlotOnLoad = false;
    updateOutputActions();
}
OutputReference ResultsPage::reference(NodeId node, const QString &port) const
{
    if (!m_record)
        return {};
    for (const auto &out : m_record->outputs.value(node))
        if (out.port == port)
            return reference(node, out);
    return {};
}
OutputReference ResultsPage::reference(NodeId node, const OutputInfo &output) const
{
    if (!m_record) return {};
    return {QFileInfo(m_path).absoluteFilePath(), m_recordId, m_record->run.runId,
            node, output.port, output.kind, m_nodeMethods.value(node)};
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
    const bool emptyDestination = m_workspace->currentIsEmpty();
    const auto selectedLabel = m_record
        ? QString("ID %1 · %2").arg(m_recordId).arg(m_subtitle->text())
        : text("ui.select_step_for_export");
    const auto plotLabel = hasFigure
        ? text("ui.active_figure_target").arg(plotted.recordId).arg(portLabel(plotted.port))
        : text("ui.no_active_figure");
    m_plotTarget->setText(hasFigure
        ? QString("ID %1 · %2").arg(plotted.recordId).arg(portLabel(plotted.port))
        : text(emptyDestination ? "ui.empty_subplot" : "ui.no_active_figure"));
    m_styleEditor->setVisible(hasFigure);
    m_exportPanel->setTargets(selectedLabel + (selected.valid() ? QString() :
                                "\n" + text("ui.no_step_output")), selected.valid(),
                              plotLabel, hasFigure);
    const auto checked = checkedOutputs();
    QSet<NodeId> nodes;
    for (const auto &ref : checked) nodes.insert(ref.nodeId);
    m_checkedButton->setText(text("ui.checked_output_count").arg(checked.size()).arg(nodes.size()));
    const bool any = !checked.isEmpty();
    m_addPlot->setEnabled(any);
    m_plotChecked->setEnabled(any);
    m_pageAction->setEnabled(any);
    m_replaceAction->setEnabled(checked.size() == 1 && (plotted.valid() || emptyDestination) && !m_workspace->loading());
    m_overlayChecked->setEnabled(m_workspace->canOverlaySelection(checked));
    m_overlayChecked->setText(text(emptyDestination ? "ui.fill_current_subplot" : "ui.overlay_selected"));
    m_overlayChecked->setToolTip(m_workspace->destinationLabel() + "\n" +
        text(emptyDestination ? "ui.empty_subplot_help" : "ui.overlay_compatibility_help"));
    const auto groupCount = plotGroups(checked).size();
    m_plotChecked->setText(any ? text("ui.add_plot_count").arg(groupCount) : text("ui.add_selected_subplots"));
    m_plotChecked->setToolTip(text("ui.plot_group_count").arg(groupCount));
    m_destination->setText(m_workspace->destinationLabel());
}
QVector<OutputInfo> ResultsPage::orderedOutputs(NodeId node) const
{
    auto outputs = m_record ? m_record->outputs.value(node) : QVector<OutputInfo>();
    const auto primary = m_primaryPorts.value(node);
    auto it = std::find_if(outputs.begin(), outputs.end(), [&primary](const OutputInfo &out) { return out.port == primary; });
    if (it != outputs.end()) std::rotate(outputs.begin(), it, it + 1);
    return outputs;
}
void ResultsPage::rebuildOutputs()
{
    QSignalBlocker selectionBlock(m_outputs->selectionModel());
    m_outputModel->removeRows(0, m_outputModel->rowCount());
    const auto outputs = orderedOutputs(m_selectedNode);
    for (int i = 0; i < outputs.size(); ++i)
    {
        const auto ref = reference(m_selectedNode, outputs[i]);
        auto *name = new QStandardItem(m_ports->itemText(i));
        auto outputFont = name->font();
        outputFont.setBold(ref.port == m_primaryPorts.value(m_selectedNode));
        name->setFont(outputFont);
        name->setCheckable(true);
        name->setData(QVariant::fromValue(ref), Qt::UserRole);
        name->setCheckState(m_checked.contains(ref.key()) ? Qt::Checked : Qt::Unchecked);
        name->setToolTip(m_ports->itemText(i) + "\n" + ref.port);
        auto *kind = new QStandardItem(text((QString("kind.") + ref.kind).toUtf8().constData()));
        m_outputModel->appendRow({name, kind});
    }
    if (m_ports->currentIndex() >= 0)
        m_outputs->setCurrentIndex(m_outputModel->index(m_ports->currentIndex(), 0));
}
QVector<OutputReference> ResultsPage::checkedOutputs() const
{
    QVector<OutputReference> refs;
    // Stable workflow/output order, independent of click order and hash ordering.
    if (m_record)
        for (const auto &node : m_nodeIds)
            for (const auto &out : orderedOutputs(node))
            {
                auto ref = reference(node, out);
                if (m_checked.contains(ref.key())) refs.push_back(ref);
            }
    return refs;
}
void ResultsPage::plotChecked(PlotWorkspace::Target target)
{
    m_workspace->openBatch(checkedOutputs(), target);
    updateOutputActions();
}
void ResultsPage::inspectChecked()
{
    QDialog dialog(this);
    dialog.setWindowTitle(text("ui.review_checked_outputs"));
    dialog.setObjectName("checkedOutputsDialog");
    connect(this, &ResultsPage::recordChanging, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget;
    const auto refs = checkedOutputs();
    for (const auto &ref : refs)
    {
        const int step = m_nodeIds.indexOf(ref.nodeId);
        auto *item = new QListWidgetItem(m_steps->item(step, 1)->text() + " · " + portLabel(ref.port), list);
        item->setData(Qt::UserRole, QVariant::fromValue(ref));
        item->setCheckState(Qt::Checked);
    }
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *clear = buttons->addButton(text("ui.clear_checked_outputs"), QDialogButtonBox::ActionRole);
    connect(clear, &QPushButton::clicked, &dialog, [this, list] {
        // One model update for a bulk operation; emitting itemChanged per row
        // would repeatedly rebuild and scan the whole browser.
        QSignalBlocker block(list);
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Unchecked);
        m_checked.clear();
        rebuildOutputs(); updateOutputActions();
    });
    connect(list, &QListWidget::itemChanged, &dialog, [this](QListWidgetItem *item) {
        const auto ref = item->data(Qt::UserRole).value<OutputReference>();
        if (item->checkState() == Qt::Checked) m_checked.insert(ref.key(), ref);
        else m_checked.remove(ref.key());
        rebuildOutputs(); updateOutputActions();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(qMin(620, screen()->availableGeometry().width() * 9 / 10),
                  qMin(360, screen()->availableGeometry().height() * 9 / 10));
    dialog.exec();
}
void ResultsPage::showPort()
{
    updateOutputActions();
    // Navigation uses the task's primary output. Browsing/checking an output
    // only changes the export/explicit plotting selection, never this default.
    auto ref = reference(m_selectedNode, m_primaryPorts.value(m_selectedNode));
    if (!ref.valid())
    {
        const auto outputs = orderedOutputs(m_selectedNode);
        if (!outputs.isEmpty()) ref = reference(m_selectedNode, outputs.front());
    }
    if (ref.valid()) m_workspace->openDefault(ref);
}
} // namespace aster::ui
