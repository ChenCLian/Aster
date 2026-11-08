// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "../tests/support/workspace_fixture.h"
#include "ui/panels/source_panel.h"
#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableView>
#include <QToolButton>
#include <QSplitter>
#include <QSplitterHandle>
#include <QScrollBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QRandomGenerator>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QStandardItemModel>
#include <QScopeGuard>
#include <QDialogButtonBox>
#include <QListWidget>
#include "ui/services/plot_data_loader.h"
#include "ui/settings/theme_manager.h"
#include "ui/common/presentation.h"
#include <QMenu>
#include <QMessageBox>
#include "ui/panels/workflow_preview_panel.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include "ui/app/main_window.h"
#include "ui/results/results_page.h"
#include "processing/scientific_registry.h"
#include "ui/workflow/workflow_document.h"
#include "ui/results/plot_workspace.h"
#include "ui/results/plot_preset.h"
#include "ui/results/plot_recipe.h"
#include "ui/results/preview_data.h"
#include "ui/panels/plot_style_editor.h"
#include "ui/services/export_controller.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "third_party/qcustomplot/qcustomplot.h"
using namespace aster;
using namespace aster::ui;
namespace
{
DataRef series(qsizetype n = 128)
{
    SeriesData d;
    d.xAxis = {"Time", "ka"};
    d.yAxis = {"Signal", "u"};
    for (qsizetype i = 0; i < n; ++i)
    {
        d.x.push_back(i);
        d.y.push_back(std::sin(double(i) * .03));
    }
    if (n > 10000)
        d.y[n / 2 + 17] = 500;
    return makeData(std::move(d));
}
RunResult run(DataRef data = series())
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("input_series", {0, 0}).nodeId;
    model.addChild(root, ChildSlot::Down, "series_summary");
    return WorkflowExecutor().run({model.freeze().snapshot, root, {"test", data, {}}});
}
OutputReference save(ResultRepository &repo, const QString &path, RunResult r)
{
    const auto receipt = repo.save(r);
    return {path, receipt.recordId, r.runId, r.root, "series", "Series"};
}
QList<QPushButton *> subplotTitles(QWidget *root)
{
    auto buttons = root->findChildren<QPushButton *>("selectSubplot");
    // QObject child order is not screen order: QSplitter manages child stacking.
    // These mouse tests target the visible top-left subplot, not QObject index 0.
    std::stable_sort(buttons.begin(), buttons.end(), [root](QPushButton *a, QPushButton *b) {
        const auto pa = a->mapTo(root, QPoint()), pb = b->mapTo(root, QPoint());
        return pa.y() == pb.y() ? pa.x() < pb.x() : pa.y() < pb.y();
    });
    return buttons;
}
QString sql(QString path, QString statement)
{
    QString error;
    auto name = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (!db.open())
            error = db.lastError().text();
        else
        {
            QSqlQuery q(db);
            if (!q.exec(statement))
                error = q.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return error;
}
bool clearPlotThroughMenu(PlotView *view)
{
    if (!QMetaObject::invokeMethod(view->plotWidget(), "customContextMenuRequested",
                                  Qt::DirectConnection, Q_ARG(QPoint, QPoint(30, 30)))) return false;
    for (auto *menu : view->findChildren<QMenu *>())
        if (menu->isVisible())
            for (auto *action : menu->actions())
                if (action->text() == text("ui.clear_plot"))
                { action->trigger(); menu->close(); return true; }
    return false;
}
} // namespace
class WorkspaceTests : public QObject
{
    Q_OBJECT
  private slots:
    void overlayAutomaticAndManualAxes_data();
    void overlayAutomaticAndManualAxes();
    void outputPanelUsesRowsAndFontMetrics_data();
    void outputPanelUsesRowsAndFontMetrics();
    void sourceDetailsUseDisplayLabels();
    void clearedSubplotCanBeFilled();
    void clearedSubplotValidatesAndCancels();
    void clearedSubplotScientificRecipes_data();
    void clearedSubplotScientificRecipes();
    void arrangementGeometryAndTransitions();
    void subplotDragTargetsAndCancellation();
    void subplotArrangementKeepsDataAndTargets();
    void subplotDividerDefersRefresh();
    void fourHeatmapsKeepBuffersAndSettle();
    void stepMouseSelectionIsExclusive();
    void defaultPlotsFollowRecordAndStepClicks();
    void defaultPlotsReusePagesAndExplicitSelection();
    void defaultPlotIgnoresBrowsedAuxiliaryOutput();
    void defaultPlotDestinationPreference_data();
    void defaultPlotDestinationPreference();
    void overlayKeepsClickedDestination_data();
    void overlayKeepsClickedDestination();
    void outputBrowserSelectionAndTargets();
    void batchPlotTransactionsAndLimits();
    void familyOverlayHasNoDuplicateCurve();
    void largeOutputBrowserReadsOnlyMetadata();
    void plotFamilyLimitCountsRequestedOutputs();
    void removingRecordKeepsUnrelatedSubplots_data();
    void removingRecordKeepsUnrelatedSubplots();
    void closingBackgroundPageKeepsDestination();
    void removedPendingReferenceCannotCommit_data();
    void removedPendingReferenceCannotCommit();
    void checkedReviewClearsInOneUpdate();
    void checkedReviewClosesBeforeRecordReload();
    void closingLoadingSubplotNotifiesIdle();
    void figureExportKeepsCapturedTarget();
    void metadataDoesNotDecodeOtherOutputs();
    void cocoPlotUsesSelectedAxes();
    void fitIgnoresHiddenCurveBounds();
    void fitRestoresFlatCurveBounds_data();
    void fitRestoresFlatCurveBounds();
    void declinedCloseRestoresPreview();
    void reuseButtonBindsSource();
    void rejectedOverlayKeepsOriginal();
    void referenceRejectsReplacedIdentity();
    void databaseSourcePreservesSnapshotAndDependencies();
    void sourceFailureRemainsSavable();
    void pagesReleaseBuffersAndRestoreStyle();
    void heatmapInspectorAcrossPages();
    void cwtSamplingAndViewportRefinement();
    void refinementKeepsPendingViewport();
    void resetStyleRequestsFullViewport();
    void refinementStatusDoesNotRelayout_data();
    void resolutionStatusHasStableSizeHints();
    void refinementStatusDoesNotRelayout();
    void rejectedOverlayKeepsPendingViewport_data();
    void rejectedOverlayKeepsPendingViewport();
    void scientificOutputGroups();
    void addAllScientificOutputs_data();
    void addAllScientificOutputs();
    void subplotOverlayAndRemoval();
    void closeDuringLoadAndOldMenu();
    void displayEnvelopeKeepsExtremaAndOriginal();
    void largePlotPreparationAndRawExport();
    void gridSamplingAndCancellation();
    void advancedStyleAndDynamicInspector();
};
void WorkspaceTests::overlayAutomaticAndManualAxes_data()
{
    QTest::addColumn<int>("manual"); QTest::addColumn<bool>("unloaded");
    for (int mask = 0; mask < 4; ++mask)
        for (bool otherPage : {false, true})
            QTest::newRow(qPrintable(QString("axes%1-page%2").arg(mask).arg(otherPage))) << mask << otherPage;
}
void WorkspaceTests::overlayAutomaticAndManualAxes()
{
    QFETCH(int, manual); QFETCH(bool, unloaded);
    QTemporaryDir dir; const auto path = dir.filePath("overlay-ranges.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    auto wide = *dataAs<SeriesData>(series(256));
    for (auto &v : wide.y) v = v * 8 + 10;
    const auto second = save(repo, path, run(makeData(wide)));
    PlotWorkspace w; w.resize(1000, 650); w.show(); w.open(first);
    QTRY_VERIFY(!w.loading()); QVERIFY(w.currentView()->hasPlot());
    QVERIFY(!w.currentView()->style().x.manualRange); QVERIFY(!w.currentView()->style().y.manualRange);
    auto style = w.currentView()->style(); style.title = "kept title";
    style.curves["series"].color = Qt::red;
    if (manual & 2) { style.y.manualRange = true; style.y.lower = -2; style.y.upper = 2; }
    QVERIFY(w.currentView()->applyStyle(style).isEmpty());
    if (manual & 1) w.currentView()->plotWidget()->xAxis->setRange(20, 40); // User pan/zoom route.
    QTRY_VERIFY(!w.loading());
    if (unloaded) {
        w.open(second, PlotWorkspace::Target::Page); QTRY_VERIFY(!w.loading());
        w.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0); QTRY_VERIFY(!w.loading());
    }
    w.open(second, PlotWorkspace::Target::Overlay); QTRY_VERIFY(!w.loading());
    auto *view = w.currentView();
    QCOMPARE(w.figureCells(false).front().references.size(), 2);
    QCOMPARE(view->style().title, QString("kept title"));
    QCOMPARE(view->style().curves["series"].color, QColor(Qt::red));
    QCOMPARE(view->style().x.manualRange, bool(manual & 1));
    QCOMPARE(view->style().y.manualRange, bool(manual & 2));
    const auto x = view->plotWidget()->xAxis->range(), y = view->plotWidget()->yAxis->range();
    if (manual & 1) { QCOMPARE(x.lower, 20.); QCOMPARE(x.upper, 40.); }
    else { QVERIFY(x.lower <= 0); QVERIFY(x.upper >= 255); }
    if (manual & 2) { QCOMPARE(y.lower, -2.); QCOMPARE(y.upper, 2.); }
    else { QVERIFY(y.lower < 0); QVERIFY(y.upper >= *std::max_element(wide.y.begin(), wide.y.end())); }
    view->fit(); QTRY_VERIFY(!w.loading());
    QVERIFY(!view->style().x.manualRange); QVERIFY(!view->style().y.manualRange);
    QVERIFY(view->plotWidget()->xAxis->range().upper >= 255);
}
void WorkspaceTests::outputPanelUsesRowsAndFontMetrics_data()
{
    QTest::addColumn<QString>("language"); QTest::addColumn<int>("pointSize");
    QTest::newRow("zh-normal") << QString("zh") << 10;
    QTest::newRow("zh-large") << QString("zh") << 14;
    QTest::newRow("en-normal") << QString("en") << 10;
}
void WorkspaceTests::outputPanelUsesRowsAndFontMetrics()
{
    QFETCH(QString, language); QFETCH(int, pointSize);
    setLanguage(language); auto cleanup = qScopeGuard([] { setLanguage("en"); });
    const auto previousSheet = qApp->styleSheet();
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    auto restoreTheme = qScopeGuard([&] { qApp->setStyleSheet(previousSheet); });
    ResultsPage page(makeScientificRegistry());
    QFont font = page.font(); font.setPointSize(pointSize); page.setFont(font);
    page.resize(1400, 900); page.show(); QTest::qWait(30);
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    auto *steps = page.findChild<QTableWidget *>("resultSteps");
    auto *split = page.findChild<QSplitter *>("plotStepsSplitter");
    auto *toggle = page.findChild<QToolButton *>("toggleResultOutputs");
    auto *checked = page.findChild<QToolButton *>("inspectCheckedOutputs");
    QVERIFY(outputs); QVERIFY(steps); QVERIFY(split); QVERIFY(toggle); QVERIFY(checked);
    QVERIFY2(outputs->viewport()->height() >= 5 * outputs->verticalHeader()->defaultSectionSize(),
             qPrintable(QString("output viewport: %1").arg(outputs->viewport()->height())));
    QVERIFY(steps->viewport()->height() >= 5 * steps->verticalHeader()->defaultSectionSize());
    const auto expanded = split->sizes();
    toggle->click(); QTest::qWait(20);
    QVERIFY(!outputs->isVisible());
    QVERIFY(toggle->height() >= toggle->sizeHint().height());
    QVERIFY(checked->height() >= checked->sizeHint().height());
    QVERIFY(split->widget(1)->height() >= toggle->sizeHint().height() + 12);
    QVERIFY(split->sizes()[0] > expanded[0]);
    QCOMPARE(split->sizes()[0] + split->sizes()[1] + split->handleWidth(), split->height());
    toggle->click(); QTest::qWait(20); QCOMPARE(split->sizes(), expanded);
    // Reopening does not discard the user's deliberate compact sizing.
    split->setSizes({500, 240}); const auto adjusted = split->sizes();
    toggle->click(); toggle->click(); QTest::qWait(20); QCOMPARE(split->sizes(), adjusted);
}
void WorkspaceTests::sourceDetailsUseDisplayLabels()
{
    setLanguage("zh"); auto cleanup = qScopeGuard([] { setLanguage("en"); });
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto cwt = model.addChild(root, ChildSlot::Down, "cwt").nodeId;
    auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"small", series(), {}}});
    QCOMPARE(execution.nodes[cwt].state, NodeState::Succeeded);
    QTemporaryDir dir; const auto path = dir.filePath("labels.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty()); const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    ResultsPage page(makeScientificRegistry()); page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded); page.openRecord(receipt.recordId);
    QTRY_COMPARE(loaded.size(), 1); page.selectStep(1);
    auto *details = page.findChild<QScrollArea *>("resultSourceDetailsContainer");
    QVERIFY(details);
    auto *label = qobject_cast<QLabel *>(details->widget()); QVERIFY(label);
    const auto html = label->text();
    QVERIFY(html.contains(choiceLabel("method", "acycle_cwt")));
    QVERIFY(html.contains(choiceLabel("mother", "morlet")));
    QVERIFY(html.contains(text("diagnostic.yes")));
    QVERIFY(!html.contains("acycle_cwt")); QVERIFY(!html.contains("<b>true</b>"));
    QVERIFY(!html.contains("Nominal 95% wavelet significance")); QVERIFY(html.contains("名义 95%"));
    QSignalSpy messages(&page, &ResultsPage::message); page.openRecord(99999);
    QTRY_VERIFY(!messages.isEmpty());
    QTRY_VERIFY(messages.last().front().toString().contains("未找到该记录"));
    QVERIFY(page.workspace()->pageCount() > 0);
    QCOMPARE(defaultPlotTitle(41, "trend"), QString("ID 41 · 拟合趋势曲线"));
}
void WorkspaceTests::clearedSubplotCanBeFilled()
{
    QTemporaryDir dir; const auto path = dir.filePath("cleared.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    auto changedUnits = *dataAs<SeriesData>(series(256));
    changedUnits.xAxis = {"Depth", "m"}; changedUnits.yAxis = {"Proxy", "other"};
    const auto second = save(repo, path, run(makeData(changedUnits)));
    ResultsPage page(makeScientificRegistry()); page.resize(1200, 800); page.show();
    page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(second.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *w = page.workspace(); QTRY_VERIFY(!w->loading());
    w->open(first, PlotWorkspace::Target::Subplot); QTRY_VERIFY(!w->loading());
    auto *view = w->currentView(); QVERIFY(view->hasPlot());
    QVERIFY(clearPlotThroughMenu(view));
    QVERIFY(!view->hasOutput());
    QVERIFY(!w->currentReference().valid());
    QCOMPARE(w->subplotCount(), 2);
    QCOMPARE(view->parentWidget()->findChild<QPushButton *>("selectSubplot")->text(), text("ui.empty_subplot"));
    QVERIFY(view->parentWidget()->findChild<QLabel *>("plotResolution")->text().isEmpty());
    // Browsing outputs must leave the chosen empty destination intact.
    page.selectStep(0, false);
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    QVERIFY(outputs->model()->setData(outputs->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole));
    auto *fill = page.findChild<QPushButton *>("overlayCheckedOutputs");
    QVERIFY(fill->isEnabled());
    const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) QVERIFY(page.grab().save(captures + "/empty-subplot.png"));
    fill->click();
    QTRY_VERIFY(!w->loading());
    QCOMPARE(w->currentView(), view); QCOMPARE(w->subplotCount(), 2);
    QCOMPARE(w->currentReference().key(), second.key()); QVERIFY(view->hasPlot());
    QCOMPARE(dataAs<SeriesData>(view->currentData())->xAxis.unit, QString("m"));
    // Old data must not return after switching pages.
    w->open(first, PlotWorkspace::Target::Page); QTRY_VERIFY(!w->loading());
    w->findChild<QTabWidget *>("plotPages")->setCurrentIndex(0); QTRY_VERIFY(!w->loading());
    const auto cells = w->figureCells(true); QCOMPARE(cells.size(), 2);
    QCOMPARE(cells[1].references.front().key(), second.key());
}
void WorkspaceTests::clearedSubplotValidatesAndCancels()
{
    QTemporaryDir dir; const auto path = dir.filePath("clear-transactions.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    const auto second = save(repo, path, run(series(256)));
    auto incompatible = *dataAs<SeriesData>(series()); incompatible.yAxis.unit = "different";
    const auto bad = save(repo, path, run(makeData(incompatible)));
    PlotWorkspace w; w.resize(1000, 700); w.show();
    w.open(first); QTRY_VERIFY(!w.loading());
    auto *view = w.currentView();
    auto oldStyle = view->style(); oldStyle.title = "must not return";
    oldStyle.y.reversed = true; QVERIFY(view->applyStyle(oldStyle).isEmpty());
    QVERIFY(clearPlotThroughMenu(view)); QVERIFY(w.currentIsEmpty());
    // The placeholder retains its position across tab rebuilds and source deletion.
    w.open(second, PlotWorkspace::Target::Page); QTRY_VERIFY(!w.loading());
    w.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0); QTRY_VERIFY(!w.loading());
    QVERIFY(w.currentIsEmpty()); w.forgetRecord(path, first.recordId);
    QCOMPARE(w.pageCount(), 2); QCOMPARE(w.subplotCount(), 1);
    view = w.currentView();
    // Type preflight is permissive; actual X AND Y units are checked atomically on load.
    QVERIFY(w.canOverlaySelection({first, bad}));
    w.openBatch({first, bad}, PlotWorkspace::Target::Overlay);
    QTRY_VERIFY(!w.loading()); QVERIFY(w.currentIsEmpty()); QVERIFY(!view->hasOutput());
    auto missing = first; missing.port = "missing_port";
    w.openBatch({missing}, PlotWorkspace::Target::Overlay);
    QTRY_VERIFY(!w.loading()); QVERIFY(w.currentIsEmpty());
    w.openBatch({first, second}, PlotWorkspace::Target::Overlay);
    QTRY_VERIFY(!w.loading()); QCOMPARE(w.subplotCount(), 1);
    QCOMPARE(w.figureCells(false).front().references.size(), 2);
    QVERIFY(!view->style().y.reversed); QVERIFY(view->style().title != oldStyle.title);
    // Clear cancels even an already-finished job whose GUI completion is queued.
    w.openBatch({bad}, PlotWorkspace::Target::Replace);
    QVERIFY(w.loading()); QVERIFY(clearPlotThroughMenu(view));
    QTRY_VERIFY(!w.loading()); QVERIFY(w.currentIsEmpty()); QVERIFY(!view->hasOutput());
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        w.openBatch({second}, PlotWorkspace::Target::Overlay);
        QVERIFY(w.loading()); QVERIFY(!w.currentIsEmpty());
        w.forgetRecord(path, second.recordId);
        QTRY_VERIFY(!w.loading()); QVERIFY(w.currentIsEmpty());
        w.openBatch({first}, PlotWorkspace::Target::Overlay);
        QTRY_VERIFY(!w.loading()); QVERIFY(view->hasPlot());
        QVERIFY(clearPlotThroughMenu(view)); QVERIFY(w.currentIsEmpty());
    }
    QCOMPARE(w.subplotCount(), 1);
}
void WorkspaceTests::clearedSubplotScientificRecipes_data()
{
    QTest::addColumn<QString>("method");
    QTest::newRow("wavelet_coi_first") << QString("cwt");
    QTest::newRow("spectrum_peaks_first") << QString("mtm");
    QTest::newRow("single_table") << QString("series_summary");
}
void WorkspaceTests::clearedSubplotScientificRecipes()
{
    QFETCH(QString, method);
    QTemporaryDir dir; const auto path = dir.filePath("clear-recipes.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto node = model.addChild(root, ChildSlot::Down, method).nodeId;
    auto input = *dataAs<SeriesData>(series(256));
    for (qsizetype i = 0; i < input.y.size(); ++i) input.y[i] = std::sin(.4 * i) + .3 * std::sin(1.3 * i);
    const auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"fill", makeData(input), {}}});
    QCOMPARE(execution.nodes[node].state, NodeState::Succeeded);
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    const auto ref = [&](QString port, QString kind) {
        return OutputReference{path, receipt.recordId, execution.runId, node, port, kind, method};
    };
    QVector<OutputReference> refs;
    if (method == "cwt") refs = {ref("coi", "Series"), ref("power", "Grid")};
    else if (method == "mtm") refs = {ref("peaks", "Table"), ref("spectrum", "Spectrum")};
    else
        for (auto it = execution.nodes[node].outputs.cbegin(); it != execution.nodes[node].outputs.cend(); ++it)
            if (dataAs<TableData>(it.value())) { refs = {ref(it.key(), "Table")}; break; }
    QVERIFY(!refs.isEmpty());
    PlotWorkspace w; w.resize(1000, 700); w.show();
    w.open({path, receipt.recordId, execution.runId, root, "series", "Series"});
    QTRY_VERIFY(!w.loading()); auto *view = w.currentView();
    QVERIFY(clearPlotThroughMenu(view)); QVERIFY(w.canOverlaySelection(refs));
    if (method == "cwt")
        QVERIFY(!w.canOverlaySelection({refs[0], refs[1], ref("raw_power", "Grid")}));
    w.openBatch(refs, PlotWorkspace::Target::Overlay); QTRY_VERIFY(!w.loading());
    QCOMPARE(w.currentView(), view); QCOMPARE(w.subplotCount(), 1); QVERIFY(view->hasOutput());
    if (method == "cwt")
    {
        QCOMPARE(w.currentReference().port, QString("power"));
        QVERIFY(dataAs<GridData>(view->currentData()));
        QVERIFY(view->style().y.logarithmic); QVERIFY(view->style().y.reversed);
        QVERIFY(view->style().curves.contains("coi"));
    }
    else if (method == "mtm")
    {
        QCOMPARE(w.currentReference().port, QString("spectrum"));
        QVERIFY(view->style().curves.contains("peaks"));
    }
    else { QVERIFY(dataAs<TableData>(view->currentData())); QVERIFY(!view->hasPlot()); }
}
void WorkspaceTests::stepMouseSelectionIsExclusive()
{
    QTemporaryDir dir; const auto path = dir.filePath("steps.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    auto node = root;
    for (int i = 0; i < 3; ++i) node = model.addChild(node, ChildSlot::Down, "affine").nodeId;
    const auto ref = save(repo, path, WorkflowExecutor().run({model.freeze().snapshot, root, {"steps", series(), {}}}));
    ResultsPage page(makeScientificRegistry()); page.resize(1400, 900); page.show();
    page.setDatabase(path); QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(ref.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *steps = page.findChild<QTableWidget *>("resultSteps"); QVERIFY(steps);
    for (int row : {1, 3, 2, 0, 3, 1})
    {
        steps->scrollToItem(steps->item(row, 1));
        QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                         steps->visualItemRect(steps->item(row, 1)).center());
        const auto selected = steps->selectionModel()->selectedIndexes();
        QCOMPARE(selected.size(), steps->columnCount());
        for (const auto &index : selected) QCOMPARE(index.row(), row);
        QCOMPARE(steps->currentRow(), row);
    }
}
void WorkspaceTests::defaultPlotsFollowRecordAndStepClicks()
{
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    QTemporaryDir dir; const auto path = dir.filePath("defaults.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto transform = model.addChild(root, ChildSlot::Down, "affine").nodeId;
    model.addChild(transform, ChildSlot::Down, "series_summary");
    const auto ref = save(repo, path, WorkflowExecutor().run({model.freeze().snapshot, root, {"defaults", series(), {}}}));
    ResultsPage page(makeScientificRegistry()); page.resize(1400, 900); page.show();
    page.workspace()->setDefaultPlotNewPage(false);
    page.setDatabase(path); QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(ref.recordId); QTRY_COMPARE(loaded.size(), 1);
    QTRY_VERIFY(page.plotView()->hasPlot());
    QCOMPARE(page.workspace()->currentReference().nodeId, transform);
    QCOMPARE(page.workspace()->subplotCount(), 1);
    auto *steps = page.findChild<QTableWidget *>("resultSteps");
    QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                     steps->visualItemRect(steps->item(0, 1)).center());
    QTRY_VERIFY(!page.workspace()->loading());
    QCOMPARE(page.workspace()->currentReference().nodeId, root);
    QCOMPARE(page.workspace()->subplotCount(), 2);
    // Revisit an existing output without duplicating it or losing manual style.
    auto style = page.plotView()->style(); style.title = "kept";
    QVERIFY(page.plotView()->applyStyle(style).isEmpty());
    QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                     steps->visualItemRect(steps->item(1, 1)).center());
    QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                     steps->visualItemRect(steps->item(0, 1)).center());
    QCOMPARE(page.workspace()->subplotCount(), 2);
    QCOMPARE(page.plotView()->style().title, QString("kept"));
    auto *split = page.findChild<QSplitter *>("plotStepsSplitter"); QVERIFY(split);
    // The output browser now starts taller, while the plot keeps the larger share.
    QVERIFY(split->sizes()[0] > split->sizes()[1]);
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(page.grab().save(capture + "/defaults.png"));
}
void WorkspaceTests::defaultPlotDestinationPreference_data()
{
    QTest::addColumn<bool>("newPage");
    QTest::newRow("new_page") << true;
    QTest::newRow("current_page") << false;
}
void WorkspaceTests::defaultPlotDestinationPreference()
{
    QFETCH(bool, newPage);
    QTemporaryDir dir; AppSettings settings;
    settings.databasePath = dir.filePath("destination.sqlite");
    settings.defaultPlotNewPage = newPage;
    ResultRepository repo; QVERIFY(repo.open(settings.databasePath).isEmpty());
    QVector<OutputReference> refs;
    for (int i = 0; i < 6; ++i) refs << save(repo, settings.databasePath, run());
    MainWindow window(settings, false);
    auto *workspace = window.findChild<PlotWorkspace *>(); QVERIFY(workspace);
    for (int i = 0; i < 5; ++i)
    {
        workspace->openDefault(refs[i]); QTRY_VERIFY(!workspace->loading());
        QCOMPARE(workspace->pageCount(), newPage ? i + 1 : i / 4 + 1);
        QCOMPARE(workspace->subplotCount(), newPage ? 1 : i % 4 + 1);
    }
    workspace->openDefault(refs[0]); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->pageCount(), newPage ? 5 : 2);
    // Preferences only affect missing navigation plots, not existing pages
    // or explicit commands. A deliberately added duplicate remains a new plot.
    workspace->openBatch({refs[0]}, PlotWorkspace::Target::Page); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->pageCount(), newPage ? 6 : 3);
    workspace->setDefaultPlotNewPage(!newPage);
    workspace->openDefault(refs[5]); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->pageCount(), newPage ? 6 : 4);
    QCOMPARE(workspace->subplotCount(), newPage ? 2 : 1);
    workspace->openDefault(refs[0]); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->pageCount(), newPage ? 6 : 4);
    QCOMPARE(workspace->currentReference().key(), refs[0].key());
}
void WorkspaceTests::defaultPlotIgnoresBrowsedAuxiliaryOutput()
{
    QTemporaryDir dir; const auto path = dir.filePath("cwt-default.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto cwt = model.addChild(root, ChildSlot::Down, "cwt").nodeId;
    const auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"default", series(256), {}}});
    QCOMPARE(execution.nodes[cwt].state, NodeState::Succeeded);
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    ResultsPage page(makeScientificRegistry()); page.resize(1400, 900); page.show();
    page.setDatabase(path); QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(receipt.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *workspace = page.workspace(); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->currentReference().port, QString("power"));
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    auto *steps = page.findChild<QTableWidget *>("resultSteps");
    for (int row = 0; row < outputs->model()->rowCount(); ++row)
        if (outputs->model()->index(row, 0).data(Qt::UserRole).value<OutputReference>().port == "coi")
        {
            outputs->setCurrentIndex(outputs->model()->index(row, 0));
            outputs->model()->setData(outputs->model()->index(row, 0), Qt::Checked, Qt::CheckStateRole);
        }
    QCOMPARE(page.checkedOutputs().size(), 1);
    QCOMPARE(page.checkedOutputs()[0].port, QString("coi"));
    // Browsing an auxiliary output must not change a later navigation default.
    // Repeat after deletion, including an initial read cancelled by removal.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        workspace->closeSubplot();
        QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                         steps->visualItemRect(steps->item(1, 1)).center());
        if (attempt == 1)
        {
            workspace->closeSubplot();
            QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                             steps->visualItemRect(steps->item(1, 1)).center());
        }
        QTRY_VERIFY(!workspace->loading());
        QCOMPARE(workspace->currentReference().port, QString("power"));
        QVERIFY(dataAs<GridData>(workspace->currentView()->currentData()));
        QVERIFY(workspace->currentView()->plotWidget()->plottableCount() > 1);
    }
    // An explicit request for COI remains valid and independent of the default.
    page.plotChecked(PlotWorkspace::Target::Subplot); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->currentReference().port, QString("coi"));
    QVERIFY(dataAs<SeriesData>(workspace->currentView()->currentData()));
    QTest::mouseClick(steps->viewport(), Qt::LeftButton, Qt::NoModifier,
                     steps->visualItemRect(steps->item(1, 1)).center());
    QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->currentReference().port, QString("power"));
    QCOMPARE(page.checkedOutputs()[0].port, QString("coi"));
}
void WorkspaceTests::defaultPlotsReusePagesAndExplicitSelection()
{
    QTemporaryDir dir; const auto path = dir.filePath("reuse.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    QVector<OutputReference> refs;
    for (int i = 0; i < 6; ++i) refs.push_back(save(repo, path, run(series(64 + i))));
    ResultsPage page(makeScientificRegistry()); page.setDatabase(path);
    auto *w = page.workspace();
    w->setDefaultPlotNewPage(false);
    for (int i = 0; i < 5; ++i) { w->openDefault(refs[i]); QTRY_VERIFY(!w->loading()); }
    QCOMPARE(w->pageCount(), 2); QCOMPARE(w->subplotCount(), 1);
    w->openDefault(refs[1]); QTRY_VERIFY(!w->loading());
    QCOMPARE(w->pageCount(), 2); QCOMPARE(w->subplotCount(), 4);
    QCOMPARE(w->currentReference().key(), refs[1].key());
    auto style = w->currentView()->style(); style.title = "reuse across pages";
    QVERIFY(w->currentView()->applyStyle(style).isEmpty());
    w->openDefault(refs[4]); QTRY_VERIFY(!w->loading());
    w->openDefault(refs[1]); QTRY_VERIFY(!w->loading());
    QCOMPARE(w->currentView()->style().title, style.title);
    // A metadata callback must not steal a destination selected after the request.
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(refs[5].recordId);
    auto buttons = subplotTitles(w);
    QCOMPARE(buttons.size(), 4); buttons[0]->click();
    QTRY_COMPARE(loaded.size(), 1); QTRY_VERIFY(!w->loading());
    QCOMPARE(w->currentReference().key(), refs[0].key());
    QCOMPARE(w->pageCount(), 2); QCOMPARE(w->subplotCount(), 4);
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    outputs->model()->setData(outputs->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole);
    QCOMPARE(w->currentReference().key(), refs[0].key());
    page.findChild<QPushButton *>("overlayCheckedOutputs")->click();
    QTRY_VERIFY(!w->loading());
    const auto cells = w->figureCells(true);
    QCOMPARE(cells[0].references.size(), 2);
    QCOMPARE(cells[0].references[1].key(), refs[5].key());
    for (int i = 1; i < cells.size(); ++i) QCOMPARE(cells[i].references.size(), 1);
}
void WorkspaceTests::overlayKeepsClickedDestination_data()
{
    QTest::addColumn<bool>("compatible");
    QTest::newRow("compatible") << true;
    QTest::newRow("different_units") << false;
}
void WorkspaceTests::overlayKeepsClickedDestination()
{
    QFETCH(bool, compatible);
    setLanguage("zh");
    QTemporaryDir dir; const auto path = dir.filePath("overlay.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto a = save(repo, path, run());
    auto input = *dataAs<SeriesData>(series(80));
    if (!compatible) { input.xAxis.unit = "rmcd"; input.yAxis.unit = "1"; }
    const auto b = save(repo, path, run(makeData(input)));
    PlotWorkspace w; w.resize(1100, 650); w.show();
    w.openBatch({a, b}, PlotWorkspace::Target::Subplot); QTRY_VERIFY(!w.loading());
    auto buttons = subplotTitles(&w); QCOMPARE(buttons.size(), 2);
    QTest::mouseClick(buttons[0], Qt::LeftButton);
    QCOMPARE(w.currentReference().key(), a.key());
    QSignalSpy messages(&w, &PlotWorkspace::message);
    w.openBatch({b}, PlotWorkspace::Target::Overlay);
    // A later selection cannot retarget the already submitted operation.
    buttons[1]->click(); QTRY_VERIFY(!w.loading());
    const auto cells = w.figureCells(true); QCOMPARE(cells.size(), 2);
    QCOMPARE(cells[0].references.size(), compatible ? 2 : 1);
    QCOMPARE(cells[1].references.size(), 1);
    QCOMPARE(cells[0].references[0].key(), a.key());
    QCOMPARE(cells[1].references[0].key(), b.key());
    if (!compatible) {
        QVERIFY(!messages.isEmpty());
        const auto message = messages.last().front().toString();
        QVERIFY(message.contains(QStringLiteral("未叠加")));
        QVERIFY(message.contains(QStringLiteral("原有子图保持不变")));
        QVERIFY(message.contains("ka / u")); QVERIFY(message.contains("rmcd / 1"));
    }
}
void WorkspaceTests::outputBrowserSelectionAndTargets()
{
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    QTemporaryDir dir;
    const auto path = dir.filePath("browser.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    const auto second = save(repo, path, run(series(80)));
    ResultsPage page(makeScientificRegistry()); page.resize(1500, 950); page.show();
    page.workspace()->setDefaultPlotNewPage(false); // Exercise destination selection within one page.
    page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(first.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *outputs = page.findChild<QTableView *>("resultOutputs"); QVERIFY(outputs);
    auto *steps = page.findChild<QTableWidget *>("resultSteps"); QVERIFY(steps);
    QCOMPARE(steps->columnCount(), 3);
    QTRY_VERIFY(!page.workspace()->loading());
    QCOMPARE(page.workspace()->pageCount(), 1);
    QCOMPARE(page.workspace()->currentReference().key(), first.key());
    QCOMPARE(outputs->model()->rowCount(), 1);
    QCoreApplication::processEvents();
    const auto rowRect = outputs->visualRect(outputs->model()->index(0, 0));
    QTest::mouseClick(outputs->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rowRect.left() + 10, rowRect.center().y()));
    QCOMPARE(page.checkedOutputs().size(), 1);

    page.selectStep(1); QCOMPARE(page.checkedOutputs().size(), 1);
    outputs->model()->setData(outputs->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole);
    QCOMPARE(page.checkedOutputs().size(), 2);
    QCOMPARE(page.checkedOutputs()[0].nodeId, first.nodeId);
    QCOMPARE(page.checkedOutputs()[1].kind, QString("Table"));
    page.findChild<QPushButton *>("plotCheckedOutputs")->click();
    QTRY_VERIFY(!page.workspace()->loading());
    QCOMPARE(page.workspace()->subplotCount(), 3); // Default plus two explicitly added outputs.
    QVERIFY(dataAs<TableData>(page.plotView()->currentData()));
    const auto target = page.workspace()->currentReference().key();
    page.selectStep(0); QCOMPARE(page.workspace()->currentReference().key(), target);
    // Export combo and output focus agree, but neither changes the figure target.
    QCOMPARE(page.findChild<QComboBox *>("outputPort")->currentData().toString(), QString("series"));
    QCOMPARE(page.checkedOutputs().size(), 2);
    auto *toggle = page.findChild<QToolButton *>("toggleResultOutputs"); QVERIFY(toggle);
    toggle->click(); QVERIFY(!outputs->isVisible()); toggle->click(); QVERIFY(outputs->isVisible());
    QCoreApplication::processEvents();
    QVERIFY(page.findChild<QSplitter *>("plotStepsSplitter")->sizes()[1] > 120);
    page.openRecord(second.recordId); QTRY_COMPARE(loaded.size(), 2);
    QVERIFY(page.checkedOutputs().isEmpty());
    QCOMPARE(outputs->model()->rowCount(), 1);
    QCoreApplication::processEvents();
    QVERIFY(outputs->visualRect(outputs->model()->index(0, 0)).height() >= 25);
    QTRY_VERIFY(!page.workspace()->loading());
    QCOMPARE(page.workspace()->currentReference().key(), second.key());
    // Selecting an existing figure changes only the destination, not the record browser.
    auto buttons = subplotTitles(page.workspace());
    QCOMPARE(buttons.size(), 4); buttons[0]->click();
    QCOMPARE(page.recordId(), second.recordId);
    QCOMPARE(page.workspace()->currentReference().key(), first.key());
    QVERIFY(page.findChild<QLabel *>("plotDestination")->text().contains("1"));
    page.resize(1200, 720); QTest::qWait(30);
    QVERIFY(outputs->visualRect(outputs->model()->index(0, 0)).bottom() < outputs->viewport()->height());
    const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        page.grab().save(captures + "/output_browser.png");
        page.resize(1200, 720); QTest::qWait(30);
        QVERIFY(outputs->width() > 150);
        QVERIFY(page.findChild<QPushButton *>("plotCheckedOutputs")->isVisible());
        page.grab().save(captures + "/output_browser_1200.png");
        QVERIFY(ThemeManager::instance().apply(*qApp, "dark").isEmpty());
        QCoreApplication::processEvents();
        page.grab().save(captures + "/output_browser_dark.png");
        QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    }
}
void WorkspaceTests::batchPlotTransactionsAndLimits()
{
    QTemporaryDir dir; const auto path = dir.filePath("batch.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    QVector<OutputReference> refs;
    for (int i = 0; i < 6; ++i) refs.push_back(save(repo, path, run(series(64 + i))));
    auto badData = *dataAs<SeriesData>(series()); badData.yAxis.unit = "incompatible";
    const auto bad = save(repo, path, run(makeData(badData)));
    PlotWorkspace workspace; workspace.resize(800, 600); workspace.show();
    workspace.openBatch({refs[0]}, PlotWorkspace::Target::Subplot);
    QTRY_VERIFY(!workspace.loading());
    auto *original = workspace.currentView();
    auto style = original->style(); style.title = "keep"; style.x.manualRange = true;
    style.x.lower = 10; style.x.upper = 30; QVERIFY(original->applyStyle(style).isEmpty());
    QTRY_VERIFY(!workspace.loading());
    QSignalSpy messages(&workspace, &PlotWorkspace::message);
    workspace.openBatch({refs[1], bad}, PlotWorkspace::Target::Overlay);
    QVERIFY(original->hasPlot()); // Not cleared while validating.
    QTRY_VERIFY(!workspace.loading()); QVERIFY(!messages.isEmpty());
    QCOMPARE(workspace.figureCells(false)[0].references.size(), 1);
    QCOMPARE(workspace.currentReference().key(), refs[0].key());
    QCOMPARE(workspace.currentView()->style().title, QString("keep"));
    QCOMPARE(workspace.currentView()->styleWithViewport().x.lower, 10.);
    workspace.openBatch({refs[1], refs[2]}, PlotWorkspace::Target::Overlay);
    QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.figureCells(false)[0].references.size(), 3);
    QCOMPARE(workspace.currentView()->plottedCount(), 3);
    QCOMPARE(workspace.currentView()->style().title, QString("keep"));
    // Leaving a page cancels I/O, not the explicit plotting command. Revisit
    // resumes validation against the original stable cell identity.
    workspace.openBatch({refs[5]}, PlotWorkspace::Target::Overlay);
    workspace.open(refs[4], PlotWorkspace::Target::Page);
    workspace.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0);
    QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.figureCells(false)[0].references.size(), 4);
    QCOMPARE(workspace.currentView()->style().title, QString("keep"));
    workspace.closePage(1); QTRY_VERIFY(!workspace.loading());
    workspace.openBatch({bad}, PlotWorkspace::Target::Overlay);
    workspace.open(refs[4], PlotWorkspace::Target::Page);
    workspace.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0);
    QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.figureCells(false)[0].references.size(), 4);
    QCOMPARE(workspace.currentView()->style().title, QString("keep"));
    workspace.closePage(1); QTRY_VERIFY(!workspace.loading());
    auto invalid = refs[3]; invalid.runId = QUuid::createUuid();
    workspace.openBatch({invalid}, PlotWorkspace::Target::Replace);
    QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.currentReference().key(), refs[0].key());
    workspace.openBatch({refs[3], refs[4]}, PlotWorkspace::Target::Replace);
    QCOMPARE(workspace.currentReference().key(), refs[0].key());
    workspace.openBatch({refs[3]}, PlotWorkspace::Target::Replace);
    QTRY_VERIFY(!workspace.loading()); QCOMPARE(workspace.currentReference().key(), refs[3].key());
    QCOMPARE(workspace.figureCells(false)[0].references.size(), 1);
    workspace.openBatch(refs, PlotWorkspace::Target::Page);
    QTRY_VERIFY(!workspace.loading()); QCOMPARE(workspace.pageCount(), 3); QCOMPARE(workspace.subplotCount(), 2);
    auto *tabs = workspace.findChild<QTabWidget *>("plotPages"); tabs->setCurrentIndex(1);
    QTRY_VERIFY(!workspace.loading()); QCOMPARE(workspace.subplotCount(), 4);
    QVector<OutputReference> overflow;
    for (int i = 0; i < 129; ++i) { auto ref = refs[0]; ref.nodeId = QUuid::createUuid(); overflow.push_back(ref); }
    workspace.openBatch(overflow, PlotWorkspace::Target::Subplot);
    QCOMPARE(workspace.pageCount(), 3); QCOMPARE(workspace.subplotCount(), 4);
}
void WorkspaceTests::familyOverlayHasNoDuplicateCurve()
{
    QTemporaryDir dir; const auto path = dir.filePath("family.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    auto r = run();
    const SpectrumData spectrum{{.1,.2,.3},{3,2,1},{"Frequency","1/ka"},{"Power","u2 ka"}};
    r.nodes[r.root] = NodeResult::success({{"spectrum", makeData(spectrum)}, {"ar1_99", makeData(spectrum)}});
    const auto receipt = repo.save(r); QVERIFY(receipt.saved());
    OutputReference main{path, receipt.recordId, r.runId, r.root, "spectrum", "Spectrum"};
    auto extra = main; extra.port = "ar1_99";
    const auto loaded = loadPlotData({main, extra}, {}, {});
    QCOMPARE(loaded.state, NodeState::Succeeded);
    QVERIFY(!loaded.outputs.contains("overlay_1"));
    PlotView view; view.showOutput(loaded, "spectrum");
    QCOMPARE(view.plottedCount(), 2); QVERIFY(view.style().curves["ar1_99"].visible);
    PlotWorkspace workspace; workspace.open(main);
    QTRY_VERIFY(!workspace.loading());
    QVERIFY(!workspace.currentView()->style().curves["ar1_99"].visible);
    workspace.openBatch({extra}, PlotWorkspace::Target::Overlay);
    QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.currentView()->plottedCount(), 2);
    QVERIFY(workspace.currentView()->style().curves["ar1_99"].visible);
}
void WorkspaceTests::largeOutputBrowserReadsOnlyMetadata()
{
    QTemporaryDir dir; const auto path = dir.filePath("many.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    auto r = run();
    for (int i = 0; i < 1000; ++i)
        r.nodes[r.root].outputs[QString("aux_%1").arg(i, 4, 10, QChar('0'))] =
            r.nodes[r.executionOrder.last()].outputs.value("summary");
    const auto saved = repo.save(r); QVERIFY(saved.saved());
    // Opening a record decodes only its default plot, not the auxiliary outputs.
    QVERIFY(sql(path, "UPDATE aster_outputs SET data=x'00' WHERE port LIKE 'aux_%'").isEmpty());
    ResultsPage page(makeScientificRegistry()); page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    QElapsedTimer elapsed; elapsed.start(); page.openRecord(saved.recordId);
    QTRY_COMPARE(loaded.size(), 1);
    auto *outputs = page.findChild<QTableView *>("resultOutputs"); QVERIFY(outputs);
    QCOMPARE(outputs->model()->rowCount(), 1001);
    QTRY_VERIFY(!page.workspace()->loading());
    QCOMPARE(page.workspace()->pageCount(), 1);
    QVERIFY(page.plotView()->hasPlot());
    QVERIFY(outputs->findChildren<QToolButton *>().isEmpty());
    qInfo() << "1001-output metadata browser milliseconds:" << elapsed.elapsed();
    elapsed.restart();
    for (int i = 0; i < 100; ++i) QVERIFY(page.checkedOutputs().isEmpty());
    qInfo() << "100 scans of 1001 output references milliseconds:" << elapsed.elapsed();
}
void WorkspaceTests::plotFamilyLimitCountsRequestedOutputs()
{
    QTemporaryDir dir; const auto path = dir.filePath("family-limit.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    auto r = run();
    for (int i = 1; i < PlotLimits::maxFamilyOutputs; ++i)
        r.nodes[r.root].outputs[QString("aux_%1").arg(i)] = series(2);
    auto ref = save(repo, path, r);
    QCOMPARE(repo.loadPlotPreview(ref, {}, {}).state, NodeState::Succeeded);
    r.nodes[r.root].outputs["one_too_many"] = series(2);
    r.runId = QUuid::createUuid(); ref = save(repo, path, r);
    QCOMPARE(repo.loadPlotPreview(ref, {}, {}).state, NodeState::Failed);
    PlotRequest onlySelected; onlySelected.includeFamily = false;
    const auto selected = repo.loadPlotPreview(ref, onlySelected, {});
    QCOMPARE(selected.state, NodeState::Succeeded);
    QCOMPARE(selected.outputs.size(), 1); QVERIFY(selected.outputs.contains("series"));
}
void WorkspaceTests::removingRecordKeepsUnrelatedSubplots_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("remove-selected") << 0;
    QTest::newRow("keep-selected") << 1;
    QTest::newRow("background-page") << 2;
}
void WorkspaceTests::removingRecordKeepsUnrelatedSubplots()
{
    QFETCH(int, mode);
    QTemporaryDir dir; const auto path = dir.filePath("remove.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto a = save(repo, path, run()), b = save(repo, path, run(series(60))),
               c = save(repo, path, run(series(70)));
    PlotWorkspace w;
    w.openBatch({a, b}, PlotWorkspace::Target::Subplot); QTRY_VERIFY(!w.loading());
    QPointer<PlotView> kept = w.currentView();
    auto style = kept->style(); style.title = "survivor";
    QVERIFY(kept->applyStyle(style).isEmpty());
    if (mode == 0) subplotTitles(&w)[0]->click();
    if (mode == 2) { w.open(c, PlotWorkspace::Target::Page); QTRY_VERIFY(!w.loading()); }
    w.forgetRecord(path, a.recordId); QTRY_VERIFY(!w.loading());
    if (mode == 2) {
        QCOMPARE(w.currentReference().key(), c.key()); QCOMPARE(w.pageCount(), 2);
        w.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0); QTRY_VERIFY(!w.loading());
    } else {
        QVERIFY(!kept.isNull()); QCOMPARE(w.currentView(), kept.data());
    }
    QCOMPARE(w.subplotCount(), 1); QCOMPARE(w.currentReference().key(), b.key());
    QCOMPARE(w.currentView()->style().title, QString("survivor"));
    w.forgetRecord(path, b.recordId); QTRY_VERIFY(!w.loading());
    QCOMPARE(w.pageCount(), mode == 2 ? 1 : 0);
    if (mode == 2) QCOMPARE(w.currentReference().key(), c.key());
}
void WorkspaceTests::closingBackgroundPageKeepsDestination()
{
    QTemporaryDir dir; const auto path = dir.filePath("close.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    QVector<OutputReference> refs;
    for (int i = 0; i < 4; ++i) refs.push_back(save(repo, path, run(series(30 + i))));
    PlotWorkspace w;
    for (int i = 0; i < 3; ++i) w.open(refs[i], PlotWorkspace::Target::Page);
    w.open(refs[3], PlotWorkspace::Target::Subplot);
    QTRY_VERIFY(!w.loading());
    auto *selected = w.currentView();
    auto style = selected->style(); style.title = "selected subplot";
    QVERIFY(selected->applyStyle(style).isEmpty());
    QTRY_VERIFY(!w.loading());
    w.closePage(0); // Closing an earlier tab must not choose another destination.
    QCOMPARE(w.pageCount(), 2);
    QCOMPARE(w.currentReference().key(), refs[3].key());
    QCOMPARE(w.currentView(), selected); // No needless unload/reload of live buffers.
    QVERIFY(!w.loading());
    QCOMPARE(w.currentView()->style().title, QString("selected subplot"));
    QCOMPARE(w.findChild<QTabWidget *>("plotPages")->currentIndex(), 1);
}
void WorkspaceTests::removedPendingReferenceCannotCommit_data()
{
    QTest::addColumn<bool>("replace");
    QTest::addColumn<bool>("unloaded");
    QTest::newRow("overlay-active") << false << false;
    QTest::newRow("replace-active") << true << false;
    QTest::newRow("overlay-unloaded") << false << true;
    QTest::newRow("replace-unloaded") << true << true;
}
void WorkspaceTests::removedPendingReferenceCannotCommit()
{
    QFETCH(bool, replace); QFETCH(bool, unloaded);
    QTemporaryDir dir; const auto path = dir.filePath("pending.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    const auto removed = save(repo, path, run(series(70)));
    const auto other = save(repo, path, run(series(90)));
    PlotWorkspace w; w.open(first); QTRY_VERIFY(!w.loading());
    auto style = w.currentView()->style(); style.title = "original";
    QVERIFY(w.currentView()->applyStyle(style).isEmpty());
    QTRY_VERIFY(!w.loading());
    w.openBatch({removed}, replace ? PlotWorkspace::Target::Replace : PlotWorkspace::Target::Overlay);
    if (unloaded) w.open(other, PlotWorkspace::Target::Page);
    // Simulate the directory's completed-removal notification. Retain the bytes
    // to also model an I/O snapshot that finished before its GUI callback runs.
    w.forgetRecord(path, removed.recordId);
    w.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0);
    QTRY_VERIFY(!w.loading());
    QCOMPARE(w.currentReference().key(), first.key());
    QCOMPARE(w.figureCells(false).size(), 1);
    QCOMPARE(w.figureCells(false)[0].references.size(), 1);
    QCOMPARE(w.currentView()->style().title, QString("original"));
}
void WorkspaceTests::checkedReviewClearsInOneUpdate()
{
    QTemporaryDir dir; const auto path = dir.filePath("review.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    auto r = run();
    for (int i = 0; i < 40; ++i) r.nodes[r.root].outputs[QString("aux_%1").arg(i)] = series(2);
    const auto saved = repo.save(r); QVERIFY(saved.saved());
    ResultsPage page(makeScientificRegistry()); page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(saved.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *outputs = page.findChild<QTableView *>("resultOutputs"); QVERIFY(outputs);
    for (int i = 0; i < outputs->model()->rowCount(); ++i)
        outputs->model()->setData(outputs->model()->index(i, 0), Qt::Checked, Qt::CheckStateRole);
    QCOMPARE(page.checkedOutputs().size(), 41);
    QSignalSpy rebuilds(outputs->model(), &QAbstractItemModel::rowsRemoved);
    QTimer::singleShot(0, &page, [&] {
        auto *dialog = page.findChild<QDialog *>("checkedOutputsDialog"); QVERIFY(dialog);
        auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        for (auto *button : buttons->buttons())
            if (buttons->buttonRole(button) == QDialogButtonBox::ActionRole) button->click();
        dialog->reject();
    });
    page.findChild<QToolButton *>("inspectCheckedOutputs")->click();
    QVERIFY(page.checkedOutputs().isEmpty());
    QVERIFY2(rebuilds.size() <= 1, "Clearing a checked list must not rebuild all output rows per item");
}
void WorkspaceTests::checkedReviewClosesBeforeRecordReload()
{
    QTemporaryDir dir; const auto path = dir.filePath("review-reload.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto ref = save(repo, path, run());
    ResultsPage page(makeScientificRegistry()); page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(ref.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    outputs->model()->setData(outputs->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole);
    bool closedImmediately = false;
    QTimer::singleShot(0, &page, [&] {
        auto *dialog = page.findChild<QDialog *>("checkedOutputsDialog"); QVERIFY(dialog);
        page.openRecord(99999); // A failure must not leave a review of old checks open.
        closedImmediately = !dialog->isVisible();
        dialog->reject();
    });
    page.findChild<QToolButton *>("inspectCheckedOutputs")->click();
    QVERIFY(closedImmediately);
    QVERIFY(page.checkedOutputs().isEmpty());
}
void WorkspaceTests::closingLoadingSubplotNotifiesIdle()
{
    QTemporaryDir dir; const auto path = dir.filePath("close-loading.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    const auto second = save(repo, path, run(series(70)));
    const auto replacement = save(repo, path, run(series(90)));
    PlotWorkspace w; w.openBatch({first, second}, PlotWorkspace::Target::Page);
    QTRY_VERIFY(!w.loading());
    QSignalSpy finished(&w, &PlotWorkspace::outputReady);
    w.openBatch({replacement}, PlotWorkspace::Target::Replace);
    w.closeSubplot();
    QTRY_VERIFY(!w.loading());
    QVERIFY(!finished.isEmpty());
    QCOMPARE(w.currentReference().key(), first.key());
    QCOMPARE(w.subplotCount(), 1);
}
void WorkspaceTests::figureExportKeepsCapturedTarget()
{
    QTemporaryDir dir; const auto path = dir.filePath("snapshot.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto first = save(repo, path, run());
    const auto replacement = save(repo, path, run(series(70)));
    PlotWorkspace w; w.resize(800, 600); w.show();
    w.open(first); QTRY_VERIFY(!w.loading());
    auto style = w.currentView()->style(); style.title = "frozen export";
    QVERIFY(w.currentView()->applyStyle(style).isEmpty());
    QTRY_VERIFY(!w.loading());
    w.openBatch({replacement}, PlotWorkspace::Target::Replace);
    const auto captured = w.figureCells(false);
    QCOMPARE(captured.size(), 1);
    QCOMPARE(captured[0].references[0].key(), first.key());
    FigureOptions options; options.path = dir.filePath("captured.png");
    options.pixels = {800, 600}; options.dpi = 96;
    FigureExportController exporter;
    QSignalSpy finished(&exporter, &FigureExportController::finished);
    QVERIFY(exporter.start(captured, options));
    w.open(replacement, PlotWorkspace::Target::Page);
    QTRY_VERIFY(!w.loading() && !exporter.busy());
    QCOMPARE(finished.size(), 1); QVERIFY(finished[0][1].toString().isEmpty());
    auto request = captured[0].request; request.columns = 800; request.rows = 600;
    const auto original = loadPlotData(captured[0].references, request, {});
    QCOMPARE(original.state, NodeState::Succeeded);
    options.path = dir.filePath("expected.png");
    QVERIFY(renderFigure(captured, {original}, options).isEmpty());
    QVERIFY(QImage(dir.filePath("captured.png")) == QImage(options.path));
}
void WorkspaceTests::metadataDoesNotDecodeOtherOutputs()
{
    QTemporaryDir d;
    const auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto r = run();
    auto ref = save(repo, p, r);
    QVERIFY(ref.valid());
    QVERIFY(sql(p, "UPDATE aster_outputs SET data=x'00' WHERE kind='Table'").isEmpty());
    auto summary = repo.load(ref.recordId, true);
    QVERIFY2(summary.ok(), qPrintable(summary.error));
    QVERIFY(summary.run.nodes[ref.nodeId].outputs.isEmpty());
    QCOMPARE(summary.outputs.size(), 2);
    QVERIFY(!repo.load(ref.recordId).ok());
    QVERIFY(repo.loadOutput(ref).ok());
    QCOMPARE(repo.loadPlot(ref).state, NodeState::Succeeded);
    QCOMPARE(repo.loadPlot(ref, 10).state, NodeState::Failed);
    ref.kind = "Spectrum";
    QCOMPARE(repo.loadPlot(ref).state, NodeState::Failed);
}
void WorkspaceTests::referenceRejectsReplacedIdentity()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto ref = save(repo, p, run());
    QCOMPARE(OutputReference::fromJson(ref.toJson()).key(), ref.key());
    ref.runId = QUuid::createUuid();
    QVERIFY(!repo.loadOutput(ref).ok());
}
void WorkspaceTests::databaseSourcePreservesSnapshotAndDependencies()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto original = run();
    auto ref = save(repo, p, original);
    WorkflowDocument document(makeScientificRegistry());
    auto root = document.addDatabaseSource(ref);
    QVERIFY2(root.ok(), qPrintable(root.error));
    auto next = document.addChild(root.nodeId, ChildSlot::Down, "affine");
    QVERIFY(next.ok());
    auto plan = document.plan();
    QVERIFY2(plan.validation.ok(), qPrintable(plan.validation.message()));
    QCOMPARE(plan.requests.size(), 1);
    auto result = WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(result.state, RunState::Succeeded);
    QCOMPARE(encodeData(result.nodes[root.nodeId].outputs["data"]).bytes,
             encodeData(original.nodes[original.root].outputs["series"]).bytes);
    auto stored = repo.save(result);
    QVERIFY2(stored.saved(), qPrintable(stored.error));
    QVERIFY(stored.recordId != ref.recordId);
    QVERIFY(!repo.remove(ref.recordId).isEmpty());
    QVERIFY(repo.remove(stored.recordId).isEmpty());
    QVERIFY(repo.remove(ref.recordId).isEmpty());
}
void WorkspaceTests::sourceFailureRemainsSavable()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto ref = save(repo, p, run());
    WorkflowDocument document(makeScientificRegistry());
    auto root = document.addDatabaseSource(ref);
    QVERIFY(root.ok());
    QVERIFY(repo.remove(ref.recordId).isEmpty());
    auto result = WorkflowExecutor().run(document.plan().requests.front());
    QCOMPARE(result.state, RunState::Failed);
    QVERIFY(repo.save(result).saved());
}
void WorkspaceTests::pagesReleaseBuffersAndRestoreStyle()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto a = save(repo, p, run()), b = save(repo, p, run());
    PlotWorkspace w;
    w.resize(1000, 600);
    w.show();
    w.open(a);
    QTRY_VERIFY_WITH_TIMEOUT(w.currentView()->hasPlot(), 10000);
    auto s = w.currentView()->style();
    s.title = "first page";
    s.curves["series"].color = Qt::red;
    QVERIFY(w.currentView()->applyStyle(s).isEmpty());
    QPointer<PlotView> old = w.currentView();
    std::weak_ptr<const DataObject> buffer = w.currentView()->currentData();
    w.open(b);
    QVERIFY(old.isNull());
    QVERIFY(buffer.expired());
    QTRY_VERIFY(w.currentView()->hasPlot());
    QCOMPARE(w.pageCount(), 2);
    w.findChild<QTabWidget *>("plotPages")->setCurrentIndex(0);
    QTRY_VERIFY(w.currentView()->hasPlot());
    QCOMPARE(w.currentView()->style().title, QString("first page"));
    QCOMPARE(w.currentView()->style().curves["series"].color, QColor(Qt::red));
    w.closePage(1);
    QCOMPARE(w.pageCount(), 1);
    QTRY_VERIFY(w.currentView()->hasPlot());
    w.closePage(0);
    QCOMPARE(w.pageCount(), 0);
    QVERIFY(!w.currentView()->hasOutput());
}
void WorkspaceTests::heatmapInspectorAcrossPages()
{
    QTemporaryDir d; const auto path=d.filePath("heatmap.sqlite"); ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root=model.addRoot("input_series",{0,0}).nodeId;
    const auto cwt=model.addChild(root,ChildSlot::Down,"cwt").nodeId;
    const auto execution=WorkflowExecutor().run({model.freeze().snapshot,root,{"cwt",series(),{}}});
    QCOMPARE(execution.nodes[cwt].state,NodeState::Succeeded);
    const auto receipt=repo.save(execution); QVERIFY(receipt.saved());
    OutputReference heat{path,receipt.recordId,execution.runId,cwt,"power","Grid"};
    auto curve=save(repo,path,run());
    PlotWorkspace w; w.resize(950,650); w.show();
    PlotStyleEditor editor(w.currentView()); editor.resize(330,800); editor.show();
    connect(&w,&PlotWorkspace::currentViewChanged,&editor,&PlotStyleEditor::setView);
    auto *tabs=editor.findChild<QTabWidget*>("plotStyleSections"); QVERIFY(tabs);
    QVERIFY(tabs->isTabVisible(3)); QVERIFY(!tabs->isTabEnabled(3));
    w.open(heat);
    QTRY_VERIFY_WITH_TIMEOUT(w.currentView()->hasPlot(),10000);
    QTRY_VERIFY(tabs->isTabEnabled(3));
    QVERIFY(w.currentView()->style().y.reversed);
    QCOMPARE(w.currentView()->style().colorGradient,4);
    tabs->setCurrentIndex(3);
    auto style=w.currentView()->style(); style.y.reversed=false; style.colorGradient=0;
    QVERIFY(w.currentView()->applyStyle(style).isEmpty());
    w.open(curve);
    QTRY_VERIFY(w.currentView()->hasPlot());
    QVERIFY(tabs->isTabVisible(3)); QVERIFY(!tabs->isTabEnabled(3));
    QVERIFY(tabs->currentIndex()!=3);
    w.findChild<QTabWidget*>("plotPages")->setCurrentIndex(0);
    QTRY_VERIFY(w.currentView()->hasPlot());
    QTRY_VERIFY(tabs->isTabEnabled(3));
    QVERIFY(!w.currentView()->style().y.reversed);
    QCOMPARE(w.currentView()->style().colorGradient,0);
    w.currentView()->resetStyle();
    QVERIFY(w.currentView()->style().y.reversed);
    QCOMPARE(w.currentView()->style().colorGradient,4);
    w.reset(); QVERIFY(!tabs->isTabEnabled(3));
}
void WorkspaceTests::subplotOverlayAndRemoval()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto a = save(repo, p, run()), b = save(repo, p, run());
    PlotWorkspace w;
    w.open(a);
    QTRY_VERIFY(w.currentView()->hasPlot());
    w.open(b, PlotWorkspace::Target::Subplot);
    QTRY_VERIFY(w.currentView()->hasPlot());
    QCOMPARE(w.subplotCount(), 2);
    w.open(a, PlotWorkspace::Target::Overlay);
    QTRY_COMPARE(w.currentView()->plottedCount(), 2);
    auto *cp = w.currentView()->findChild<QCustomPlot *>();
    QCOMPARE(cp->legend->itemCount(), 2);
    w.closeSubplot();
    QCOMPARE(w.subplotCount(), 1);
    QVERIFY(w.currentView()->hasPlot());
}
void WorkspaceTests::closeDuringLoadAndOldMenu()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto a = save(repo, p, run());
    PlotWorkspace w;
    w.open(a);
    QTRY_VERIFY(w.currentView()->hasPlot());
    QWidget owner;
    auto *menu = w.currentView()->curveMenu(&owner);
    QVERIFY(!menu->actions().isEmpty());
    auto *action = menu->actions().front();
    w.closePage(0);
    action->trigger();
    QVERIFY(!w.currentView()->hasOutput());
    w.open(a);
    w.closePage(0);
    QTRY_VERIFY(!w.loading());
    QVERIFY(!w.currentView()->hasOutput());
    w.open(a);
    QTRY_VERIFY(w.currentView()->hasPlot());
    delete menu;
}
void WorkspaceTests::displayEnvelopeKeepsExtremaAndOriginal()
{
    auto raw = series(1000000);
    auto before = encodeData(raw).bytes;
    QElapsedTimer t;
    t.start();
    QTemporaryDir dir; const auto path=dir.filePath("display.sqlite"); ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty()); auto ref=save(repo,path,run(raw));
    auto out=repo.loadPlotPreview(ref,{});
    QCOMPARE(out.state, NodeState::Succeeded);
    auto *s = dataAs<SeriesData>(out.outputs["series"]);
    QVERIFY(s);
    QVERIFY(s->x.size() <= 10000);
    QVERIFY(s->y.contains(500));
    QCOMPARE(s->x.front(), 0.);
    QCOMPARE(s->x.back(), 999999.);
    QCOMPARE(encodeData(raw).bytes, before);
    qInfo() << "one million points ->" << s->x.size() << "display points in" << t.elapsed() << "ms";
}
void WorkspaceTests::largePlotPreparationAndRawExport()
{
    QTemporaryDir d;
    auto p = d.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(p).isEmpty());
    auto r = run(series(1000000));
    auto ref = save(repo, p, r);
    int heartbeats = 0;
    QTimer timer;
    timer.setInterval(1);
    connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
    timer.start();
    PlotWorkspace w;
    w.open(ref);
    QTRY_VERIFY_WITH_TIMEOUT(w.currentView()->hasPlot(), 15000);
    QVERIFY(heartbeats > 0);
    QVERIFY(dataAs<SeriesData>(w.currentView()->currentData())->x.size() <= 10000);
    ExportController exporter;
    QSignalSpy done(&exporter, &ExportController::finished);
    QVERIFY(exporter.startOutput(ref, d.filePath("raw.csv")));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 30000);
    QVERIFY(done.front()[1].toString().isEmpty());
    QFile f(d.filePath("raw.csv"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    qint64 lines = 0;
    while (!f.atEnd())
    {
        f.readLine();
        ++lines;
    }
    QCOMPARE(lines, 1000001);
}
void WorkspaceTests::gridSamplingAndCancellation()
{
    GridData g;
    g.rows = 1024;
    g.columns = 2048;
    g.xAxis = {"Time", "ka"};
    g.yAxis = {"Period", "ka"};
    g.valueAxis = {"Power", "u"};
    for (int i = 0; i < g.columns; ++i)
        g.x.push_back(i);
    for (int i = 0; i < g.rows; ++i)
        g.y.push_back(i + 1);
    g.values.fill(.1, g.rows * g.columns);
    g.values[123456] = 99;
    auto raw = makeData(std::move(g));
    WorkflowModel model(makeScientificRegistry()); auto root=model.addRoot("input_series",{0,0}).nodeId;
    auto id=model.addChild(root,ChildSlot::Down,"cwt").nodeId;
    auto execution=WorkflowExecutor().run({model.freeze().snapshot,root,{"grid",series(),{}}});
    QCOMPARE(execution.nodes[id].state,NodeState::Succeeded);
    execution.nodes[id].outputs={{"power",raw}};
    QTemporaryDir dir; const auto path=dir.filePath("grid.sqlite"); ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty()); const auto receipt=repo.save(execution); QVERIFY(receipt.saved());
    OutputReference ref{path,receipt.recordId,execution.runId,id,"power","Grid"};
    PlotRequest request; request.columns=640; request.rows=256;
    auto out=repo.loadPlotPreview(ref,request);
    auto *small = dataAs<GridData>(out.outputs["power"]);
    QVERIFY(small);
    QCOMPARE(small->columns, 640);
    QCOMPARE(small->rows, 256);
    // Every displayed value remains at its original coordinate. Aggregating a
    // block maximum at another coordinate would misalign significance contours.
    const auto *original = dataAs<GridData>(raw);
    for (qsizetype row = 0; row < small->rows; ++row)
        for (qsizetype col = 0; col < small->columns; ++col)
            QCOMPARE(small->values[row * small->columns + col],
                     original->values[int(small->y[row] - 1) * original->columns + int(small->x[col])]);
    QCOMPARE(out.report["plot_samples"].toObject()["power"].toObject()["color_bounds"].toArray()[1].toDouble(), 99.);
    QCOMPARE(dataAs<GridData>(repo.loadPlot(ref).outputs["power"])->values[123456], 99.);
    QCOMPARE(dataAs<GridData>(raw)->values.size(), 2097152);
    CancellationToken c;
    c.cancel();
    QCOMPARE(repo.loadPlotPreview(ref, request, c).state, NodeState::Cancelled);
}
void WorkspaceTests::cwtSamplingAndViewportRefinement()
{
    SeriesData signal; signal.xAxis = {"Age", "ka"}; signal.yAxis = {"Signal", "u"};
    for (int i = 0; i < 2048; ++i) {
        const double x = .5 * i; signal.x << x;
        signal.y << std::sin(2 * M_PI * x / 32) + .3 * std::sin(2 * M_PI * x / 128);
    }
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto node = model.addChild(root, ChildSlot::Down, "cwt").nodeId;
    const auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"CWT", makeData(signal), {}}});
    QCOMPARE(execution.nodes[node].state, NodeState::Succeeded);
    QTemporaryDir dir; const auto path = dir.filePath("cwt.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    OutputReference ref{path, receipt.recordId, execution.runId, node, "power", "Grid", "cwt"};
    const auto original = execution.nodes[node];
    QCOMPARE(dataAs<GridData>(repo.loadPlot(ref).outputs["power"])->values,
             dataAs<GridData>(original.outputs["power"])->values);
    PlotRequest small; small.columns = 32; small.rows = 32;
    const auto reduced = repo.loadPlotPreview(ref, small);
    QVERIFY(plotPreset(reduced, "power").y.logarithmic);
    QVERIFY(plotPreset(reduced, "power").y.reversed);
    small.columns = PreviewLimits::gridSide; small.rows = PreviewLimits::gridSide;
    const auto storedPreview = repo.loadPlotPreview(ref, small);
    const auto memoryPreview = reducePreviewData(original, {});
    const auto *a = dataAs<GridData>(storedPreview.outputs["power"]);
    const auto *b = dataAs<GridData>(memoryPreview.outputs["power"]);
    QVERIFY(a && b); QCOMPARE(a->x, b->x); QCOMPARE(a->y, b->y); QCOMPARE(a->values, b->values);

    PlotWorkspace workspace; workspace.resize(1100, 700); workspace.show();
    QSignalSpy ready(&workspace, &PlotWorkspace::outputReady);
    workspace.open(ref);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() >= 2 && !workspace.loading(), 10000);
    auto *view = workspace.currentView();
    QVERIFY(view->style().y.logarithmic); QVERIFY(view->style().y.reversed);
    const auto *grid = dataAs<GridData>(view->currentData()); QVERIFY(grid);
    QVERIFY(grid->columns > 384); QCOMPARE(grid->rows, dataAs<GridData>(original.outputs["power"])->rows);
    QCOMPARE(workspace.findChild<QPushButton *>("selectSubplot")->text(),
             QString("ID %1 · %2").arg(receipt.recordId).arg(portLabel("power")));
    auto *status = workspace.findChild<QLabel *>("plotResolution"); QVERIFY(status);
    QVERIFY(status->text().contains(QString::number(grid->columns)));
    const double colorUpper = view->styleWithViewport().colorUpper;
    auto style = view->style(); style.y.reversed = false; style.colorGradient = 1;
    QVERIFY(view->applyStyle(style).isEmpty());
    int previous = ready.size(); view->plotWidget()->xAxis->setRange(200, 220);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() > previous && !workspace.loading(), 10000);
    grid = dataAs<GridData>(view->currentData());
    QVERIFY(grid->columns <= 43); QVERIFY(grid->x.front() <= 200 && grid->x.back() >= 220);
    QVERIFY(view->style().y.logarithmic); QVERIFY(!view->style().y.reversed);
    QCOMPARE(view->style().colorGradient, 1);
    QCOMPARE(view->styleWithViewport().colorUpper, colorUpper);
    QVERIFY(status->text().contains(text("ui.plot_resolution_original").section('%', 0, 0)));
    previous = ready.size(); view->plotWidget()->xAxis->setRange(300, 320);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() > previous && !workspace.loading(), 10000);
    grid = dataAs<GridData>(view->currentData());
    QVERIFY(grid->x.front() >= 299 && grid->x.back() <= 321);
    const auto cells = workspace.figureCells(false);
    QVERIFY(cells.front().style.colorManual); // Export freezes the visible scale only.
    QVERIFY(!view->style().colorManual);
    QCOMPARE(cells.front().style.colorUpper, colorUpper);
    const auto exportData = loadPlotData(cells.front().references, cells.front().request, {});
    QCOMPARE(exportData.state, NodeState::Succeeded);
    FigureOptions options; options.path = dir.filePath("cwt.png"); options.pixels = {1100, 700};
    QVERIFY(renderFigure(cells, {exportData}, options).isEmpty());
    QVERIFY(QFileInfo(options.path).size() > 1000);
    QVERIFY(!view->style().y.reversed); QCOMPARE(view->style().colorGradient, 1);
    const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        view->resetStyle(); view->fit();
        QTest::qWait(600); QTRY_VERIFY(!workspace.loading());
        workspace.grab().save(captures + "/cwt_results.png");
        PlotView preview; preview.resize(1100, 650); preview.showOutput(memoryPreview, "power"); preview.show();
        QTest::qWait(50); preview.grab().save(captures + "/cwt_preview.png");
    }
}
void WorkspaceTests::refinementKeepsPendingViewport()
{
    PlotView view;
    const auto result = NodeResult::success({{"series", series()}});
    view.showOutput(result, "series");
    view.setOutputReference({"test.sqlite", 1, QUuid::createUuid(), QUuid::createUuid(), "series", "Series"});
    QSignalSpy changed(&view, &PlotView::viewportChanged);
    view.plotWidget()->xAxis->setRange(10, 20);
    // An older read can finish before the new gesture's debounce timeout.
    view.refreshOutput(result);
    QCOMPARE(view.plotWidget()->xAxis->range(), QCPRange(10, 20));
    QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 1000);
    QTest::qWait(2 * PlotLimits::debounceMs);
    QCOMPARE(changed.size(), 1); // Rendering must not create a refinement loop.
    view.refreshOutput(result);
    QTest::qWait(2 * PlotLimits::debounceMs);
    QCOMPARE(changed.size(), 1); // An idle completion schedules no extra read.
}
void WorkspaceTests::resetStyleRequestsFullViewport()
{
    QTemporaryDir dir; const auto path = dir.filePath("reset.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto ref = save(repo, path, run(series(4096)));
    PlotWorkspace workspace; workspace.resize(1000, 600); workspace.show();
    QSignalSpy ready(&workspace, &PlotWorkspace::outputReady);
    workspace.open(ref);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() >= 2 && !workspace.loading(), 10000);
    auto *view = workspace.currentView();
    auto count = ready.size(); view->plotWidget()->xAxis->setRange(200, 220);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() > count && !workspace.loading(), 10000);
    QVERIFY(dataAs<SeriesData>(view->currentData())->x.back() < 222);
    count = ready.size(); view->resetStyle();
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() > count && !workspace.loading(), 10000);
    QVERIFY(dataAs<SeriesData>(view->currentData())->x.front() <= 0);
    QVERIFY(dataAs<SeriesData>(view->currentData())->x.back() >= 4095);
    QVERIFY(!view->style().x.manualRange);
    QTest::qWait(2 * PlotLimits::debounceMs);
    QVERIFY(!workspace.loading());
}
void WorkspaceTests::resolutionStatusHasStableSizeHints()
{
    PlotWorkspace workspace;
    workspace.resize(450, 500); workspace.show();
    // A valid reference creates the cell synchronously; no database read is
    // needed to test its status label's layout contract.
    QTemporaryDir directory;
    workspace.open({directory.filePath("missing.sqlite"), 1, QUuid::createUuid(),
                    QUuid::createUuid(), "series", "Series"});
    auto *status = workspace.findChild<QLabel *>("plotResolution"); QVERIFY(status);
    const auto originalFont = status->font();
    for (int extra : {0, 4}) {
        auto font = originalFont; font.setPointSizeF(std::max(9., font.pointSizeF()) + extra);
        status->setFont(font);
        status->setText("Loading");
        const auto hint = status->sizeHint(), minimum = status->minimumSizeHint();
        for (const auto &message : {QString(), QString("正在细化绘图…"),
             QString("当前视区：显示 205 × 128 / 原始 2048 × 128"),
             QString("Current viewport: 205 x 128 / original 2048 x 128")}) {
            status->setText(message);
            QCOMPARE(status->sizeHint(), hint);
            QCOMPARE(status->minimumSizeHint(), minimum);
        }
        QVERIFY(hint.height() >= status->fontMetrics().height());
    }
}
void WorkspaceTests::refinementStatusDoesNotRelayout_data()
{
    QTest::addColumn<QString>("language");
    QTest::newRow("zh") << QString("zh");
    QTest::newRow("en") << QString("en");
}
void WorkspaceTests::refinementStatusDoesNotRelayout()
{
    QFETCH(QString, language); setLanguage(language);
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    GridData grid; grid.columns = 2048; grid.rows = 128;
    grid.xAxis = {"Time", "ka"}; grid.yAxis = {"Depth", "m"}; grid.valueAxis = {"Power", "u"};
    for (int i = 0; i < grid.columns; ++i) grid.x << i;
    for (int i = 0; i < grid.rows; ++i) grid.y << i + 1;
    grid.values.fill(.5, grid.columns * grid.rows);
    auto execution = run();
    execution.nodes[execution.root] = NodeResult::success({{"power", makeData(grid)}, {"raw_power", makeData(grid)}});
    QTemporaryDir dir; const auto path = dir.filePath("layout.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    PlotWorkspace workspace; workspace.resize(450, 500); workspace.show();
    QSignalSpy ready(&workspace, &PlotWorkspace::outputReady);
    workspace.openBatch({{path, receipt.recordId, execution.runId, execution.root, "power", "Grid", "input_series"},
                         {path, receipt.recordId, execution.runId, execution.root, "raw_power", "Grid", "input_series"}},
                        PlotWorkspace::Target::Subplot);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() >= 4 && !workspace.loading(), 10000);
    QTest::qWait(500); QTRY_VERIFY(!workspace.loading());
    const auto count = ready.size();
    QTest::qWait(1000);
    QCOMPARE(ready.size(), count);
    QVERIFY(!workspace.loading());
    for (const auto *status : workspace.findChildren<QLabel *>("plotResolution"))
        QVERIFY(status->text().contains("2048"));
    const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) workspace.grab().save(captures + "/precision_narrow_" + language + ".png");
    setLanguage("zh");
}
void WorkspaceTests::rejectedOverlayKeepsPendingViewport_data()
{
    QTest::addColumn<bool>("cancelCandidate");
    QTest::newRow("incompatible_units") << false;
    QTest::newRow("removed_candidate") << true;
}
void WorkspaceTests::rejectedOverlayKeepsPendingViewport()
{
    QFETCH(bool, cancelCandidate);
    QTemporaryDir dir; const auto path = dir.filePath("rejected.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto target = save(repo, path, run(series(4096)));
    auto incompatible = *dataAs<SeriesData>(series()); incompatible.yAxis.unit = "other";
    auto candidate = save(repo, path, run(makeData(incompatible)));
    candidate.method = "input_series";
    PlotWorkspace workspace; workspace.resize(1000, 600); workspace.show();
    QSignalSpy ready(&workspace, &PlotWorkspace::outputReady);
    workspace.open(target);
    QTRY_VERIFY_WITH_TIMEOUT(ready.size() >= 2 && !workspace.loading(), 10000);
    auto *view = workspace.currentView();
    QSignalSpy changed(view, &PlotView::viewportChanged);
    QSignalSpy messages(&workspace, &PlotWorkspace::message);
    // Hold the candidate read until the new gesture's debounce has fired.
    // This exercises the real queued worker completion, without timing guesses.
    const auto connection = QUuid::createUuid().toString();
    {
        auto blocker = QSqlDatabase::addDatabase("QSQLITE", connection);
        auto cleanup = qScopeGuard([&] { blocker.close(); blocker = {}; QSqlDatabase::removeDatabase(connection); });
        blocker.setDatabaseName(path); QVERIFY(blocker.open());
        QSqlQuery lock(blocker); QVERIFY(lock.exec("BEGIN EXCLUSIVE"));
        workspace.openBatch({candidate}, PlotWorkspace::Target::Overlay);
        view->plotWidget()->xAxis->setRange(200, 220);
        QTest::qWait(2 * PlotLimits::debounceMs);
        QVERIFY(changed.size() >= 1); QVERIFY(workspace.loading());
        if (cancelCandidate) workspace.forgetRecord(path, candidate.recordId);
        QVERIFY(lock.exec("ROLLBACK"));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty() && !workspace.loading(), 10000);
    QCOMPARE(workspace.currentReference().key(), target.key());
    QTRY_VERIFY_WITH_TIMEOUT(dataAs<SeriesData>(view->currentData())->x.back() < 222, 2000);
    QCOMPARE(view->plotWidget()->xAxis->range(), QCPRange(200, 220));
}
void WorkspaceTests::scientificOutputGroups()
{
    QTemporaryDir dir; const auto path = dir.filePath("groups.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto cwt = model.addChild(root, ChildSlot::Down, "cwt").nodeId;
    const auto mtm = model.addChild(root, ChildSlot::Left, "mtm").nodeId;
    auto input = *dataAs<SeriesData>(series(1024));
    for (qsizetype i = 0; i < input.y.size(); ++i)
        input.y[i] = std::sin(.4 * i) + .3 * std::sin(1.3 * i);
    const auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"groups", makeData(input), {}}});
    QCOMPARE(execution.nodes[cwt].state, NodeState::Succeeded);
    QCOMPARE(execution.nodes[mtm].state, NodeState::Succeeded);
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    const auto ref = [&](NodeId node, QString port, QString kind, QString method) {
        return OutputReference{path, receipt.recordId, execution.runId, node, port, kind, method};
    };
    QVector<OutputReference> wavelet{
        ref(cwt, "coi", "Series", "cwt"), ref(cwt, "power", "Grid", "cwt"),
        ref(cwt, "raw_power", "Grid", "cwt"), ref(cwt, "significance_ratio", "Grid", "cwt")};
    QVERIFY(execution.nodes[cwt].outputs.contains("significance_ratio"));
    QCOMPARE(plotGroups(wavelet).size(), 3);
    QCOMPARE(plotGroups(wavelet, true).size(), 4);
    PlotWorkspace workspace; workspace.resize(1100, 700); workspace.show();
    QSignalSpy messages(&workspace, &PlotWorkspace::message);
    workspace.openBatch(wavelet, PlotWorkspace::Target::Subplot);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.loading(), 10000);
    QTest::qWait(300); QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.subplotCount(), 3);
    const auto cells = workspace.figureCells(true);
    QVERIFY2(cells.size() == 3, qPrintable(messages.isEmpty() ? QString("No figures") : messages.last()[0].toString()));
    for (const auto &cell : cells) {
        QCOMPARE(cell.references.size(), 2);
        QCOMPARE(cell.references[1].port, QString("coi"));
        QVERIFY(cell.style.y.logarithmic); QVERIFY(cell.style.y.reversed);
        const auto loaded = loadPlotData(cell.references, cell.request, {});
        QCOMPARE(loaded.state, NodeState::Succeeded);
        QVERIFY(!loaded.outputs.contains("overlay_1"));
    }
    const auto target = workspace.currentReference().key();
    workspace.openBatch({wavelet[1]}, PlotWorkspace::Target::Overlay);
    QCOMPARE(workspace.currentReference().key(), target);
    QCOMPARE(workspace.figureCells(false)[0].references.size(), 2);
    workspace.reset();
    QVector<OutputReference> spectra;
    for (auto it = execution.nodes[mtm].outputs.cbegin(); it != execution.nodes[mtm].outputs.cend(); ++it)
        if (dataAs<SpectrumData>(it.value()) || it.key() == "peaks")
            spectra << ref(mtm, it.key(), dataAs<SpectrumData>(it.value()) ? "Spectrum" : "Table", "mtm");
    QVERIFY(spectra.size() > 8);
    QCOMPARE(plotGroups(spectra).size(), 1);
    workspace.openBatch(spectra, PlotWorkspace::Target::Subplot);
    QTRY_VERIFY_WITH_TIMEOUT(!workspace.loading(), 10000);
    QTest::qWait(300); QTRY_VERIFY(!workspace.loading());
    QCOMPARE(workspace.subplotCount(), 1);
    QVERIFY(workspace.currentView()->hasPlot());
    QCOMPARE(workspace.currentReference().port, QString("spectrum"));
    QCOMPARE(workspace.figureCells(false)[0].references.size(), spectra.size());
    const auto plotted = workspace.currentView()->style().curves;
    for (const auto &item : spectra) {
        QVERIFY2(plotted.contains(item.port), qPrintable(item.port));
        QVERIFY2(plotted[item.port].visible, qPrintable(item.port));
    }
    // No heuristic merging across records or unregistered task types.
    auto unrelated = spectra.front(); unrelated.recordId++; unrelated.runId = QUuid::createUuid();
    QCOMPARE(plotGroups({spectra.front(), unrelated}).size(), 2);
    for (auto &item : spectra) item.method = "unknown_method";
    QCOMPARE(plotGroups(spectra).size(), spectra.size());
    const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) workspace.grab().save(captures + "/mtm_group.png");
}
void WorkspaceTests::addAllScientificOutputs_data()
{
    QTest::addColumn<QString>("method");
    QTest::addColumn<int>("expectedGroups");
    QTest::newRow("cwt_four_heatmaps") << QString("cwt") << 4;
    QTest::newRow("mtm_one_spectrum_family") << QString("mtm") << 1;
}
void WorkspaceTests::addAllScientificOutputs()
{
    QFETCH(QString, method); QFETCH(int, expectedGroups);
    QTemporaryDir dir; const auto path = dir.filePath("add-all.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto node = model.addChild(root, ChildSlot::Down, method).nodeId;
    auto input = *dataAs<SeriesData>(series(256));
    for (qsizetype i = 0; i < input.y.size(); ++i)
        input.y[i] = std::sin(.4 * i) + .3 * std::sin(1.3 * i);
    const auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"all outputs", makeData(input), {}}});
    QCOMPARE(execution.nodes[node].state, NodeState::Succeeded);
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    ResultsPage page(makeScientificRegistry()); page.resize(1450, 950); page.show(); page.setDatabase(path);
    QSignalSpy loaded(&page, &ResultsPage::recordLoaded);
    page.openRecord(receipt.recordId); QTRY_COMPARE(loaded.size(), 1);
    auto *workspace = page.workspace(); QTRY_VERIFY(!workspace->loading());
    QCOMPARE(workspace->subplotCount(), 1);
    auto *outputs = page.findChild<QTableView *>("resultOutputs");
    for (int i = 0; i < outputs->model()->rowCount(); ++i)
        outputs->model()->setData(outputs->model()->index(i, 0), Qt::Checked, Qt::CheckStateRole);
    auto *add = page.findChild<QPushButton *>("plotCheckedOutputs");
    QCOMPARE(add->text(), text("ui.add_plot_count").arg(expectedGroups));
    auto *tabs = workspace->findChild<QTabWidget *>("plotPages");
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        add->click(); QTRY_VERIFY(!workspace->loading());
        int total = 0;
        for (int i = 0; i < workspace->pageCount(); ++i)
        {
            tabs->setCurrentIndex(i); QTRY_VERIFY(!workspace->loading());
            const auto cells = workspace->figureCells(true);
            QCOMPARE(cells.size(), workspace->subplotCount());
            total += cells.size();
            for (const auto &cell : cells)
            {
                const auto result = loadPlotData(cell.references, cell.request, {});
                QCOMPARE(result.state, NodeState::Succeeded);
                const auto primary = cell.references.front();
                if (method == "cwt")
                {
                    QVERIFY(waveletGrid(primary));
                    QVERIFY(dataAs<GridData>(result.outputs[primary.port]));
                    QVERIFY(dataAs<SeriesData>(result.outputs["coi"]));
                    QVERIFY(cell.style.y.logarithmic); QVERIFY(cell.style.y.reversed);
                }
                else QCOMPARE(primary.port, QString("spectrum"));
            }
        }
        QCOMPARE(total, expectedGroups + (cycle == 0 ? 1 : 0));
        if (cycle == 0)
        {
            const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
            if (!captures.isEmpty()) QVERIFY(page.grab().save(captures + "/all_" + method + ".png"));
        }
        // Remove every plot, retaining the checked outputs, then add them again.
        while (workspace->pageCount()) workspace->closeSubplot();
        QTRY_VERIFY(!workspace->loading());
    }
}
void WorkspaceTests::advancedStyleAndDynamicInspector()
{
    PlotView a, b;
    a.showOutput(NodeResult::success({{"series", series()}}), "series");
    b.showOutput(NodeResult::success({{"series", series()}}), "series");
    auto s = a.style();
    s.swapXY = true;
    s.x.secondaryVisible = true;
    s.x.secondaryLabels = true;
    s.y.subGrid = true;
    s.y.tickCount = 8;
    s.curves["series"].connection = 3;
    s.curves["series"].fill = true;
    s.curves["series"].inLegend = false;
    QVERIFY2(a.applyStyle(s).isEmpty(), qPrintable(a.applyStyle(s)));
    auto *cp = a.findChild<QCustomPlot *>();
    QCOMPARE(cp->graph(0)->keyAxis(), cp->yAxis);
    QVERIFY(cp->yAxis2->visible());
    QVERIFY(cp->xAxis->grid()->subGridVisible());
    QCOMPARE(cp->legend->itemCount(), 0);
    QCOMPARE(cp->graph(0)->lineStyle(), QCPGraph::lsStepRight);
    PlotStyleEditor editor(&a);
    editor.setView(&b);
    auto *ticks = editor.findChild<QSpinBox *>("axisTickCount");
    QVERIFY(ticks);
    ticks->setValue(11);
    QVERIFY(editor.apply());
    QCOMPARE(b.style().x.tickCount, 11);
    QVERIFY(a.style().x.tickCount != 11);
    editor.setView(nullptr);
    QVERIFY(!editor.apply());
}
void WorkspaceTests::reuseButtonBindsSource()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("ui.sqlite");
    MainWindow window(settings, false);
    window.show();
    auto fixture = aster::testsupport::installWorkspaceFixture(window);
    QVERIFY(fixture);
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && window.results()->recordId() > 0, 10000);
    window.results()->selectStep(1, true);
    QTRY_VERIFY(dataAs<SeriesData>(window.results()->plotView()->currentData()));
    auto *button = window.findChild<QPushButton *>("reuseOutput");
    QVERIFY(button);
    button->click();
    QCOMPARE(window.document()->model().definition().nodes.size(), 7);
    auto plan = window.document()->plan();
    QVERIFY2(plan.validation.ok(), qPrintable(plan.validation.message()));
    QCOMPARE(plan.requests.size(), 4);
    bool found = false;
    for (const auto &n : window.document()->model().definition().nodes)
        if (n.method == "database_series")
        {
            found = true;
            QCOMPARE(window.document()->sources().value(n.id).size(), 1);
            QVERIFY(window.document()->addChild(n.id, ChildSlot::Down, "affine").ok());
            break;
        }
    QVERIFY(found);
}
void WorkspaceTests::rejectedOverlayKeepsOriginal()
{
    QTemporaryDir dir;
    auto path = dir.filePath("unit.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto ref = save(repo, path, run());
    auto other = *dataAs<SeriesData>(series());
    other.xAxis.unit = "s";
    auto second = save(repo, path, run(makeData(other)));
    PlotWorkspace w;
    w.open(ref);
    QTRY_VERIFY(w.currentView()->hasPlot());
    auto old = w.currentView()->style();
    old.title = "Keep me";
    QVERIFY(w.currentView()->applyStyle(old).isEmpty());
    QSignalSpy messages(&w, &PlotWorkspace::message);
    w.open(second, PlotWorkspace::Target::Overlay);
    QTRY_VERIFY(!messages.isEmpty());
    QTRY_VERIFY(!w.loading() && w.currentView()->hasPlot());
    QCOMPARE(w.currentView()->plottedCount(), 1);
    QCOMPARE(w.currentView()->style().title, QString("Keep me"));
    auto missing = dir.filePath("missing.sqlite");
    ResultRepository reader;
    QVERIFY(!reader.open(missing, false).isEmpty());
    QVERIFY(!QFileInfo::exists(missing));
}

void WorkspaceTests::cocoPlotUsesSelectedAxes()
{
    SeriesData input;
    input.xAxis = {"Depth", "m"};
    input.yAxis = {"Proxy", "u"};
    for (int i = 0; i < 128; ++i) {
        input.x << .5 * i;
        input.y << std::sin(.17 * i) + .3 * std::cos(.39 * i);
    }
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto task = model.addChild(root, ChildSlot::Down, "coco_monte_carlo",
        {{"coco_periods", "405,125,95"}, {"coco_powers", "1,.6,.3"},
         {"coco_rate_min", 2}, {"coco_rate_max", 6}, {"coco_rate_step", .5},
         {"coco_max_frequency", .02}, {"coco_simulations", 19}});
    QVERIFY(task.ok());
    const auto execution = WorkflowExecutor().run(
        {model.freeze().snapshot, root, {"depth", makeData(input), {}}});
    QCOMPARE(execution.state, RunState::Succeeded);
    QTemporaryDir dir;
    const auto path = dir.filePath("coco.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    const auto receipt = repo.save(execution);
    QVERIFY2(receipt.saved(), qPrintable(receipt.error));
    const OutputReference ref{path, receipt.recordId, execution.runId, task.nodeId,
                              "scan_adjusted_p", "Series"};
    const auto loaded = repo.loadPlotPreview(ref, {});
    QCOMPARE(loaded.state, NodeState::Succeeded);
    PlotView view;
    view.showOutput(loaded, ref.port);
    QVERIFY(view.hasPlot());
    const auto request = view.plotRequest();
    // Frequencies and simulation numbers must not widen a rate axis.
    QVERIFY2(request.xLower > 1.5 && request.xUpper < 6.5,
             qPrintable(QString("Incorrect rate range [%1,%2]")
                        .arg(request.xLower).arg(request.xUpper)));
    QVERIFY(!view.style().curves.contains("null_maxima"));
    QVERIFY(!view.style().curves.contains("best_observed"));
    // Direct live preview and persisted plots offer the same compatible curves.
    PlotView preview;
    preview.showOutput(execution.nodes.value(task.nodeId), ref.port);
    QCOMPARE(preview.style().curves.keys(), view.style().curves.keys());
    view.showOutput(loaded, "best_observed");
    QVERIFY(view.style().curves.contains("best_target"));
    QVERIFY(!view.style().curves.contains(ref.port));
    const auto frequency = view.plotRequest();
    QVERIFY(frequency.xLower >= 0 && frequency.xUpper < .025);
}
void WorkspaceTests::fitIgnoresHiddenCurveBounds()
{
    auto result = NodeResult::success({
        {"series", makeData(SeriesData{{0, 1, 2}, {1, 2, 3}, {"X", "m"}, {"Y", "u"}})},
        {"trend", makeData(SeriesData{{0, 1, 2}, {100, 200, 300}, {"X", "m"}, {"Y", "u"}})}});
    result.report["bounds"] = QJsonArray{0, 2, 1, 300};
    result.report["output_bounds"] = QJsonObject{
        {"series", QJsonArray{0, 2, 1, 3}}, {"trend", QJsonArray{0, 2, 100, 300}}};
    PlotView view;
    view.showOutput(result, "series");
    QVERIFY(view.plotRequest().yUpper < 4);
    auto style = view.style();
    style.curves["trend"].visible = true;
    QVERIFY(view.applyStyle(style).isEmpty());
    view.fit();
    QVERIFY(view.plotRequest().yUpper >= 300);
    style.curves["trend"].visible = false;
    QVERIFY(view.applyStyle(style).isEmpty());
    view.fit();
    QVERIFY(view.plotRequest().yUpper < 4);
    auto zoomed = result;
    zoomed.report["output_bounds"] = QJsonObject{
        {"series", QJsonArray{.8, 1.2, 1, 3}}, {"trend", QJsonArray{.8, 1.2, 100, 300}}};
    view.refreshOutput(zoomed);
    view.fit();
    QVERIFY(view.plotRequest().xLower <= 0 && view.plotRequest().xUpper >= 2);
    // Swapping axes preserves logical bounds and series identities.
    style.swapXY = true;
    QVERIFY(view.applyStyle(style).isEmpty());
    view.fit();
    QVERIFY(view.plotRequest().yUpper < 4);
}


void WorkspaceTests::fitRestoresFlatCurveBounds_data()
{
    QTest::addColumn<double>("level");
    QTest::addColumn<bool>("logarithmic");
    QTest::newRow("positive") << 3.0 << false;
    QTest::newRow("zero") << 0.0 << false;
    QTest::newRow("negative") << -4.0 << false;
    QTest::newRow("small_units") << 1e-12 << false;
    QTest::newRow("logarithmic") << 3.0 << true;
}
void WorkspaceTests::fitRestoresFlatCurveBounds()
{
    QFETCH(double, level);
    QFETCH(bool, logarithmic);
    QTemporaryDir dir;
    const auto path = dir.filePath("flat.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    SeriesData data;
    data.xAxis = {"Time", "ka"};
    data.yAxis = {"Constant", "u"};
    for (int i = 0; i <= 1000; ++i) {
        data.x.push_back(i);
        data.y.push_back(level);
    }
    const auto ref = save(repo, path, run(makeData(data)));
    const auto full = repo.loadPlotPreview(ref, {});
    QCOMPARE(full.state, NodeState::Succeeded);
    PlotView view;
    view.showOutput(full, ref.port);
    auto style = view.style();
    style.y.logarithmic = logarithmic;
    style.x.manualRange = true;
    style.x.lower = 400;
    style.x.upper = 450;
    QVERIFY(view.applyStyle(style).isEmpty());
    const auto zoomed = repo.loadPlotPreview(ref, view.plotRequest());
    QCOMPARE(zoomed.state, NodeState::Succeeded);
    const auto *sampled = dataAs<SeriesData>(zoomed.outputs.value(ref.port));
    QVERIFY(sampled);
    QVERIFY(sampled->x.size() < data.x.size());
    view.refreshOutput(zoomed);
    view.fit();
    auto range = view.plotRequest();
    QVERIFY2(range.xLower <= 0 && range.xUpper >= 1000,
             qPrintable(QString("Flat curve fit lost full X bounds: %1..%2")
                            .arg(range.xLower).arg(range.xUpper)));
    QVERIFY(range.yLower < level && range.yUpper > level);
    if (logarithmic) QVERIFY(range.yLower > 0);
    style = view.style();
    style.swapXY = true;
    QVERIFY(view.applyStyle(style).isEmpty());
    view.fit();
    range = view.plotRequest();
    QVERIFY(range.xLower <= 0 && range.xUpper >= 1000);
    QVERIFY(range.yLower < level && range.yUpper > level);
}

void WorkspaceTests::declinedCloseRestoresPreview()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("close.sqlite");
    MainWindow window(settings, false);
    window.resize(1500, 850);
    window.show();
    auto *preview = window.findChild<WorkflowPreviewPanel *>();
    QVERIFY(preview);
    QTRY_COMPARE(preview->controller()->state(), WorkflowPreviewController::State::Idle);
    auto *exporter = window.results()->exporter();
    QVERIFY(exporter->start(series(10000), dir.filePath("export.csv")));
    QTimer::singleShot(0, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            QTest::mouseClick(box->button(QMessageBox::No), Qt::LeftButton);
    });
    window.close();
    QVERIFY(window.isVisible());
    QTRY_VERIFY(!exporter->busy());
    // Declining to close must not disable future live previews for this session.
    QCOMPARE(preview->controller()->state(), WorkflowPreviewController::State::Idle);
}
void WorkspaceTests::arrangementGeometryAndTransitions()
{
    const QVector<QUuid> ids{QUuid::createUuid(), QUuid::createUuid(), QUuid::createUuid(), QUuid::createUuid()};
    auto tree = PlotArrangement::defaults(ids.mid(0, 3));
    QCOMPARE(tree.regions()[ids[2]], QRectF(0, .5, 1, .5));
    QVERIFY(tree.move(ids[2], {}, PlotPlacement::Above, true));
    QCOMPARE(tree.regions()[ids[2]], QRectF(0, 0, 1, .5));
    QCOMPARE(tree.order(), QVector<QUuid>({ids[2], ids[0], ids[1]}));
    QVERIFY(tree.move(ids[0], ids[1], PlotPlacement::Below));
    QCOMPARE(tree.order(), QVector<QUuid>({ids[2], ids[1], ids[0]}));
    QVERIFY(tree.append(ids[3]));
    QVERIFY(!tree.append(QUuid::createUuid()));
    const auto unchanged = tree.regions();
    QVERIFY(!tree.move(ids[0], ids[0], PlotPlacement::Swap));
    QVERIFY(!tree.move(QUuid::createUuid(), ids[1], PlotPlacement::Left));
    QCOMPARE(tree.regions(), unchanged);
    QRandomGenerator random(170019);
    QElapsedTimer elapsed; elapsed.start();
    for (int iteration = 0; iteration < 4000; ++iteration)
    {
        const auto source = ids[random.bounded(4)], target = ids[random.bounded(4)];
        tree.move(source, target, PlotPlacement(random.bounded(5)), random.bounded(3) == 0);
        if (iteration % 11 == 0) { QVERIFY(tree.remove(source)); QVERIFY(tree.append(source)); }
        QVERIFY(tree.valid());
        const auto order = tree.order();
        QCOMPARE(QSet<QUuid>(order.cbegin(), order.cend()).size(), 4);
        const auto regions = tree.regions();
        double total = 0;
        for (int a = 0; a < 4; ++a)
        {
            const auto area = regions[ids[a]];
            QVERIFY(area.width() > 0 && area.height() > 0);
            total += area.width() * area.height();
            QVERIFY(area.left() >= 0 && area.top() >= 0 && area.right() <= 1.000001 && area.bottom() <= 1.000001);
            for (int b = a + 1; b < 4; ++b)
            {
                const auto overlap = area.intersected(regions[ids[b]]);
                QVERIFY(overlap.width() < 1e-10 || overlap.height() < 1e-10);
            }
        }
        QVERIFY(std::abs(total - 1.) < 1e-10);
    }
    qInfo() << "4000 layout transitions (ms):" << elapsed.elapsed();
    for (auto id : ids) QVERIFY(tree.remove(id));
    QVERIFY(tree.empty());
    QVERIFY(tree.valid(true));
    QVERIFY(!PlotArrangement::defaults({ids[0], ids[0]}).valid());
}
void WorkspaceTests::subplotDragTargetsAndCancellation()
{
    PlotLayoutWidget host; host.resize(1100, 760);
    const QVector<QUuid> ids{QUuid::createUuid(), QUuid::createUuid(), QUuid::createUuid()};
    QMap<QUuid, QWidget *> panels;
    for (int i = 0; i < ids.size(); ++i)
    {
        auto *panel = new QWidget;
        panel->setMinimumSize(160, 140);
        auto *layout = new QVBoxLayout(panel);
        layout->addWidget(host.createTitle(ids[i], QString("Plot %1").arg(i + 1)));
        layout->addWidget(new QLabel(QString("Data %1").arg(i + 1)), 1);
        panels[ids[i]] = panel;
    }
    QVERIFY(host.arrange(PlotArrangement::defaults(ids), panels, ids));
    host.show(); QTest::qWait(80);
    auto *viewport = host.viewport();
    auto point = [&](QUuid id) { return panels[id]->mapTo(viewport, panels[id]->rect().center()); };
    auto sendDrop = [&](const QMimeData *mime, QPoint position) {
        QDragEnterEvent enter(position, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &enter);
        if (!enter.isAccepted()) return false;
        QDragMoveEvent move(position, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        QDropEvent drop(position, Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &drop);
        QCoreApplication::processEvents();
        return drop.isAccepted();
    };
    std::unique_ptr<QMimeData> mime(host.dragData(ids[2])); QVERIFY(mime);
    auto *title = panels[ids[0]]->findChild<QPushButton *>("selectSubplot");
    QVERIFY(sendDrop(mime.get(), title->mapTo(viewport, title->rect().center()))); // Title center swaps too.
    QCOMPARE(host.snapshot().order(), QVector<QUuid>({ids[2], ids[1], ids[0]}));
    QCOMPARE(host.findChildren<QPushButton *>("selectSubplot").size(), 3);
    QVERIFY(sendDrop(mime.get(), QPoint(viewport->width() / 2, 2))); // Whole-page top.
    QCOMPARE(host.snapshot().order().front(), ids[2]);
    QVERIFY(std::abs(host.snapshot().regions()[ids[2]].width() - 1.) < 1e-10);
    auto *third = panels[ids[0]];
    const auto below = third->mapTo(viewport, QPoint(third->width() / 2, third->height() - 4));
    std::unique_ptr<QMimeData> first(host.dragData(ids[1]));
    QVERIFY(sendDrop(first.get(), below)); // Split a specific destination.
    QCOMPARE(host.snapshot().order(), QVector<QUuid>({ids[2], ids[0], ids[1]}));
    const auto before = host.snapshot().regions();
    QVERIFY(!sendDrop(first.get(), point(ids[1]))); // Self drop does nothing.
    QCOMPARE(host.snapshot().regions(), before);
    QMimeData external; external.setText("file:///arbitrary");
    QVERIFY(!sendDrop(&external, point(ids[0])));
    // Even a valid payload is local to its originating page widget.
    PlotLayoutWidget other;
    auto *one = new QWidget, *two = new QWidget;
    QVERIFY(other.arrange(PlotArrangement::defaults({ids[0], ids[1]}), {{ids[0], one}, {ids[1], two}}, {ids[0], ids[1]}));
    std::unique_ptr<QMimeData> foreign(other.dragData(ids[0]));
    QVERIFY(!sendDrop(foreign.get(), point(ids[0])));
    QDragEnterEvent enter(point(ids[0]), Qt::MoveAction, first.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &enter);
    QDragMoveEvent move(point(ids[0]), Qt::MoveAction, first.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QVERIFY(host.findChild<QWidget *>("plotDropPreview")->isVisible());
    QDragLeaveEvent leave; QApplication::sendEvent(viewport, &leave);
    QVERIFY(!host.findChild<QWidget *>("plotDropPreview")->isVisible());
    QCOMPARE(host.snapshot().regions(), before);
    auto remaining = host.snapshot(); QVERIFY(remaining.remove(ids[1]));
    delete panels.take(ids[1]);
    QVERIFY(host.arrange(remaining, panels, {ids[0], ids[2]}));
    QVERIFY(!sendDrop(first.get(), point(ids[0]))); // Stale removed identity.
}
void WorkspaceTests::subplotArrangementKeepsDataAndTargets()
{
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    QTemporaryDir dir; const auto path = dir.filePath("arrangement.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    QVector<OutputReference> refs;
    for (int i = 0; i < 4; ++i) refs.push_back(save(repo, path, run(series(30000 + i * 1000))));
    PlotWorkspace w; w.resize(1250, 900); w.show();
    QVector<QPointer<PlotView>> views;
    for (int i = 0; i < 3; ++i)
    {
        w.open(refs[i], PlotWorkspace::Target::Subplot);
        QTRY_VERIFY(w.currentView()->hasPlot());
        views.push_back(w.currentView());
    }
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    auto *host = w.findChild<PlotLayoutWidget *>(); QVERIFY(host);
    const auto ids = host->snapshot().order(); QCOMPARE(ids.size(), 3);
    auto regions = host->snapshot().regions();
    auto *title = views[2]->parentWidget()->findChild<QPushButton *>("selectSubplot");
    QTest::mouseClick(title, Qt::LeftButton);
    QVERIFY(title->isChecked());
    QCOMPARE(w.currentView(), views[2].data());
    QVERIFY(regions[ids[2]].width() > .99);
    const auto fullWidth = views[2]->parentWidget()->width();
    QVERIFY(fullWidth > views[0]->parentWidget()->width() * 1.8);
    auto style = views[2]->style(); style.title = "stable target";
    style.x.manualRange = true; style.x.lower = 10000; style.x.upper = 15000;
    QVERIFY(views[2]->applyStyle(style).isEmpty());
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    // A queued overlay stays attached to its original cell while it moves.
    w.open(refs[3], PlotWorkspace::Target::Overlay);
    QVERIFY(host->moveCell(ids[2], {}, PlotPlacement::Above, true));
    QCOMPARE(w.currentView(), views[2].data());
    QCOMPARE(w.currentReference().key(), refs[2].key());
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    QCOMPARE(w.currentView()->style().title, QString("stable target"));
    QCOMPARE(w.currentView()->plotWidget()->xAxis->range().lower, 10000.);
    QCOMPARE(w.figureCells(false).front().references.size(), 2);
    QVERIFY(host->moveCell(ids[0], ids[1], PlotPlacement::Swap));
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    QCOMPARE(w.currentView(), views[2].data());
    for (const auto &view : views) QVERIFY(view);
    QCOMPARE(w.findChildren<PlotView *>().size(), 4); // Three cells and the empty view only.
    QCOMPARE(w.figureCells(true).front().references.front().key(), refs[2].key());
    QCOMPARE(w.figureCells(false).front().area, QRectF(0, 0, 1, 1));
    QVERIFY(w.destinationLabel().contains(text("ui.plot_destination").arg(1).arg(1)));
    auto *split = host->findChild<QSplitter *>("plotDivider"); QVERIFY(split);
    split->setSizes({570, 250}); emit split->splitterMoved(570, 1);
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    const auto saved = w.figureCells(true);
    const auto ratio = saved.front().area.height();
    QVERIFY(ratio > .6);
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(w.grab().save(capture + "/layout_three_top_span.png"));
    QSignalSpy idle(&w, &PlotWorkspace::outputReady);
    QTest::qWait(450); QCOMPARE(idle.count(), 0); // No geometry/read feedback loop.
    w.open(refs[3], PlotWorkspace::Target::Page); QTRY_VERIFY(w.currentView()->hasPlot());
    for (const auto &view : views) QVERIFY(view.isNull()); // Inactive-page memory bound unchanged.
    w.findChild<QTabWidget *>()->setCurrentIndex(0);
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    host = w.findChild<PlotLayoutWidget *>(); QVERIFY(host);
    const auto restored = w.figureCells(true);
    QCOMPARE(restored.size(), saved.size());
    for (int i = 0; i < saved.size(); ++i)
    {
        QCOMPARE(restored[i].references.front().key(), saved[i].references.front().key());
        QVERIFY(std::abs(restored[i].area.height() - saved[i].area.height()) < .02);
    }
    w.openDefault(refs[2]); QCOMPARE(w.currentView()->style().title, QString("stable target"));
    w.closeSubplot(); QTest::qWait(250); QTRY_VERIFY(!w.loading());
    QCOMPARE(w.subplotCount(), 2);
    host->resetArrangement(); QCoreApplication::processEvents();
    w.open(refs[2], PlotWorkspace::Target::Subplot); QTest::qWait(250); QTRY_VERIFY(!w.loading());
    const auto again = w.figureCells(true);
    QVERIFY(again.back().area.width() > .99); // Reset returns additions to the default three-cell layout.
    if (!capture.isEmpty()) QVERIFY(w.grab().save(capture + "/layout_three_default.png"));
}
void WorkspaceTests::subplotDividerDefersRefresh()
{
    QTemporaryDir dir; const auto path = dir.filePath("divider.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto ref = save(repo, path, run(series(200000)));
    PlotWorkspace w; w.resize(1200, 700); w.show();
    w.open(ref); QTRY_VERIFY(w.currentView()->hasPlot());
    w.open(ref, PlotWorkspace::Target::Subplot); QTRY_VERIFY(w.currentView()->hasPlot());
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    auto *host = w.findChild<PlotLayoutWidget *>(); QVERIFY(host);
    auto *split = host->findChild<QSplitter *>("plotDivider"); QVERIFY(split);
    QVERIFY(!split->opaqueResize()); QVERIFY(!split->childrenCollapsible());
    const auto sizes = split->sizes();
    auto *handle = split->handle(1); QVERIFY(handle);
    const auto center = handle->rect().center();
    QSignalSpy ready(&w, &PlotWorkspace::outputReady);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, center);
    for (int dx = 10; dx <= 130; dx += 10)
    {
        const auto pos = center + QPoint(dx, 0);
        QMouseEvent event(QEvent::MouseMove, pos, handle->mapToGlobal(pos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &event);
    }
    QTest::qWait(220);
    QCOMPARE(split->sizes(), sizes);
    QCOMPARE(ready.count(), 0);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, center + QPoint(130, 0));
    QTest::qWait(250); QTRY_VERIFY(!w.loading());
    QVERIFY(split->sizes()[0] > sizes[0] + 80);
    QVERIFY(ready.count() <= 4); // At most one final refinement per cell, allowing a settling resize.
    QTest::mouseDClick(handle, Qt::LeftButton);
    QVERIFY(std::abs(split->sizes()[0] - split->sizes()[1]) <= 1);
    w.resize(350, 300); QTest::qWait(100);
    QVERIFY(host->horizontalScrollBar()->maximum() > 0); // Small windows scroll instead of collapsing plots.
}

void WorkspaceTests::fourHeatmapsKeepBuffersAndSettle()
{
    GridData grid; grid.rows = 256; grid.columns = 4096;
    grid.xAxis = {"Time", "ka"}; grid.yAxis = {"Period", "ka"}; grid.valueAxis = {"Power", "u"};
    for (int x = 0; x < grid.columns; ++x) grid.x.push_back(x * .25);
    for (int y = 0; y < grid.rows; ++y) grid.y.push_back(std::pow(2., y / 32.));
    grid.values.reserve(grid.rows * grid.columns);
    for (int y = 0; y < grid.rows; ++y)
        for (int x = 0; x < grid.columns; ++x) grid.values.push_back(1. + .6 * std::sin(.05 * y) * std::sin(.03 * x));
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto node = model.addChild(root, ChildSlot::Down, "cwt").nodeId;
    auto execution = WorkflowExecutor().run({model.freeze().snapshot, root, {"layout grid fixture", series(), {}}});
    QCOMPARE(execution.nodes[node].state, NodeState::Succeeded);
    execution.nodes[node].outputs = {{"power", makeData(std::move(grid))}};
    QTemporaryDir dir; const auto path = dir.filePath("four-grid.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto receipt = repo.save(execution); QVERIFY(receipt.saved());
    OutputReference ref{path, receipt.recordId, execution.runId, node, "power", "Grid", "cwt"};
    PlotWorkspace w; w.resize(1400, 1000); w.show();
    QVector<QPointer<PlotView>> views;
    for (int i = 0; i < 4; ++i)
    {
        w.open(ref, PlotWorkspace::Target::Subplot);
        QTRY_VERIFY_WITH_TIMEOUT(w.currentView()->hasPlot(), 10000);
        views.push_back(w.currentView());
    }
    QTest::qWait(250); QTRY_VERIFY_WITH_TIMEOUT(!w.loading(), 10000);
    QVector<DataRef> buffers;
    for (const auto &view : views) buffers.push_back(view->currentData());
    auto *host = w.findChild<PlotLayoutWidget *>(); QVERIFY(host);
    const auto ids = host->snapshot().order();
    QSignalSpy ready(&w, &PlotWorkspace::outputReady);
    QElapsedTimer elapsed; elapsed.start();
    for (int i = 0; i < 12; ++i)
        QVERIFY(host->moveCell(ids[i % 4], {}, i % 2 ? PlotPlacement::Above : PlotPlacement::Left, true));
    qInfo() << "12 four-heatmap layout submissions (ms):" << elapsed.elapsed();
    QCOMPARE(ready.count(), 0); // Structural moves reuse buffers; no synchronous I/O or repaint of data.
    for (int i = 0; i < 4; ++i)
    {
        QVERIFY(views[i]); QCOMPARE(views[i]->currentData(), buffers[i]);
        QVERIFY(views[i]->style().y.logarithmic && views[i]->style().y.reversed);
    }
    QTest::qWait(250); QTRY_VERIFY_WITH_TIMEOUT(!w.loading(), 10000);
    qInfo() << "Four heatmaps settled after layout (ms):" << elapsed.elapsed() << "completions:" << ready.count();
    QVERIFY(ready.count() <= 8);
    const auto settled = ready.count(); QTest::qWait(450); QCOMPARE(ready.count(), settled);
    for (const auto &view : views) QVERIFY(dataAs<GridData>(view->currentData()));
    const auto capture = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) QVERIFY(w.grab().save(capture + "/layout_four_heatmaps.png"));
}

QTEST_MAIN(WorkspaceTests)
#include "workspace_tests.moc"
