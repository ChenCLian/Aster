// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "integration_suites.h"
#include <QtTest>
#include <QClipboard>
#include <QMessageBox>
#include "ui/common/error_presenter.h"
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <QListWidget>
#include <QAbstractButton>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QMenu>
#include "ui/common/output_picker.h"
#include <QListWidget>
#include <QMouseEvent>
#include <QToolButton>
#include <QCheckBox>
#include <QScrollBar>
#include "ui/panels/file_preview_panel.h"
#include "ui/panels/workflow_preview_panel.h"
#include <QSplitter>
#include "ui/services/file_preview_loader.h"
#include "ui/app/main_window.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/results/results_page.h"
#include "ui/panels/collapsible_panel.h"
#include <future>
#include <QJsonDocument>
#include <QRegularExpression>
#include "model/workflow_codec.h"
#include "processing/scientific_registry.h"
#include "processing/input_series_task.h"
#include "processing/sine_source_task.h"
#include "io/database/data_codec.h"
#include "io/exports/export_path.h"
#include "io/files/series_file_reader.h"
#include "ui/workflow/workflow_document.h"
#include "ui/panels/source_panel.h"
#include "ui/panels/parameter_editor.h"
#include "ui/help/help_dialog.h"
#include <QTextBrowser>
#include "ui/panels/file_options_editor.h"
#include "ui/panels/task_catalog.h"
#include "ui/services/run_controller.h"
#include "ui/services/export_controller.h"
#include "ui/services/result_loader.h"
#include "ui/services/plot_data_loader.h"
#include "ui/exports/figure_export.h"
#include "ui/results/plot_view.h"
#include "ui/settings/theme_manager.h"
#include "ui/common/presentation.h"
#include "third_party/qcustomplot/qcustomplot.h"
using namespace aster;
using namespace aster::ui;
namespace
{
bool write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
DataRef series()
{
    return makeData(SeriesData{{0, 1, 2, 3}, {1, 3, 5, 7}, {"Time", "ka"}, {"Signal", "u"}});
}
// Test-only extension: a new source key, without any UI/executor dispatch case.
class ExternalSource final : public Task
{
  public:
    explicit ExternalSource(const QJsonObject &) {}
    NodeResult execute(const TaskInput &, const ExecutionContext &c) override
    {
        return NodeResult::success({{"series", c.boundSource}});
    }
};
RunResult record()
{
    WorkflowModel m(makeScientificRegistry());
    auto root = m.addRoot("input_series", {0, 0}).nodeId;
    return WorkflowExecutor().run({m.freeze().snapshot, root, {"test", series(), {}}});
}
} // namespace
class InteractionSafetyTests : public QObject
{
    Q_OBJECT
  private slots:
    void diagnosticSerializationAndTranslations();
    void multipleRootsKeepFileOptionsAndStatusSeparate();
    void diagnosticSignalStorageAndDetails();
    void ar1GlobalResultPresentation();
    void initTestCase()
    {
        applyTheme(*qApp);
        setLanguage("en");
    }
    void fileToBranchesToDatabaseAndCsv();
    void externalSourcesAndGeneratorsNeedNoDispatchEdits();
    void probeDoesNotLosePendingEditsAndBlocksStaleRun();
    void localizedChoicesKeepStableParameterValues();
    void exportServicesProtectSourceDatabaseAndSidecars();
    void cachedPlotStylesFollowTheme();
    void overlayFitIncludesAllLayers();
    void readFailuresStayBounded();
    void themeTemplateAndTaskResourcesCoverDeclaredChoices();
    void bulkSourceRefreshKeepsSelectionsAndOptions();
    void whitespaceReadersRemainIndependent();
    void compactToolbarAndIndependentSidePanels();
    void detrendOutputMenuKeepsExplicitPortBinding();
    void invalidFileIsReportedAndCorrectColumnsRecover();
    void failedStepCannotExportOrReuseAnOlderPlot();
    void filePreviewMatchesWorkflow_data();
    void filePreviewMatchesWorkflow();
    void filePreviewFollowsAllEditorOptions();
    void filePreviewIsLazyAndDiscardsStaleReads();
    void filePreviewKeepsExtremaWithinDisplayBudget();
    void filePreviewPanelResizesAndReleasesHiddenData();
    void filePreviewVisibleOnFirstShow();
    void filePreviewClearsInvalidInputs();
};
void InteractionSafetyTests::fileToBranchesToDatabaseAndCsv()
{
    QTemporaryDir dir;
    auto file = dir.filePath("input.csv"), db = dir.filePath("result.sqlite");
    QByteArray bytes;
    for (int i = 0; i < 128; ++i)
        bytes += QByteArray::number(i) + ',' +
                 QByteArray::number(1 + 2 * i + std::sin(i * .4), 'g', 17) + ",sample\n";
    QVERIFY(write(file, bytes));
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("file_source").nodeId;
    const auto trend = doc.addChild(root, ChildSlot::Down, "linear_detrend").nodeId;
    const auto fit =
        doc.addChild(trend, ChildSlot::Left, "affine", {{"factor", 3}}, "trend").nodeId;
    const auto residual =
        doc.addChild(trend, ChildSlot::Right, "affine", {{"factor", 2}}, "detrended").nodeId;
    const auto mtm =
        doc.addChild(trend, ChildSlot::Down, "mtm", {{"swa", false}}, "detrended").nodeId;
    const auto cwt =
        doc.addChild(residual, ChildSlot::Down, "cwt", {{"significance", false}}).nodeId;
    QVERIFY(!mtm.isNull() && !cwt.isNull());
    doc.setFiles(root, {file});
    auto options = doc.fileOptions(root, file);
    options["x_unit"] = "ka";
    QVERIFY(doc.setFileOptions(root, {file}, options));
    auto plan = doc.plan();
    QVERIFY2(plan.validation.ok(), qPrintable(plan.validation.message()));
    // Frozen plan must not observe subsequent editing.
    QVERIFY(doc.setParameters(fit, {{"factor", 99}}).ok());
    RunController controller;
    QSignalSpy events(&controller, &RunController::eventReady);
    QSignalSpy items(&controller, &RunController::itemReady);
    QSignalSpy batches(&controller, &RunController::batchReady);
    bool wrongThread = false;
    connect(&controller, &RunController::eventReady, this,
            [&](const RunEvent &) { wrongThread |= QThread::currentThread() != qApp->thread(); });
    QVERIFY(controller.start(plan, db, 2));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
    QCOMPARE(batches.size(), 1);
    QCOMPARE(items.size(), 1);
    QVERIFY(!wrongThread);
    const auto item = qvariant_cast<BatchItem>(items[0][0]);
    QCOMPARE(item.computation, RunState::Succeeded);
    QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
    QCOMPARE(events.size(), 6 * 3); // pending, running, completed per node
    ResultRepository repo;
    QVERIFY(repo.open(db).isEmpty());
    const auto loaded = repo.load(item.persistence.recordId);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.run.workflowSnapshot, workflowToJson(plan.requests[0].workflow->definition()));
    const auto &nodes = loaded.run.nodes;
    const auto *input = dataAs<SeriesData>(nodes[root].outputs["series"]);
    QVERIFY(input);
    QCOMPARE(input->x.size(), 128); // unused text column must not consume the first row
    QCOMPARE(input->x.front(), 0.);
    QCOMPARE(input->xAxis.unit, QString("ka"));
    QCOMPARE(nodes[fit].report["factor"].toDouble(), 3.);
    const auto *fitted = dataAs<SeriesData>(nodes[trend].outputs["trend"]);
    const auto *detrended = dataAs<SeriesData>(nodes[trend].outputs["detrended"]);
    for (int i = 0; i < 128; ++i)
    {
        QCOMPARE(dataAs<SeriesData>(nodes[fit].outputs["series"])->y[i], fitted->y[i] * 3);
        QCOMPARE(dataAs<SeriesData>(nodes[residual].outputs["series"])->y[i], detrended->y[i] * 2);
    }
    const auto registry = makeScientificRegistry();
    for (const auto &n : plan.requests[0].workflow->definition().nodes)
        QVERIFY2(
            registry.validateOutput(registry.find(n.method)->descriptor, nodes[n.id]).isEmpty(),
            qPrintable(n.method));
    QVERIFY(dataAs<GridData>(nodes[cwt].outputs["power"]));
    const auto *peaks = dataAs<TableData>(nodes[mtm].outputs["peaks"]);
    QVERIFY(peaks && !peaks->rows.isEmpty());
    RecordFilter filter;
    filter.frequencyUnit = "1/ka";
    filter.minFrequency = filter.maxFrequency = peaks->rows[0][0];
    QCOMPARE(repo.list(filter).records.size(), 1);
    filter.frequencyUnit = "1/yr";
    QVERIFY(repo.list(filter).records.isEmpty());
    OutputReference ref{db, item.persistence.recordId, item.runId, residual, "series", "Series"};
    WorkflowDocument derived(registry);
    auto reused = derived.addDatabaseSource(ref);
    QVERIFY(reused.ok());
    derived.addChild(reused.nodeId, ChildSlot::Down, "series_summary");
    auto saved = BatchRunner().run(derived.plan(), db);
    QCOMPARE(saved.items.size(), 1);
    QVERIFY(saved.items[0].persistence.saved());
    QVERIFY(!repo.remove(ref.recordId).isEmpty());
    auto again = repo.load(saved.items[0].persistence.recordId);
    QVERIFY(again.ok());
    QCOMPARE(encodeData(again.run.nodes[reused.nodeId].outputs["data"]).bytes,
             encodeData(nodes[residual].outputs["series"]).bytes);
    ExportController exporter;
    QSignalSpy exported(&exporter, &ExportController::finished);
    auto csv = dir.filePath("raw.csv");
    QVERIFY(exporter.startOutput(ref, csv));
    QTRY_VERIFY_WITH_TIMEOUT(!exporter.busy(), 10000);
    QCOMPARE(exported.size(), 1);
    QVERIFY(exported[0][1].toString().isEmpty());
    SeriesFileOptions readOptions;
    auto raw = readSeriesFile(csv, readOptions);
    QVERIFY2(raw.ok(), qPrintable(raw.error));
    QCOMPARE(dataAs<SeriesData>(raw.data)->y,
             dataAs<SeriesData>(nodes[residual].outputs["series"])->y);
    ResultLoader loader;
    QSignalSpy ready(&loader, &ResultLoader::ready);
    loader.request(db, 999999);
    loader.request(db, ref.recordId);
    QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
    QCOMPARE(ready[0][0].toLongLong(), ref.recordId);
}
void InteractionSafetyTests::externalSourcesAndGeneratorsNeedNoDispatchEdits()
{
    auto registry = makeScientificRegistry();
    auto external = InputSeriesTask::descriptor();
    external.key = "external_test";
    external.catalogVisible = true;
    QVERIFY(
        registry
            .add(external, [](const QJsonObject &p) { return std::make_unique<ExternalSource>(p); })
            .ok());
    auto generator = SineSourceTask::descriptor();
    generator.key = "new_generator";
    QVERIFY(registry
                .add(generator,
                     [](const QJsonObject &p) { return std::make_unique<SineSourceTask>(p); })
                .ok());
    WorkflowDocument doc(registry);
    const auto root = doc.addRoot(external.key).nodeId;
    const auto generated = doc.addRoot(generator.key).nodeId;
    QVERIFY(doc.setSources(root, {{"A", series(), {}}, {"B", series(), {}}}).isEmpty());
    QVERIFY(!doc.setSources(generated, {{"A", series(), {}}}).isEmpty());
    QVERIFY(!doc.setSources(root, {{"wrong", makeData(TableData{{"v"}, {{1}}}), {}}}).isEmpty());
    auto plan = doc.plan();
    QVERIFY(plan.validation.ok());
    QCOMPARE(plan.requests.size(), 3);
    for (const auto &request : plan.requests)
        QCOMPARE(WorkflowExecutor().run(request).state, RunState::Succeeded);
    SourcePanel panel(&doc);
    panel.refresh();
    QCOMPARE(panel.findChild<QComboBox *>("sourceRoot")->count(), 1);
    TaskCatalog catalog(registry);
    auto *tree = catalog.findChild<QTreeWidget *>("taskCatalog");
    QSet<QString> keys;
    QTreeWidgetItemIterator it(tree);
    for (; *it; ++it)
        keys.insert((*it)->data(0, Qt::UserRole).toString());
    QVERIFY(keys.contains(external.key) && keys.contains(generator.key));
    QVERIFY(!keys.contains("input_series") && !keys.contains("database_series"));
    QVERIFY(doc.removeSubtree(root).ok());
    QCOMPARE(doc.plan().requests.size(), 1);
    QVERIFY(doc.undo().ok());
    QCOMPARE(doc.plan().requests.size(), 3);
}
void InteractionSafetyTests::probeDoesNotLosePendingEditsAndBlocksStaleRun()
{
    QTemporaryDir dir;
    auto file = dir.filePath("input.csv");
    QVERIFY(write(file, "X,Y,Other\n0,1,9\n1,2,8\n"));
    WorkflowDocument doc(makeScientificRegistry());
    SourcePanel panel(&doc);
    panel.addFiles({file});
    auto *editor = panel.findChild<FileOptionsEditor *>();
    QVERIFY(editor);
    QVERIFY(!panel.applyCurrentOptions()); // pending preview
    QTRY_VERIFY(editor->ready());
    auto *unit = editor->findChild<QLineEdit *>("fileXUnit");
    unit->setText("ka");
    panel.refresh();
    QCOMPARE(unit->text(), QString("ka"));
    QVERIFY(editor->ready());
    auto *y = editor->findChild<QComboBox *>("fileYColumn");
    y->setCurrentIndex(2);
    QTRY_VERIFY(editor->ready());
    QVERIFY(panel.applyCurrentOptions());
    QCOMPARE(
        doc.plan().requests[0].source.provenance["file_options"].toObject()["y_column"].toInt(), 2);
    QCOMPARE(
        doc.plan().requests[0].source.provenance["file_options"].toObject()["x_unit"].toString(),
        QString("ka"));
    QTRY_VERIFY(editor->ready());
    y->setCurrentIndex(-1);
    QVERIFY(!panel.applyCurrentOptions());
}
void InteractionSafetyTests::localizedChoicesKeepStableParameterValues()
{
    auto registry = makeScientificRegistry();
    WorkflowDocument doc(registry);
    auto root = doc.addRoot("sine_source").nodeId;
    auto step = doc.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    setLanguage("zh");
    ParameterEditor editor;
    editor.showNode(doc.node(step), registry);
    auto *combo = editor.findChild<QComboBox *>("parameter_spacing_mode");
    QVERIFY(combo);
    QCOMPARE(combo->currentText(), QString::fromUtf8("中位间隔"));
    combo->setCurrentIndex(combo->findData("fixed"));
    QCOMPARE(editor.parameters()["spacing_mode"].toString(), QString("fixed"));
    QVERIFY(doc.setParameters(step, editor.parameters()).ok());
    setLanguage("en");
    editor.showNode(doc.node(step), registry);
    combo = editor.findChild<QComboBox *>("parameter_spacing_mode");
    QCOMPARE(combo->currentText(), QString("Fixed interval"));
    QCOMPARE(editor.parameters()["spacing_mode"].toString(), QString("fixed"));
}
void InteractionSafetyTests::exportServicesProtectSourceDatabaseAndSidecars()
{
    QTemporaryDir dir;
    const auto db = dir.filePath("r.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(db).isEmpty());
    auto r = record();
    auto receipt = repo.save(r);
    QVERIFY(receipt.saved());
    OutputReference ref{db, receipt.recordId, r.runId, r.root, "series", "Series"};
    ExportController controller;
    QSignalSpy finished(&controller, &ExportController::finished);
    for (const auto &suffix : {"", "-journal", "-wal", "-shm"})
    {
        QVERIFY(conflictsWithDatabase(db + suffix, {db}));
        QVERIFY(!controller.startOutput(ref, db + suffix));
        QVERIFY(!controller.busy());
        QVERIFY(!finished.last()[1].toString().isEmpty());
    }
#ifdef Q_OS_UNIX
    const auto alias = dir.filePath("alias.sqlite");
    QVERIFY(QFile::link(db, alias));
    QVERIFY(!controller.startOutput(ref, alias));
    const auto linkedDir = dir.filePath("aliasdir");
    QVERIFY(QFile::link(dir.path(), linkedDir));
    QVERIFY(!controller.startOutput(ref, linkedDir + "/r.sqlite-journal"));
#endif
    QVERIFY(repo.load(receipt.recordId).ok());
    QVERIFY(!conflictsWithDatabase(dir.filePath("raw.csv"), {db}));
}
void InteractionSafetyTests::cachedPlotStylesFollowTheme()
{
    ThemeManager::instance().apply(*qApp, "light");
    PlotView view;
    auto out = NodeResult::success({{"series", series()}});
    view.showOutput(out, "series", "first");
    auto custom = view.style();
    custom.curves["series"].color = QColor("#ff00bb");
    QVERIFY(view.applyStyle(custom).isEmpty());
    view.showOutput(out, "series", "second");
    auto oldDefault = view.style().curves["series"].color;
    view.showOutput(out, "series", "third");
    QVERIFY(ThemeManager::instance().apply(*qApp, "dark").isEmpty());
    view.showOutput(out, "series", "first");
    QCOMPARE(view.style().background, themeColor("plot"));
    QCOMPARE(view.style().curves["series"].color, QColor("#ff00bb"));
    view.showOutput(out, "series", "second");
    QCOMPARE(view.style().background, themeColor("plot"));
    QVERIFY(view.style().curves["series"].color != oldDefault);
    QCOMPARE(view.findChild<QCustomPlot *>()->xAxis->tickLabelColor(), themeColor("plotText"));
    ThemeManager::instance().apply(*qApp, "light");
}
void InteractionSafetyTests::overlayFitIncludesAllLayers()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("overlay.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    QVector<OutputReference> refs;
    for (int i = 0; i < 2; ++i)
    {
        auto r = record();
        if (i)
        {
            auto d = *dataAs<SeriesData>(r.nodes[r.root].outputs["series"]);
            for (auto &x : d.x)
                x += 10;
            for (auto &y : d.y)
                y += 1000;
            r.nodes[r.root].outputs["series"] = makeData(d);
        }
        auto receipt = repo.save(r);
        QVERIFY(receipt.saved());
        refs.push_back({path, receipt.recordId, r.runId, r.root, "series", "Series"});
    }
    auto data = loadPlotData(refs, {}, {});
    QCOMPARE(data.state, NodeState::Succeeded);
    PlotView view;
    view.showOutput(data, "series");
    view.fit();
    auto *plot = view.findChild<QCustomPlot *>();
    QVERIFY(plot->xAxis->range().contains(13));
    QVERIFY(plot->yAxis->range().contains(1007));
    FigureOptions options;
    options.path = path;
    QVERIFY(!renderFigure({{refs, view.styleWithViewport(), view.plotRequest()}}, {data}, options)
                 .isEmpty());
    QCOMPARE(repo.list().records.size(), 2);
}
void InteractionSafetyTests::readFailuresStayBounded()
{
    QTemporaryDir dir;
    auto file = dir.filePath("long.csv");
    QVERIFY(write(file, QByteArray(70000, '1') + ",2\n0,1\n"));
    SeriesFileOptions options;
    options.autoHeader = true;
    QVERIFY(!readSeriesFile(file, options).ok());
    QVERIFY(!probeSeriesFile(file, options).ok());
    QVERIFY(write(file, "0,1,note\n1,2,note\n"));
    auto probe = probeSeriesFile(file, options);
    QVERIFY(probe.ok() && !probe.header);
    QCOMPARE(dataAs<SeriesData>(readSeriesFile(file, options).data)->x.size(), 2);
    ResultRepository unopened;
    QCOMPARE(unopened.loadPlot({}).state, NodeState::Failed);
    QCOMPARE(unopened.loadPlotPreview({}, {}).state, NodeState::Failed);
}
void InteractionSafetyTests::themeTemplateAndTaskResourcesCoverDeclaredChoices()
{
    auto readJson = [](const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return QJsonObject{};
        return QJsonDocument::fromJson(f.readAll()).object();
    };
    auto en = readJson(":/i18n/en.json"), zh = readJson(":/i18n/zh.json");
    for (const auto &d : makeScientificRegistry().descriptors())
        for (const auto &p : d.parameters)
            for (const auto &c : p.choices)
            {
                auto key = "choice." + p.key + "." + c;
                QVERIFY2(en.contains(key) && zh.contains(key), qPrintable(key));
            }
    auto light = readJson(":/themes/light.json"), dark = readJson(":/themes/dark.json");
    QCOMPARE(light.keys(), dark.keys());
    for (const auto &v : light)
        QVERIFY(QColor(v.toString()).isValid());
    for (const auto &v : dark)
        QVERIFY(QColor(v.toString()).isValid());
    QFile qss(":/themes/base.qss");
    QVERIFY(qss.open(QIODevice::ReadOnly));
    auto content = QString::fromUtf8(qss.readAll());
    QVERIFY(!content.contains(QRegularExpression("#[0-9a-fA-F]{3,8}\\b")));
    auto tokens = QRegularExpression("@([A-Za-z]+)@").globalMatch(content);
    while (tokens.hasNext())
    {
        auto key = tokens.next().captured(1);
        QVERIFY2(key == "theme" || light.contains(key), qPrintable(key));
    }
}
void InteractionSafetyTests::multipleRootsKeepFileOptionsAndStatusSeparate()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("shared.csv");
    QVERIFY(write(path, "x,a,b\n0,1,9\n1,2,8\n2,3,7\n"));
    WorkflowDocument doc(makeScientificRegistry());
    const auto a = doc.addRoot("file_source").nodeId;
    const auto b = doc.addRoot("file_source").nodeId;
    doc.setFiles(a, {path}); doc.setFiles(b, {path});
    auto options = doc.fileOptions(b, path); options["y_column"] = 2;
    QVERIFY(doc.setFileOptions(b, {path}, options));
    SourcePanel panel(&doc);
    auto *editor = panel.findChild<FileOptionsEditor *>();
    auto *files = panel.findChild<QListWidget *>("sourceFiles");
    QVERIFY(editor && files);
    panel.selectRoot(a); QTRY_VERIFY(editor->ready());
    panel.setFileState(a, path, SourceFileState::Failed);
    panel.setFileState(b, path, SourceFileState::Saved);
    QCOMPARE(editor->options()["y_column"].toInt(), 1);
    QCOMPARE(files->item(0)->data(Qt::UserRole + 2).toInt(), int(SourceFileState::Failed));
    panel.selectRoot(b); QTRY_VERIFY(editor->ready());
    QCOMPARE(editor->options()["y_column"].toInt(), 2);
    QCOMPARE(files->item(0)->data(Qt::UserRole + 2).toInt(), int(SourceFileState::Saved));
    panel.selectRoot(a); QTRY_VERIFY(editor->ready());
    QCOMPARE(editor->options()["y_column"].toInt(), 1);
    QCOMPARE(files->item(0)->data(Qt::UserRole + 2).toInt(), int(SourceFileState::Failed));
    QCOMPARE(doc.plan().requests.size(), 2);
}
void InteractionSafetyTests::bulkSourceRefreshKeepsSelectionsAndOptions()
{
    QTemporaryDir dir;
    const auto file = dir.filePath("first.csv");
    QVERIFY(write(file, "Time,Signal\n0,2\n1,3\n"));
    WorkflowDocument document(makeScientificRegistry());
    SourcePanel panel(&document);
    QStringList paths{file};
    for (int i = 1; i < 2000; ++i)
        paths.push_back(dir.filePath(QString("pending_%1.csv").arg(i)));
    panel.addFiles(paths);
    auto *list = panel.findChild<QListWidget *>("sourceFiles");
    auto *editor = panel.findChild<FileOptionsEditor *>();
    const auto root = panel.root();
    QCOMPARE(list->count(), paths.size());
    QTRY_VERIFY(editor->ready());
    list->selectAll();
    panel.refresh();
    QCOMPARE(list->selectedItems().size(), paths.size());
    QCOMPARE(list->currentRow(), 0);
    QItemSelection ranges;
    ranges.select(list->model()->index(10, 0), list->model()->index(29, 0));
    ranges.select(list->model()->index(100, 0), list->model()->index(109, 0));
    list->selectionModel()->select(ranges, QItemSelectionModel::ClearAndSelect);
    auto options = document.fileOptions(root, file);
    options["x_unit"] = "ka";
    QStringList targets = paths.mid(10, 20) + paths.mid(100, 10);
    QVERIFY(document.setFileOptions(root, targets, options));
    QCOMPARE(list->selectedItems().size(), 30); // refreshed by the document signal
    for (int i = 0; i < paths.size(); ++i)
    {
        const bool selected = (i >= 10 && i < 30) || (i >= 100 && i < 110);
        QCOMPARE(list->item(i)->isSelected(), selected);
        QCOMPARE(document.fileOptions(root, paths[i])["x_unit"].toString(),
                 selected ? QString("ka") : QString());
    }
    QVERIFY(editor->ready());
    QVERIFY(document.removeSubtree(root).ok());
    QCOMPARE(list->count(), 0);
    QVERIFY(document.undo().ok());
    QCOMPARE(list->count(), paths.size());
    QTRY_VERIFY(editor->ready());
}
void InteractionSafetyTests::whitespaceReadersRemainIndependent()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("spaces.txt");
    QByteArray contents("# comment\n\nX Y\n");
    for (int i = 0; i < 10000; ++i)
        contents += "  " + QByteArray::number(i) + "  \t " + QByteArray::number(i * 3) + " \r\n";
    contents += "invalid row\n";
    QVERIFY(write(path, contents));
    SeriesFileOptions options;
    options.delimiter = "whitespace";
    options.autoHeader = true;
    options.skipInvalidRows = true;
    const auto read = [&] { return readSeriesFile(path, options); };
    auto a = std::async(std::launch::async, read);
    auto b = std::async(std::launch::async, read);
    for (const auto &result : {a.get(), b.get()})
    {
        QVERIFY2(result.ok(), qPrintable(result.error));
        const auto *data = dataAs<SeriesData>(result.data);
        QCOMPARE(data->x.size(), 10000);
        QCOMPARE(data->x.last(), 9999.0);
        QCOMPARE(data->y.last(), 29997.0);
        QCOMPARE(result.report["skipped_rows"].toInt(), 1);
    }
    options.skipInvalidRows = false;
    QVERIFY(!read().ok());
    CancellationToken cancel;
    cancel.cancel();
    QVERIFY(!readSeriesFile(path, options, cancel).ok());
}
void InteractionSafetyTests::compactToolbarAndIndependentSidePanels()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("layout.sqlite");
    MainWindow window(settings, false);
    window.resize(1600, 900);
    window.show();
    QTest::qWait(50);
    auto *parameters = window.findChild<CollapsiblePanel *>("parameterSidePanel");
    auto *catalog = window.findChild<CollapsiblePanel *>("catalogSidePanel");
    QVERIFY(parameters && catalog);
    catalog->setCollapsed(false);
    QVERIFY(!parameters->isCollapsed() && !catalog->isCollapsed());
    QVERIFY(parameters->mapTo(&window, QPoint()).x() < catalog->mapTo(&window, QPoint()).x());
    auto *grip = parameters->findChild<QAbstractButton *>("sidePanelGrip");
    QTest::mouseClick(grip, Qt::LeftButton);
    QVERIFY(parameters->isCollapsed());
    QVERIFY(!catalog->isCollapsed());
    QTRY_VERIFY(parameters->width() <= 30);
    QTest::mouseClick(grip, Qt::LeftButton);
    QVERIFY(!parameters->isCollapsed());
    QTRY_VERIFY(parameters->width() >= 220);
    const int oldWidth = parameters->width();
    const auto local = QPointF(grip->rect().center());
    const auto global = QPointF(grip->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(grip, &press);
    QMouseEvent move(QEvent::MouseMove, local - QPointF(40, 0), global - QPointF(40, 0),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(grip, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global - QPointF(40, 0),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(grip, &release);
    QVERIFY(parameters->width() > oldWidth);
    QVERIFY(!parameters->isCollapsed());
    catalog->setCollapsed(true);
    QVERIFY(!parameters->isCollapsed());
    QVERIFY(!window.findChild<QPushButton *>("loadExample"));
    auto *run = window.findChild<QPushButton *>("Primary");
    QVERIFY(run && !run->icon().isNull() && run->height() >= 42);
    auto *navigation = window.findChildren<QPushButton *>("Nav").front();
    QCOMPARE(run->parentWidget(), navigation->parentWidget());
    auto *sources = window.findChild<SourcePanel *>();
    auto *roots = sources->findChild<QComboBox *>("sourceRoot");
    QVERIFY(window.document()->addRoot("file_source").ok());
    QVERIFY(roots->isHidden());
    QVERIFY(window.document()->addRoot("file_source").ok());
    QVERIFY(!roots->isHidden());
    QVERIFY(sources->findChild<QListWidget *>("sourceFiles")->height() >= 150);
}
void InteractionSafetyTests::detrendOutputMenuKeepsExplicitPortBinding()
{
    QTemporaryDir dir;
    AppSettings settings;
    settings.databasePath = dir.filePath("ports.sqlite");
    MainWindow window(settings, false);
    window.show();
    const auto root = window.document()->addRoot("file_source").nodeId;
    const auto trend = window.document()->addChild(root, ChildSlot::Down, "detrend_lowess").nodeId;
    QTest::qWait(50);
    window.canvas()->methodDropped("mtm", trend, ChildSlot::Down);
    QPointer<OutputPicker> picker = window.findChild<OutputPicker *>();
    QVERIFY(picker);
    QTRY_VERIFY(picker && picker->isVisible());
    auto *list = picker->findChild<QListWidget *>("outputChoices");
    QVERIFY(list);
    QListWidgetItem *detrended = nullptr;
    bool fitted = false;
    for (int row = 0; row < list->count(); ++row)
    {
        auto *item = list->item(row);
        QVERIFY(!item->text().contains("usual choice"));
        if (item->data(Qt::UserRole).toString() == "detrended") detrended = item;
        if (item->text().contains("fitted trend curve")) fitted = true;
    }
    QVERIFY(detrended && detrended->font().bold() && fitted);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(detrended).center());
    const auto &node = window.document()->model().definition().nodes.last();
    QCOMPARE(node.method, QString("mtm"));
    QCOMPARE(node.input->output, QString("detrended"));
}
void InteractionSafetyTests::invalidFileIsReportedAndCorrectColumnsRecover()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("depth_age_signal.csv");
    QByteArray bytes("depth,age,signal\r\n0,,0.5\r\n");
    for (int i = 1; i < 64; ++i)
        bytes += QByteArray::number(i) + ',' + QByteArray::number(100 + i) + ',' +
                 QByteArray::number(std::sin(i * .4)) + "\r\n";
    QVERIFY(write(path, bytes));
    SeriesFileOptions options;
    options.autoHeader = true;
    const auto probe = probeSeriesFile(path, options);
    QCOMPARE(probe.preview.size(), 5);
    QCOMPARE(probe.preview[0].line, 2);
    QCOMPARE(probe.preview[0].fields[1], QString());
    AppSettings settings;
    settings.databasePath = dir.filePath("failures.sqlite");
    MainWindow window(settings, false);
    window.show();
    auto *document = window.document();
    const auto root = document->addRoot("file_source").nodeId;
    const auto lowess = document->addChild(root, ChildSlot::Down, "detrend_lowess").nodeId;
    const auto mtm = document->addChild(lowess, ChildSlot::Down, "mtm").nodeId;
    document->setFiles(root, {path});
    auto *sources = window.findChild<SourcePanel *>();
    QTRY_VERIFY(sources->readyToRun());
    auto *preview = sources->findChild<QTableWidget *>("fileRowPreview");
    QCOMPARE(preview->item(0, 2)->text(), text("ui.preview_empty"));
    QCOMPARE(preview->item(0, 2)->foreground().color(), themeColor("error"));
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && window.results()->recordId() > 0, 10000);
    const auto failedId = window.results()->recordId();
    ResultRepository repo;
    QVERIFY(repo.open(settings.databasePath).isEmpty());
    const auto failed = repo.load(failedId);
    QVERIFY(failed.ok());
    QCOMPARE(failed.run.nodes[root].state, NodeState::Failed);
    QCOMPARE(failed.run.nodes[lowess].state, NodeState::Blocked);
    QCOMPARE(failed.run.nodes[mtm].state, NodeState::Blocked);
    QCOMPARE(failed.run.nodes[root].report["input_error"].toObject()["line"].toInt(), 2);
    window.results()->selectStep(2, true);
    auto *notice = window.findChild<QLabel *>("StepError");
    QVERIFY(notice && notice->isVisible());
    QVERIFY(notice->text().contains("First failed step"));
    QVERIFY(notice->text().contains("line 2"));
    auto fixed = document->fileOptions(root, path);
    fixed["y_column"] = 2;
    QVERIFY(document->setFileOptions(root, {path}, fixed));
    QTRY_VERIFY(sources->readyToRun());
    QCOMPARE(preview->item(0, 2)->text(), QString("0.5"));
    window.startRun();
    // The asynchronous result loader clears the ID while the new record is loading.
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && window.results()->recordId() > 0 &&
                            window.results()->recordId() != failedId, 10000);
    const auto passed = repo.load(window.results()->recordId());
    QVERIFY(passed.ok());
    QCOMPARE(passed.run.state, RunState::Succeeded);
    QCOMPARE(repo.load(failedId).run.nodes[root].state, NodeState::Failed);
}
void InteractionSafetyTests::failedStepCannotExportOrReuseAnOlderPlot()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("descending.csv");
    QByteArray bytes("Age,Value\n");
    for (int i = 63; i >= 0; --i)
        bytes += QByteArray::number(i) + ',' + QByteArray::number(std::sin(i * .4)) + '\n';
    QVERIFY(write(path, bytes));
    AppSettings settings;
    settings.databasePath = dir.filePath("order.sqlite");
    MainWindow window(settings, false);
    window.show();
    auto *document = window.document();
    const auto root = document->addRoot("file_source").nodeId;
    const auto lowess = document->addChild(root, ChildSlot::Down, "detrend_lowess").nodeId;
    document->addChild(lowess, ChildSlot::Down, "mtm");
    document->setFiles(root, {path});
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && window.results()->recordId() > 0, 10000);
    auto *results = window.results();
    const auto failedId = results->recordId();
    results->selectStep(0, true);
    QTRY_VERIFY(results->plotView()->hasPlot());
    auto *csv = window.findChild<QPushButton *>("exportCsv");
    auto *reuse = window.findChild<QPushButton *>("reuseOutput");
    QVERIFY(csv->isEnabled() && reuse->isEnabled());
    results->selectStep(1, true);
    QVERIFY(results->plotView()->hasPlot()); // Retained, explicitly labelled source plot.
    QVERIFY(!csv->isEnabled() && !reuse->isEnabled());
    QVERIFY(window.findChild<QPushButton *>("exportPng")->isEnabled());
    QVERIFY(!results->startCsvExport(dir.filePath("wrong_output.csv")));
    QVERIFY(!QFile::exists(dir.filePath("wrong_output.csv")));
    QVERIFY(window.findChild<QLabel *>("StepError")->text().contains("descending values"));
    results->selectStep(0, true);
    QSignalSpy finished(results->exporter(), &ExportController::finished);
    QVERIFY(results->startCsvExport(dir.filePath("source.csv")));
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished[0][1].toString().isEmpty());
    WorkflowModel repaired(document->model().registry());
    const auto input = repaired.addRoot("file_source", {0, 0}).nodeId;
    const auto prep = repaired.addChild(input, ChildSlot::Down, "preprocess").nodeId;
    const auto detrend = repaired.addChild(prep, ChildSlot::Down, "detrend_lowess").nodeId;
    repaired.addChild(detrend, ChildSlot::Down, "mtm");
    QVERIFY(document->replaceDefinition(repaired.definition()).ok());
    document->setFiles(input, {path});
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && results->recordId() > 0 &&
                            results->recordId() != failedId, 10000);
    ResultRepository repo;
    QVERIFY(repo.open(settings.databasePath).isEmpty());
    const auto result = repo.load(results->recordId());
    QVERIFY(result.ok());
    QCOMPARE(result.run.state, RunState::Succeeded);
    QCOMPARE(repo.load(failedId).run.nodes[lowess].state, NodeState::Failed);
}
void InteractionSafetyTests::diagnosticSerializationAndTranslations()
{
    const auto diagnostic = makeError(ErrorCode::XDuplicate,
        {{"point_before", 4}, {"point", 5}, {"x_before", 1720.0}, {"x", 1720.0}}, "library detail");
    QCOMPARE(ErrorInfo::fromJson(diagnostic.toJson()).toJson(), diagnostic.toJson());
    const ErrorInfo extension{"external.test.CUSTOM", {{"value", "<b>{point}</b>"}}, "extension failure"};
    QCOMPARE(ErrorInfo::fromJson(extension.toJson()).code, extension.code);
    QCOMPARE(presentError(extension).summary, QString("extension failure"));
    QVERIFY(presentError(extension).details.contains("<b>{point}</b>")); // Text, never markup.
    auto invalid = diagnostic.toJson(); invalid["version"] = 999;
    QVERIFY(ErrorInfo::fromJson(invalid).isEmpty());
    const auto bounded = makeError(ErrorCode::Unknown, {{"text", QString(2000, 'x')}, {"payload", QJsonArray{1, 2}}}, QString(5000, 'z'));
    QCOMPARE(bounded.context["text"].toString().size(), 1024);
    QVERIFY(!bounded.context.contains("payload"));
    QCOMPARE(bounded.technical.size(), 4096);
    for (const auto &language : {QString("zh"), QString("en")})
    {
        setLanguage(language);
        for (const auto &code : builtinErrorCodes())
        {
            const auto key = "diagnostic." + code + ".summary";
            QVERIFY2(translatedLabel(key, key) != key, qPrintable(language + ": " + code));
        }
        const auto display = presentError(diagnostic);
        QVERIFY(display.details.contains("1720"));
        QVERIFY(display.details.contains("INPUT_X_DUPLICATE"));
        QVERIFY(!display.suggestion.isEmpty());
        QVERIFY(!display.details.contains("diagnostic.field."));
    }
    setLanguage("en");
    NodeResult old;
    old.error = "X must be strictly increasing; run preprocessing first";
    QVERIFY(presentError(old).summary.contains("descending"));
    // New structured metadata determines presentation even if the legacy message changes.
    auto result = NodeResult::failure(diagnostic);
    result.error = "completely different fallback";
    QVERIFY(presentError(result).summary.contains("duplicate"));
}
void InteractionSafetyTests::diagnosticSignalStorageAndDetails()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("duplicate.csv"), database = dir.filePath("diagnostic.sqlite");
    QByteArray bytes("X,Y\n");
    for (int i = 0; i < 64; ++i)
        bytes += QByteArray::number(i == 12 ? 11 : i) + ',' + QByteArray::number(std::sin(i * .2)) + '\n';
    QVERIFY(write(path, bytes));
    AppSettings settings; settings.databasePath = database;
    MainWindow window(settings, false); window.show();
    auto *document = window.document();
    const auto root = document->addRoot("file_source").nodeId;
    const auto lowess = document->addChild(root, ChildSlot::Down, "detrend_lowess").nodeId;
    const auto mtm = document->addChild(lowess, ChildSlot::Down, "mtm").nodeId;
    document->setFiles(root, {path});
    QTRY_VERIFY(window.findChild<SourcePanel *>()->readyToRun());
    QSignalSpy events(window.runner(), &RunController::eventReady);
    window.startRun();
    QTRY_VERIFY_WITH_TIMEOUT(!window.runner()->busy() && window.results()->recordId() > 0, 10000);
    ErrorInfo emitted;
    for (const auto &arguments : events)
    {
        const auto event = qvariant_cast<RunEvent>(arguments[0]);
        if (event.nodeId == lowess && event.state == NodeState::Failed) emitted = event.diagnostic;
    }
    QCOMPARE(emitted.code, QString("INPUT_X_DUPLICATE"));
    QCOMPARE(emitted.context["point_before"].toInt(), 12);
    QCOMPARE(emitted.context["point"].toInt(), 13);
    ResultRepository repo; QVERIFY(repo.open(database).isEmpty());
    const auto saved = repo.load(window.results()->recordId());
    QVERIFY(saved.ok());
    QCOMPARE(saved.run.nodes[lowess].diagnostic().toJson(), emitted.toJson());
    QCOMPARE(saved.run.nodes[mtm].state, NodeState::Blocked);
    QCOMPARE(saved.run.nodes[mtm].diagnostic().context["cause_node_id"].toString(), lowess.toString(QUuid::WithoutBraces));
    QVERIFY(!saved.run.executionOrder.contains(mtm));
    window.results()->selectStep(2, true);
    auto *button = window.findChild<QPushButton *>("viewErrorDetails");
    QVERIFY(button && button->isVisible());
    bool dialogOpened = false;
    QTimer::singleShot(0, &window, [&]
    {
        auto *dialog = window.findChild<QDialog *>("errorDetailsDialog");
        if (!dialog) return;
        dialogOpened = true;
        auto *copy = dialog->findChild<QPushButton *>("copyErrorDetails");
        if (copy) copy->click();
        dialog->accept();
    });
    button->click();
    QVERIFY(dialogOpened);
    const auto copied = QApplication::clipboard()->text();
    QVERIFY(copied.contains("UPSTREAM_UNAVAILABLE"));
    QVERIFY(copied.contains("INPUT_X_DUPLICATE"));
    QVERIFY(copied.contains("13"));
    QVERIFY(copied.contains(path.section('/', -1)));
    window.results()->selectStep(0, true);
    QVERIFY(!button->isVisible());
    // File diagnostics preserve the real physical line separately from algorithm point indexes.
    const auto badFile = dir.filePath("invalid.csv");
    QVERIFY(write(badFile, "# comment\nX,Y\n1,2\n2,no_number\n"));
    const auto read = readSeriesFile(badFile, {});
    const auto fileDiagnostic = ErrorInfo::fromJson(read.report["diagnostic"].toObject());
    QCOMPARE(fileDiagnostic.code, QString("FILE_NUMERIC_INVALID"));
    QCOMPARE(fileDiagnostic.context["line"].toInt(), 4);
    QCOMPARE(fileDiagnostic.context["y_column"].toInt(), 2);
    QCOMPARE(fileDiagnostic.context["y_value"].toString(), QString("no_number"));
}
void InteractionSafetyTests::filePreviewMatchesWorkflow_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QJsonObject>("options");
    QTest::addColumn<int>("rows");
    QTest::addColumn<double>("firstX");
    QTest::addColumn<int>("skipped");
    const QByteArray numbers("0,10\n1,20\n2,30\n");
    const QByteArray headers("time,value\n" + numbers);
    auto opts = fileOptionsToJson(SeriesFileOptions{});
    opts["x_unit"] = "ka"; opts["y_unit"] = "u";
    opts["header"] = false;
    QTest::newRow("numeric-no-header") << numbers << opts << 3 << 0. << 0;
    QTest::newRow("text-header-as-data-fails") << headers << opts << 0 << 0. << 0;
    opts["skip_invalid_rows"] = true;
    QTest::newRow("text-header-as-data-skip") << headers << opts << 3 << 0. << 1;
    opts["skip_invalid_rows"] = false; opts["header"] = true;
    QTest::newRow("numeric-first-row-consumed-as-header") << numbers << opts << 2 << 1. << 0;
    QTest::newRow("text-header") << headers << opts << 3 << 0. << 0;
    opts["auto_header"] = true;
    QTest::newRow("auto-numeric") << numbers << opts << 3 << 0. << 0;
    QTest::newRow("auto-text") << headers << opts << 3 << 0. << 0;
    opts["y_column"] = 2;
    QTest::newRow("unused-text-column") << QByteArray("0,a,10\n1,b,20\n2,c,30\n") << opts << 3 << 0. << 0;
    opts["auto_header"] = false; opts["header"] = false; opts["y_column"] = 1;
    opts["delimiter"] = "tab";
    QTest::newRow("tabs") << QByteArray("0\t10\n1\t20\n2\t30\n") << opts << 3 << 0. << 0;
    opts["delimiter"] = "whitespace";
    QTest::newRow("whitespace") << QByteArray("0 10\n1 20\n2 30\n") << opts << 3 << 0. << 0;
    opts["delimiter"] = "comma";
    QTest::newRow("wrong-delimiter") << QByteArray("0 10\n1 20\n2 30\n") << opts << 0 << 0. << 0;
    QTest::newRow("too-few-points") << QByteArray("0,10\n") << opts << 0 << 0. << 0;
}
void InteractionSafetyTests::filePreviewMatchesWorkflow()
{
    QFETCH(QByteArray, bytes); QFETCH(QJsonObject, options);
    QFETCH(int, rows); QFETCH(double, firstX); QFETCH(int, skipped);
    QTemporaryDir dir;
    const auto file = dir.filePath("input.csv");
    QVERIFY(write(file, bytes));
    FilePreviewLoader preview;
    preview.setInput(file, options, true);
    preview.setActive(true);
    QTRY_VERIFY(preview.state() == FilePreviewLoader::State::Ready ||
                preview.state() == FilePreviewLoader::State::Failed);
    const auto result = preview.result();
    QVERIFY(result);
    QCOMPARE(result->rows, rows);
    WorkflowDocument document(makeScientificRegistry());
    const auto root = document.addRoot("file_source").nodeId;
    document.setFiles(root, {file});
    QVERIFY(document.setFileOptions(root, {file}, options));
    const auto plan = document.plan();
    QVERIFY(plan.validation.ok());
    const auto run = WorkflowExecutor().run(plan.requests.front());
    const auto formal = run.nodes[root];
    if (rows == 0)
    {
        QCOMPARE(formal.state, NodeState::Failed);
        QVERIFY(!result->failure.diagnostic().isEmpty());
        QCOMPARE(result->failure.diagnostic().code, formal.diagnostic().code);
        QCOMPARE(result->failure.error, formal.error);
    }
    else
    {
        QCOMPARE(formal.state, NodeState::Succeeded);
        const auto *original = dataAs<SeriesData>(formal.outputs["series"]);
        QVERIFY(original);
        QCOMPARE(result->samples.x.front(), firstX);
        QCOMPARE(result->samples.x, original->x);
        QCOMPARE(result->samples.y, original->y);
        QCOMPARE(result->samples.xAxis.name, original->xAxis.name);
        QCOMPARE(result->samples.yAxis.name, original->yAxis.name);
        QCOMPARE(result->samples.xAxis.unit, original->xAxis.unit);
        QCOMPARE(result->samples.yAxis.unit, original->yAxis.unit);
        QCOMPARE(result->skippedRows, skipped);
    }
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1); // No database/cache file created.
}
void InteractionSafetyTests::filePreviewFollowsAllEditorOptions()
{
    QTemporaryDir dir;
    const auto a = dir.filePath("a.csv"), b = dir.filePath("b.tsv");
    QVERIFY(write(a, "time,value,other\n0,10,100\n1,20,200\n2,30,300\n"));
    QVERIFY(write(b, "5\t50\n6\t60\n7\t70\n"));
    WorkflowDocument document(makeScientificRegistry());
    SourcePanel sources(&document);
    FilePreviewLoader preview;
    connect(&sources, &SourcePanel::previewInputChanged, &preview, &FilePreviewLoader::setInput);
    preview.setActive(true);
    sources.addFiles({a, b});
    auto *editor = sources.findChild<FileOptionsEditor *>();
    auto *header = editor->findChild<QComboBox *>("fileHeader");
    auto *delimiter = editor->findChild<QComboBox *>("fileDelimiter");
    auto *x = editor->findChild<QComboBox *>("fileXColumn");
    auto *y = editor->findChild<QComboBox *>("fileYColumn");
    auto *skip = editor->findChild<QCheckBox *>();
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    const auto persisted = document.fileOptions(sources.root(), a);
    QSignalSpy reads(&preview, &FilePreviewLoader::readStarted);
    editor->findChild<QLineEdit *>("fileXUnit")->setText("ka");
    editor->findChild<QLineEdit *>("fileYUnit")->setText("ppm");
    QCOMPARE(preview.result()->samples.xAxis.unit, QString("ka"));
    QCOMPARE(preview.result()->samples.yAxis.unit, QString("ppm"));
    QCOMPARE(reads.size(), 0); // Label-only edits reuse the current samples.
    y->setCurrentIndex(y->findData(2));
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->samples.y, QVector<double>({100, 200, 300}));
    x->setCurrentIndex(x->findData(1));
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->samples.x, QVector<double>({10, 20, 30}));
    QCOMPARE(document.fileOptions(sources.root(), a), persisted); // Preview never applies drafts.
    const auto activate = [](QComboBox *box, const QVariant &value)
    {
        box->setCurrentIndex(box->findData(value));
        return QMetaObject::invokeMethod(box, "activated", Q_ARG(int, box->currentIndex()));
    };
    QVERIFY(activate(header, 0)); // Text header is now data: fail, never leave an old curve visible.
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Failed);
    QVERIFY(preview.result()->samples.x.isEmpty());
    skip->setChecked(true);
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->skippedRows, 1);
    QVERIFY(activate(header, 1));
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->skippedRows, 0);
    sources.findChild<QListWidget *>("sourceFiles")->setCurrentRow(1);
    QTRY_VERIFY(editor->ready());
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.path(), b);
    QCOMPARE(preview.result()->rows, 3);
    QVERIFY(activate(header, 1)); // Numeric first row becomes labels, just as in the workflow.
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->rows, 2);
    QCOMPARE(preview.result()->samples.xAxis.name, QString("5"));
    QCOMPARE(preview.result()->samples.x.front(), 6.);
    QVERIFY(activate(delimiter, QString("comma")));
    QTRY_VERIFY(!editor->ready());
    QVERIFY(!preview.result()); // Invalid column probe clears the previous plot.
    QVERIFY(activate(delimiter, QString("tab")));
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->rows, 2);
    document.setFiles(sources.root(), {});
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Empty);
    QVERIFY(!preview.result());
}
void InteractionSafetyTests::filePreviewIsLazyAndDiscardsStaleReads()
{
    QTemporaryDir dir;
    const auto a = dir.filePath("large.csv"), b = dir.filePath("latest.csv");
    QByteArray bytes;
    for (int i = 0; i < 150000; ++i) bytes += QByteArray::number(i) + ",1\n";
    QVERIFY(write(a, bytes)); QVERIFY(write(b, "900,9\n901,8\n"));
    auto options = fileOptionsToJson(SeriesFileOptions{}); options["header"] = false;
    FilePreviewLoader preview;
    QSignalSpy reads(&preview, &FilePreviewLoader::readStarted);
    preview.setInput(a, options, true);
    QTest::qWait(180);
    QCOMPARE(reads.size(), 0);
    preview.setActive(true);
    QTRY_COMPARE(reads.size(), 1);
    preview.setInput(b, options, true);
    QVERIFY(!preview.result());
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->samples.x.front(), 900.);
    preview.setInput(a, options, true);
    preview.setActive(false);
    const auto count = reads.size();
    QTest::qWait(200);
    QCOMPARE(reads.size(), count);
    QCOMPARE(preview.state(), FilePreviewLoader::State::Inactive);
    QVERIFY(!preview.result());
    preview.setInput(b, options, true);
    preview.setActive(true);
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    QCOMPARE(preview.result()->samples.x.front(), 900.);
}
void InteractionSafetyTests::filePreviewKeepsExtremaWithinDisplayBudget()
{
    QTemporaryDir dir;
    const auto file = dir.filePath("large.csv");
    QByteArray bytes;
    for (int i = 0; i < 50000; ++i)
        bytes += QByteArray::number(i == 231 ? -100. : i) + ',' +
                 QByteArray::number(i == 433 ? 10000. : i == 1081 ? -9000. : 1.) + '\n';
    QVERIFY(write(file, bytes));
    auto options = fileOptionsToJson(SeriesFileOptions{}); options["header"] = false;
    FilePreviewLoader preview; preview.setInput(file, options, true); preview.setActive(true);
    QTRY_COMPARE(preview.state(), FilePreviewLoader::State::Ready);
    const auto data = preview.result();
    QCOMPARE(data->rows, 50000);
    QVERIFY(data->samples.x.size() <= FilePreviewLoader::PointBudget);
    QVERIFY(data->samples.x.size() < data->rows);
    QCOMPARE(data->samples.x.front(), 0.); QCOMPARE(data->samples.x.back(), 49999.);
    QVERIFY(data->samples.x.contains(-100.));
    QVERIFY(data->samples.y.contains(10000.)); QVERIFY(data->samples.y.contains(-9000.));
    const auto negative = data->samples.x.indexOf(-100.);
    QVERIFY(negative > 0 && data->samples.x[negative - 1] > 0.); // Row order, not sorting by X.
}
void InteractionSafetyTests::filePreviewVisibleOnFirstShow()
{
    QTemporaryDir dir; AppSettings settings; settings.databasePath = dir.filePath("startup.sqlite");
    MainWindow window(settings, false); window.resize(1500, 900); window.show();
    QTest::qWait(60);
    auto *column = window.findChild<CollapsiblePanel *>("previewSidePanel");
    auto *panel = window.findChild<FilePreviewPanel *>();
    auto *loader = panel->findChild<FilePreviewLoader *>();
    auto *preview = window.findChild<WorkflowPreviewPanel *>();
    QVERIFY(column && panel && preview); QVERIFY(!column->isCollapsed());
    QVERIFY(panel->isVisible()); QVERIFY(panel->width() > 250);
    auto *grip = column->findChild<QAbstractButton *>("sidePanelGrip");
    QVERIFY(grip && grip->isVisible());
    QCOMPARE(loader->state(), FilePreviewLoader::State::Empty);
    QCOMPARE(preview->controller()->state(), WorkflowPreviewController::State::Idle);
    QSignalSpy reads(loader, &FilePreviewLoader::readStarted);
    QTest::qWait(300); QCOMPARE(reads.count(), 0);
    auto *canvas = window.canvas();
    QVERIFY(!canvas->rect().intersects(QRect(canvas->mapFromGlobal(panel->mapToGlobal(QPoint())), panel->size())));
    window.setPage(1); QCOMPARE(loader->state(), FilePreviewLoader::State::Inactive);
    window.setPage(0); QCOMPARE(loader->state(), FilePreviewLoader::State::Empty);
    QCOMPARE(reads.count(), 0);
}
void InteractionSafetyTests::filePreviewClearsInvalidInputs()
{
    QTemporaryDir dir;
    const auto valid = dir.filePath("valid.csv"), invalid = dir.filePath("invalid.csv"), empty = dir.filePath("empty.csv");
    QVERIFY(write(valid, "time,value\n0,10\n1,20\n2,30\n"));
    QVERIFY(write(invalid, "0,10\n1,nan\n2,30\n"));
    QVERIFY(write(empty, {}));
    AppSettings settings; settings.databasePath = dir.filePath("empty.sqlite");
    MainWindow window(settings, false); window.resize(1500, 900); window.show();
    auto *panel = window.findChild<FilePreviewPanel *>();
    auto *loader = panel->findChild<FilePreviewLoader *>();
    auto *plot = panel->findChild<QCustomPlot *>();
    auto *curve = qobject_cast<QCPCurve *>(plot->plottable(0));
    auto *fit = panel->findChild<QToolButton *>("filePreviewFit");
    auto *message = panel->findChild<QLabel *>("filePlotMessage");
    auto options = fileOptionsToJson(SeriesFileOptions{});
    options["header"] = true; options["auto_header"] = false;
    panel->setInput(valid, options, true);
    QTRY_COMPARE(loader->state(), FilePreviewLoader::State::Ready);
    QVERIFY(plot->isVisible()); QVERIFY(fit->isEnabled()); QCOMPARE(curve->data()->size(), 3);
    options["header"] = false;
    for (const auto &path : {valid, invalid, empty, dir.filePath("missing.csv")})
    {
        panel->setInput(path, options, true);
        QVERIFY(curve->data()->isEmpty()); QVERIFY(!fit->isEnabled()); QVERIFY(!plot->isVisible());
        QTRY_COMPARE(loader->state(), FilePreviewLoader::State::Failed);
        QVERIFY(message->isVisible()); QVERIFY(!message->text().isEmpty());
        QVERIFY(curve->data()->isEmpty());
    }
    panel->setInput(valid, options, false);
    QCOMPARE(loader->state(), FilePreviewLoader::State::Waiting);
    QVERIFY(!plot->isVisible()); QVERIFY(!fit->isEnabled());
    options["skip_invalid_rows"] = true;
    panel->setInput(valid, options, true);
    QTRY_COMPARE(loader->state(), FilePreviewLoader::State::Ready);
    QCOMPARE(curve->data()->size(), 3); QCOMPARE(loader->result()->skippedRows, qsizetype(1));
    panel->setInput({}, {}, false);
    QCOMPARE(loader->state(), FilePreviewLoader::State::Empty);
    QVERIFY(!loader->result()); QVERIFY(curve->data()->isEmpty()); QVERIFY(!fit->isEnabled());
    QCOMPARE(plot->xAxis->label(), QString()); QCOMPARE(plot->yAxis->label(), QString());
}
void InteractionSafetyTests::filePreviewPanelResizesAndReleasesHiddenData()
{
    QTemporaryDir dir; const auto file = dir.filePath("input.csv");
    QVERIFY(write(file, "time,value\n2,1\n0,2\n1,3\n"));
    AppSettings settings; settings.databasePath = dir.filePath("unused.sqlite");
    MainWindow window(settings, false); window.resize(1500, 900); window.show();
    auto *panel = window.findChild<FilePreviewPanel *>();
    auto *column = window.findChild<CollapsiblePanel *>("previewSidePanel");
    auto *loader = panel->findChild<FilePreviewLoader *>();
    auto *plot = panel->findChild<QCustomPlot *>();
    window.findChild<SourcePanel *>()->addFiles({file});
    QTRY_COMPARE(loader->state(), FilePreviewLoader::State::Ready);
    QCOMPARE(plot->xAxis->label(), QString("time"));
    auto *curve = qobject_cast<QCPCurve *>(plot->plottable(0));
    QVERIFY(curve); QCOMPARE(curve->data()->constBegin()->key, 2.);
    auto *split = window.findChild<QSplitter *>("taskSplitter");
    auto sizes = split->sizes(); sizes[1] = 2000; split->setSizes(sizes);
    QVERIFY(column->width() <= 720 && column->width() >= 300);
    ThemeManager::instance().apply(*qApp, "dark");
    QCOMPARE(curve->pen().color(), themeColor("curve"));
    ThemeManager::instance().apply(*qApp, "light");
    column->setCollapsed(true); QVERIFY(!loader->result()); QVERIFY(curve->data()->isEmpty());
    QVERIFY(column->width() <= 28);
    column->setCollapsed(false); QTRY_COMPARE(loader->state(), FilePreviewLoader::State::Ready);
    window.setPage(1); QCOMPARE(loader->state(), FilePreviewLoader::State::Inactive);
    window.setPage(0); QTRY_COMPARE(loader->state(), FilePreviewLoader::State::Ready);
    ResultRepository repository; QVERIFY(repository.open(settings.databasePath).isEmpty());
    const auto records = repository.list(); QVERIFY(records.error.isEmpty()); QVERIFY(records.records.isEmpty());
}

