#include "../tests/support/workspace_fixture.h"
#include "ui/panels/source_panel.h"
#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTabWidget>
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
} // namespace
class WorkspaceTests : public QObject
{
    Q_OBJECT
  private slots:
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
    void subplotOverlayAndRemoval();
    void closeDuringLoadAndOldMenu();
    void displayEnvelopeKeepsExtremaAndOriginal();
    void largePlotPreparationAndRawExport();
    void gridAggregationAndCancellation();
    void advancedStyleAndDynamicInspector();
};
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
void WorkspaceTests::gridAggregationAndCancellation()
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
    QVERIFY(small->values.contains(99));
    QCOMPARE(dataAs<GridData>(raw)->values.size(), 2097152);
    CancellationToken c;
    c.cancel();
    QCOMPARE(repo.loadPlotPreview(ref, request, c).state, NodeState::Cancelled);
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
    window.results()->selectStep(1);
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
QTEST_MAIN(WorkspaceTests)
#include "workspace_tests.moc"
