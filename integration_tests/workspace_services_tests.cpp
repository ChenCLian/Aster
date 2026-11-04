#include "integration_suites.h"
#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QImageReader>
#include <QSqlQuery>
#include <QLineEdit>
#include "processing/scientific_registry.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "ui/results/plot_workspace.h"
#include "ui/panels/plot_style_editor.h"
#include "ui/settings/theme_manager.h"
#include "ui/settings/language_catalog.h"
#include "ui/common/app_settings.h"
#include "ui/common/presentation.h"
#include "ui/exports/figure_export_controller.h"
#include "third_party/qcustomplot/qcustomplot.h"
using namespace aster;
using namespace aster::ui;
namespace
{
DataRef signal(int n = 512)
{
    SeriesData d;
    d.xAxis = {"Time", "ka"};
    d.yAxis = {"Signal", "u"};
    for (int i = 0; i < n; ++i)
    {
        d.x.push_back(i);
        d.y.push_back(std::sin(i * .1));
    }
    if (n > 10000)
        d.y[n / 2 + 5] = 900;
    return makeData(std::move(d));
}
RunResult run(DataRef data = signal())
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("input_series", {0, 0}).nodeId;
    return WorkflowExecutor().run({model.freeze().snapshot, root, {"fixture", data, {}}});
}
OutputReference save(ResultRepository &repo, const QString &path, RunResult r,
                     QString port = "series", QString kind = "Series")
{
    auto receipt = repo.save(r);
    return {path, receipt.recordId, r.runId, r.root, port, kind};
}
} // namespace
class WorkspaceServicesTests : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        applyTheme(*qApp);
        setLanguage("en");
    }
    void settingsRoundTripAndValidation();
    void catalogsAndThemes();
    void viewportReaderPreservesSpikesAndRefines();
    void gridWindowHasExactValues();
    void tablePreviewIsBounded();
    void rejectsWrongIdentityAndCancellation();
    void workspaceRefinesWithoutChangingViewport();
    void extendedStylesAndThemePreserveCustomCurves();
    void inactivePagesFollowTheme();
    void heatmapScaleAndBackground();
    void figurePngPdfAndAtomicFailure();
    void figureControllerSnapshotAndCancellation();
    void figureCompletionBeforeRestart();
};
void WorkspaceServicesTests::settingsRoundTripAndValidation()
{
    QTemporaryDir dir;
    auto path = dir.filePath("nested/settings.ini");
    AppSettings s;
    s.databasePath = dir.filePath("数据/results.sqlite");
    s.language = "en";
    s.theme = "dark";
    s.concurrency = 7;
    QVERIFY2(s.save(path).isEmpty(), qPrintable(s.save(path)));
    auto loaded = AppSettings::load(path);
    QCOMPARE(loaded.databasePath, s.databasePath);
    QCOMPARE(loaded.language, s.language);
    QCOMPARE(loaded.theme, s.theme);
    QCOMPARE(loaded.concurrency, 7);
    s.language = "xx";
    s.theme = "bad";
    s.concurrency = 999;
    QVERIFY(s.save(path).isEmpty());
    loaded = AppSettings::load(path);
    QCOMPARE(loaded.language, QString("zh"));
    QCOMPARE(loaded.theme, QString("light"));
    QCOMPARE(loaded.concurrency, 32);
    QVERIFY(!s.save(dir.path()).isEmpty());
}
void WorkspaceServicesTests::catalogsAndThemes()
{
    auto &catalog = LanguageCatalog::instance();
    QVERIFY(catalog.load("zh"));
    QCOMPARE(text("ui.settings"), QString::fromUtf8("设置"));
    QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QString::fromUtf8("取消"));
    QVERIFY(catalog.load("en"));
    QCOMPARE(text("ui.settings"), QString("Settings"));
    QVERIFY(!catalog.load("fr"));
    QCOMPARE(catalog.label("new.key", "Fallback"), QString("Fallback"));
    QFile en(":/i18n/en.json"), zh(":/i18n/zh.json");
    QVERIFY(en.open(QIODevice::ReadOnly));
    QVERIFY(zh.open(QIODevice::ReadOnly));
    auto a = QJsonDocument::fromJson(en.readAll()).object(),
         b = QJsonDocument::fromJson(zh.readAll()).object();
    for (auto i = a.begin(); i != a.end(); ++i)
        QVERIFY2(b.contains(i.key()), qPrintable(i.key()));
    auto &theme = ThemeManager::instance();
    QVERIFY(theme.apply(*qApp, "dark").isEmpty());
    QVERIFY(qApp->palette().color(QPalette::Window).lightnessF() < .3);
    QVERIFY(!qApp->styleSheet().contains('@'));
    QVERIFY(theme.apply(*qApp, "light").isEmpty());
    QVERIFY(qApp->palette().color(QPalette::Window).lightnessF() > .8);
}
void WorkspaceServicesTests::viewportReaderPreservesSpikesAndRefines()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto ref = save(repo, path, run(signal(200000)));
    QVERIFY(ref.valid());
    PlotRequest request;
    request.columns = 64;
    auto overview = repo.loadPlotPreview(ref, request);
    QVERIFY2(overview.state == NodeState::Succeeded, qPrintable(overview.error));
    auto *d = dataAs<SeriesData>(overview.outputs["series"]);
    QVERIFY(d);
    QVERIFY(d->x.size() <= 258);
    QVERIFY(d->y.contains(900));
    request.window = true;
    request.xLower = 1001;
    request.xUpper = 1021;
    request.yLower = -1;
    request.yUpper = 1;
    auto zoom = repo.loadPlotPreview(ref, request);
    QVERIFY2(zoom.state == NodeState::Succeeded, qPrintable(zoom.error));
    d = dataAs<SeriesData>(zoom.outputs["series"]);
    QVERIFY(d);
    QCOMPARE(d->x.size(), 23);
    QCOMPARE(d->x.front(), 1000.);
    QCOMPARE(d->x.back(), 1022.);
    for (int i = 0; i < d->x.size(); ++i)
        QCOMPARE(d->y[i], std::sin(d->x[i] * .1));
    QVERIFY(!zoom.report["display_reduced"].toBool());
    auto raw = repo.loadOutput(ref);
    QCOMPARE(dataAs<SeriesData>(raw.data)->x.size(), 200000);
}
void WorkspaceServicesTests::gridWindowHasExactValues()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    GridData g;
    g.columns = 2048;
    g.rows = 512;
    g.xAxis = {"Time", "ka"};
    g.yAxis = {"Period", "ka"};
    g.valueAxis = {"Power", "u2"};
    for (int c = 0; c < g.columns; ++c)
        g.x.push_back(c);
    for (int r = 0; r < g.rows; ++r)
    {
        g.y.push_back(r + 1);
        for (int c = 0; c < g.columns; ++c)
            g.values.push_back(r * 10000 + c);
    }
    auto record = run();
    record.nodes[record.root].outputs["power"] = makeData(g);
    auto ref = save(repo, path, record, "power", "Grid");
    QVERIFY(ref.valid());
    PlotRequest request;
    request.window = true;
    request.xLower = 100;
    request.xUpper = 130;
    request.yLower = 31;
    request.yUpper = 51;
    request.columns = 64;
    request.rows = 64;
    auto out = repo.loadPlotPreview(ref, request);
    QVERIFY2(out.state == NodeState::Succeeded, qPrintable(out.error));
    auto *grid = dataAs<GridData>(out.outputs["power"]);
    QVERIFY(grid);
    QCOMPARE(grid->columns, 33);
    QCOMPARE(grid->rows, 23);
    for (int r = 0; r < grid->rows; ++r)
        for (int c = 0; c < grid->columns; ++c)
            QCOMPARE(grid->values[r * grid->columns + c], (grid->y[r] - 1) * 10000 + grid->x[c]);
    QVERIFY(out.report["bytes_read"].toDouble() < encodeData(makeData(g)).bytes.size() / 2);
}
void WorkspaceServicesTests::tablePreviewIsBounded()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    TableData t;
    t.columns = {"a", "b"};
    for (int i = 0; i < 15000; ++i)
        t.rows.push_back({double(i), double(i + 1)});
    auto record = run();
    record.nodes[record.root].outputs["table"] = makeData(t);
    auto ref = save(repo, path, record, "table", "Table");
    auto out = repo.loadPlotPreview(ref, {});
    QVERIFY2(out.state == NodeState::Succeeded, qPrintable(out.error));
    QCOMPARE(dataAs<TableData>(out.outputs["table"])->rows.size(), 10000);
    QVERIFY(out.report["display_reduced"].toBool());
}
void WorkspaceServicesTests::rejectsWrongIdentityAndCancellation()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto ref = save(repo, path, run());
    CancellationToken cancel;
    cancel.cancel();
    QCOMPARE(repo.loadPlotPreview(ref, {}, cancel).state, NodeState::Cancelled);
    ref.runId = QUuid::createUuid();
    QCOMPARE(repo.loadPlotPreview(ref, {}).state, NodeState::Failed);
}
void WorkspaceServicesTests::workspaceRefinesWithoutChangingViewport()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto ref = save(repo, path, run(signal(100000)));
    PlotWorkspace workspace;
    workspace.resize(1000, 650);
    workspace.show();
    workspace.open(ref);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.currentView()->hasPlot(), 10000);
    auto *view = workspace.currentView();
    PlotStyleEditor editor(view);
    auto *title = editor.findChild<QLineEdit *>("plotTitle");
    title->setText("Draft kept through refinement");
    auto *plot = view->plotWidget();
    plot->xAxis->setRange(1001, 1021);
    plot->yAxis->setRange(-.5, .5);
    QTRY_VERIFY_WITH_TIMEOUT(dataAs<SeriesData>(view->currentData())->x.size() == 23, 10000);
    QCOMPARE(plot->xAxis->range().lower, 1001.);
    QCOMPARE(plot->xAxis->range().upper, 1021.);
    QCOMPARE(plot->yAxis->range().upper, .5);
    QCOMPARE(title->text(), QString("Draft kept through refinement"));
    QVERIFY(editor.apply());
    QCOMPARE(plot->xAxis->range().lower, 1001.);
    view->fit();
    QTRY_VERIFY_WITH_TIMEOUT(dataAs<SeriesData>(view->currentData())->x.size() > 23, 10000);
    QVERIFY(plot->xAxis->range().upper > 99000);
}
void WorkspaceServicesTests::extendedStylesAndThemePreserveCustomCurves()
{
    PlotView view;
    view.showOutput(NodeResult::success({{"series", signal()}}), "series");
    auto s = view.style();
    s.curves["series"].color = Qt::magenta;
    s.curves["series"].marker = MarkerStyle::Diamond;
    s.curves["series"].fill = true;
    s.curves["series"].fillPattern = Qt::CrossPattern;
    s.x.secondaryVisible = true;
    s.x.independentSecondary = true;
    s.x.secondaryColor = Qt::red;
    s.x.secondaryWidth = 3;
    s.x.secondaryCount = 4;
    QVERIFY(view.applyStyle(s).isEmpty());
    QCOMPARE(view.plotWidget()->xAxis2->basePen().color(), QColor(Qt::red));
    QCOMPARE(view.plotWidget()->graph(0)->brush().style(), Qt::CrossPattern);
    QVERIFY(ThemeManager::instance().apply(*qApp, "dark").isEmpty());
    QVERIFY(view.style().background.lightnessF() < .3);
    QCOMPARE(view.style().curves["series"].color, QColor(Qt::magenta));
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
    s = view.style();
    s.backgroundImage = "/missing/image.png";
    QVERIFY(!view.applyStyle(s).isEmpty());
}
void WorkspaceServicesTests::inactivePagesFollowTheme()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto a = save(repo, path, run()), b = save(repo, path, run());
    PlotWorkspace workspace;
    workspace.resize(1000, 600);
    workspace.show();
    workspace.open(a);
    QTRY_VERIFY(workspace.currentView()->hasPlot());
    workspace.open(b);
    QTRY_VERIFY(workspace.currentView()->hasPlot());
    QVERIFY(ThemeManager::instance().apply(*qApp, "dark").isEmpty());
    const auto expected = workspace.currentView()->style().curves["series"].color;
    workspace.open(a);
    QTRY_VERIFY(workspace.currentView()->hasPlot());
    QCOMPARE(workspace.currentView()->style().curves["series"].color, expected);
    QCOMPARE(workspace.currentView()->style().background, themeColor("plot"));
    QVERIFY(ThemeManager::instance().apply(*qApp, "light").isEmpty());
}
void WorkspaceServicesTests::heatmapScaleAndBackground()
{
    GridData g;
    g.columns = 6;
    g.rows = 4;
    g.x = {0, 1, 2, 3, 4, 5};
    g.y = {1, 2, 3, 4};
    for (int i = 0; i < 24; ++i)
        g.values.push_back(i + 1);
    PlotView view;
    view.showOutput(NodeResult::success({{"power", makeData(g)}}), "power");
    auto style = view.style();
    style.colorGradient = 2;
    style.colorManual = true;
    style.colorLower = 2;
    style.colorUpper = 18;
    style.colorLog = true;
    QVERIFY(view.applyStyle(style).isEmpty());
    auto *map = qobject_cast<QCPColorMap *>(view.plotWidget()->plottable(0));
    QVERIFY(map);
    auto *scale = map->colorScale();
    QVERIFY(scale);
    QCOMPARE(scale->gradient(), map->gradient());
    QCOMPARE(scale->dataRange(), QCPRange(2, 18));
    QCOMPARE(map->dataScaleType(), QCPAxis::stLogarithmic);
    QTemporaryDir dir;
    QImage bg(50, 50, QImage::Format_RGB32);
    bg.fill(Qt::yellow);
    auto path = dir.filePath("background.png");
    QVERIFY(bg.save(path));
    style.backgroundImage = path;
    QVERIFY(view.applyStyle(style).isEmpty());
    QVERIFY(!view.plotWidget()->axisRect()->background().isNull());
    view.resetStyle();
    QVERIFY(view.plotWidget()->axisRect()->background().isNull());
}
void WorkspaceServicesTests::figurePngPdfAndAtomicFailure()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto ref = save(repo, path, run());
    auto data = repo.loadPlotPreview(ref, {});
    PlotView view;
    view.showOutput(data, "series");
    auto style = view.styleWithViewport();
    style.title = "Export fixture";
    view.resize(1000, 700);
    view.show();
    QTest::qWait(20);
    view.plotWidget()->replot();
    QVERIFY(view.plotWidget()->axisRect()->right() > view.plotWidget()->width() - 40);
    FigureCell cell{{ref}, style, view.plotRequest()};
    FigureOptions options;
    options.path = dir.filePath("page.png");
    options.pixels = {1000, 700};
    QVERIFY2(renderFigure({cell, cell}, {data, data}, options).isEmpty(), "PNG render failed");
    QImageReader reader(options.path);
    QCOMPARE(reader.size(), options.pixels);
    options.path = dir.filePath("page.pdf");
    options.format = FigureOptions::Format::Pdf;
    QVERIFY(renderFigure({cell, cell}, {data, data}, options).isEmpty());
    QFile pdf(options.path);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.readAll().startsWith("%PDF-"));
    pdf.close();
    const auto captureDir = qEnvironmentVariable("ASTER_CAPTURE_DIR");
    if (!captureDir.isEmpty())
    {
        QDir().mkpath(captureDir);
        QFile::copy(dir.filePath("page.png"), captureDir + "/page.png");
        QFile::copy(options.path, captureDir + "/page.pdf");
    }
    auto oldSize = QFileInfo(options.path).size();
    options.whitePaper = false;
    cell.style.backgroundImage = "/missing/image.png";
    QVERIFY(!renderFigure({cell}, {data}, options).isEmpty());
    QCOMPARE(QFileInfo(options.path).size(), oldSize);
    options.pixels = {8192, 8192};
    QVERIFY(!validateFigureOptions(options).isEmpty());
}
void WorkspaceServicesTests::figureControllerSnapshotAndCancellation()
{
    QTemporaryDir dir;
    auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto ref = save(repo, path, run(signal(100000)));
    PlotWorkspace workspace;
    workspace.resize(1000, 600);
    workspace.show();
    workspace.open(ref);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.currentView()->hasPlot(), 10000);
    auto cells = workspace.figureCells(false);
    FigureOptions options;
    options.path = dir.filePath("export.png");
    options.pixels = {1000, 700};
    FigureExportController controller;
    QSignalSpy done(&controller, &FigureExportController::finished);
    QVERIFY(controller.start(cells, options));
    workspace.reset();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
    QCOMPARE(done.count(), 1);
    QVERIFY2(done[0][1].toString().isEmpty(), qPrintable(done[0][1].toString()));
    QVERIFY(QFileInfo::exists(options.path));
    options.path = dir.filePath("cancelled.png");
    QVERIFY(controller.start(cells, options));
    controller.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 10000);
    QVERIFY(!QFileInfo::exists(options.path));
    options.path = path;
    QVERIFY(!controller.start(cells, options));
    QVERIFY(repo.loadOutput(ref).ok());
}
void WorkspaceServicesTests::figureCompletionBeforeRestart()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    const auto computation = run();
    const auto ref = save(repo, path, computation);
    PlotView view;
    view.showOutput(computation.nodes[computation.root], "series");
    FigureCell cell{{ref}, view.styleWithViewport(), view.plotRequest()};
    FigureOptions options;
    options.path = dir.filePath("first.png");
    options.pixels = {640, 480};
    FigureExportController controller;
    QStringList events;
    int idleCount = 0;
    connect(&controller, &FigureExportController::finished, this,
            [&](const QString &file, const QString &error, bool cancelled)
            {
                QVERIFY2(error.isEmpty(), qPrintable(error));
                QVERIFY(!cancelled);
                QVERIFY(controller.busy());
                events << QFileInfo(file).fileName();
                QVERIFY(!controller.start({cell}, options));
            });
    connect(&controller, &FigureExportController::busyChanged, this, [&](bool busy)
            {
                if (!busy)
                {
                    events << "idle";
                    if (++idleCount == 1)
                    {
                        options.path = dir.filePath("second.png");
                        QVERIFY(controller.start({cell}, options));
                    }
                }
            });
    QVERIFY(controller.start({cell}, options));
    QTRY_COMPARE_WITH_TIMEOUT(idleCount, 2, 10000);
    QCOMPARE(events, QStringList({"first.png", "idle", "second.png", "idle"}));
    QVERIFY(!controller.busy());
}
int runWorkspaceServicesTests(int argc, char **argv)
{
    WorkspaceServicesTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "workspace_services_tests.moc"