void InteractionSafetyTests::ar1GlobalResultPresentation()
{
    const auto registry=makeScientificRegistry();
    WorkflowModel model(registry);
    const auto root=model.addRoot("input_series",{0,0}).nodeId;
    auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"preview",series(),{}}});
    const auto child=model.addChild(root,ChildSlot::Down,"ar1_global").nodeId;
    run.workflowSnapshot=workflowToJson(model.definition());
    run.executionOrder.push_back(child);
    run.nodes[child]=NodeResult::success({{"ar1_global_test",makeData(TableData{{"global_p"},{{.03}}})}},
        {{"global_p",.03},{"global_test_available",true},{"reject_global_null",true}});
    run.nodes[child].warnings<<"warning.ar1_global_scope";
    QTemporaryDir dir;
    ResultRepository repository;QVERIFY(repository.open(dir.filePath("global.sqlite")).isEmpty());
    const auto saved=repository.save(run);QVERIFY(saved.saved());
    for(const auto &language:{QString("en"),QString("zh")})
    {
        setLanguage(language);
        ResultsPage page(registry);page.resize(1300,800);page.show();
        QSignalSpy loaded(&page,&ResultsPage::recordLoaded);
        page.setDatabase(dir.filePath("global.sqlite"));page.openRecord(saved.recordId);
        QTRY_VERIFY(!loaded.isEmpty());page.selectStep(1,false);
        auto *notice=page.findChild<QLabel *>("StepError");QVERIFY(notice && notice->isVisible());
        QVERIFY(notice->property("globalTestSummary").toBool());
        QVERIFY(notice->text().contains("0.03"));QVERIFY(notice->text().contains(text("ui.ar1_reject")));
        auto *details=page.findChild<QPushButton *>("viewErrorDetails");QVERIFY(details && !details->isVisible());
        page.selectStep(0,false);QVERIFY(!notice->property("globalTestSummary").toBool());
        ParameterEditor editor;const auto definition=model.definition();
        const auto node=std::find_if(definition.nodes.begin(),definition.nodes.end(),[&](const NodeDefinition& n){return n.id==child;});
        QVERIFY(node!=definition.nodes.end());editor.showNode(&*node,registry);
        auto *help=editor.findChild<QToolButton *>("methodHelp");
        QVERIFY(help && !help->isHidden());
        QCOMPARE(help->accessibleDescription(),text("methodhelp.ar1_global"));
        QVERIFY(help->toolTip().isEmpty());
        QVERIFY(help->accessibleDescription().contains("64–1024"));
        help->click();
        auto *dialog=editor.findChild<HelpDialog *>("helpDialog");
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->currentTopic().anchor,QString("m-ar1_global"));
        auto *browser=dialog->findChild<QTextBrowser *>("helpBrowser");
        QVERIFY(browser && browser->toPlainText().contains("64–1024"));
        dialog->close();
    }
    setLanguage("en");
}

int runInteractionSafetyTests(int argc, char **argv)
{
    InteractionSafetyTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "interaction_safety_tests.moc"
