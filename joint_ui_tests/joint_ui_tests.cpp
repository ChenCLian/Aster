// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QScopeGuard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <QElapsedTimer>
#include "io/examples/example_catalog.h"
#include <cmath>
#include "processing/scientific_registry.h"
#include "processing/cross_spectrum_task.h"
#include "application/joint_batch_runner.h"
#include "ui/workflow/workflow_document.h"
#include "ui/services/workflow_preview_controller.h"
#include "ui/services/plot_data_loader.h"
#include "ui/services/record_directory_service.h"
#include "ui/services/export_controller.h"
#include "ui/panels/pairing_panel.h"
#include "../tests/support/joint_input_helpers.h"
#include "ui/app/main_window.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/panels/source_panel.h"
#include "ui/panels/workflow_preview_panel.h"
#include "ui/results/plot_view.h"
#include "ui/results/results_page.h"
#include "ui/exports/figure_export.h"
#include "ui/common/presentation.h"
#include "third_party/qcustomplot/qcustomplot.h"
#include "io/database/data_codec.h"
#include "model/workflow_codec.h"
using namespace aster;
using namespace aster::ui;
namespace
{
DataRef signal(double phase = 0, double shift = 0)
{
    SeriesData s;
    s.xAxis = {"Time", "ka"};
    s.yAxis = {"Signal", "u"};
    for (int i = 0; i < 512; ++i)
    {
        s.x << i * .25 + shift;
        s.y << std::sin(i * .13 + phase) + .2 * std::cos(i * .7 + phase);
    }
    return makeData(s);
}
class DualSource : public Task
{
    NodeResult execute(const TaskInput &, const ExecutionContext &ctx) override
    {
        // A delayed source lets tests prove superseded callbacks are discarded.
        if (ctx.sourceMetadata["slow"].toBool())
            QThread::msleep(80);
        auto b = *dataAs<SeriesData>(ctx.boundSource);
        for (auto &v : b.y)
            v *= 3;
        b.yAxis = {"Alternative", "v"};
        return NodeResult::success({{"default", ctx.boundSource}, {"alternative", makeData(b)}});
    }
};
class PassTable : public Task
{
    NodeResult execute(const TaskInput &in, const ExecutionContext &) override
    {
        return NodeResult::success({{"table", in.data}});
    }
};
TaskRegistry registry()
{
    auto r = makeScientificRegistry();
    TaskDescriptor d;
    d.key = "dual_test";
    d.category = {"test"};
    d.label = "Dual test";
    d.requiresSourceBinding = true;
    d.boundDataKind = DataKind::Series;
    d.outputs = {{"default", "Default", DataKind::Series, true},
                 {"alternative", "Alternative", DataKind::Series, true}};
    d.defaultOutput = "default";
    if (!r.add(d, [](const QJsonObject &) { return std::make_unique<DualSource>(); }).ok())
        qFatal("registry");
    TaskDescriptor pass;
    pass.key = "pass_table_test";
    pass.category = {"test"};
    pass.label = "Pass table";
    pass.inputKind = DataKind::Table;
    pass.outputs = {{"table", "Table", DataKind::Table, true}};
    pass.defaultOutput = "table";
    if (!r.add(pass, [](const QJsonObject &) { return std::make_unique<PassTable>(); }).ok())
        qFatal("pass registry");
    return r;
}
struct Fixture
{
    WorkflowDocument doc{registry()};
    NodeId root, joint, ordinary;
    QUuid pair = QUuid::createUuid();
    Fixture()
    {
        root = doc.addRoot("dual_test").nodeId;
        joint = doc.addChild(root, ChildSlot::Down, "cross_spectrum",
                             {{"psd_window", 64}, {"psd_overlap", 32}}, "default")
                    .nodeId;
        ordinary = doc.addChild(root, ChildSlot::Left, "demean", {}, "default").nodeId;
        doc.setSources(root, {{"same name", signal(), {}},
                              {"same name", signal(.5), {}},
                              {"unassigned", signal(.8), {}}});
    }
    BatchPair group(QString a = "default", QString b = "alternative") const
    {
        auto data = doc.datasets(root);
        return {pair, "A × B", {data[0].id, a}, {data[1].id, b}};
    }
};
NodeResult calculated()
{
    return CrossSpectrumTask({{"psd_window", 64}, {"psd_overlap", 32}})
        .execute({signal(), signal(.4)}, {});
}
} // namespace
class JointUiTests : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        setLanguage("en");
        applyTheme(*qApp);
    }
    void provenanceGrowthBounded_data();
    void provenanceGrowthBounded();
    void saveFailureRetry_data();
    void saveFailureRetry();
    void cancelDuringCalculation();
    void realPairWorkflow();
    void scopedManifestReplay();
    void orbitalPairPorts();
    void restoreLogMetricFromUndefinedCoherence();
    void compoundDocumentSignals();
    void fileImportIsOneEdit();
    void sourceOptionHistory();
    void longJointSourceCaptionKeepsPlotSpace();
    void restoredPairRefreshesPreview();
    void documentEditLock();
    void identitiesAndInvalidation();
    void mainWindowCompleteFlow();
    void batchDirectoryRefreshCoalesces_data();
    void batchDirectoryRefreshCoalesces();
    void missingAndDuplicatePairs();
    void executionPersistenceAndFailureIsolation();
    void cancelledRecords();
    void retainedResultBudget();
    void previewPortsAndSupersededCallbacks();
    void pairPanelScope();
    void pairingDialogCommitsActualChoices_data();
    void pairingDialogCommitsActualChoices();
    void largePairDialogCancelsEdits_data();
    void largePairDialogCancelsEdits();
    void downstreamPreviewAndPersistence();
    void plotMetricMasksAndExport();
    void asyncCsvAndDatabaseReload();
};
void JointUiTests::mainWindowCompleteFlow()
{
    QTemporaryDir dir;
    QStringList paths;
    for (int side = 0; side < 2; ++side)
    {
        auto path = dir.filePath(side ? "B.csv" : "A.csv");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Time,Signal\n");
        const auto data = signal(side * .4);
        const auto *series = dataAs<SeriesData>(data);
        for (int i = 0; i < series->x.size(); ++i)
            file.write((QString::number(series->x[i], 'g', 17) + "," +
                        QString::number(series->y[i], 'g', 17) + "\n")
                           .toUtf8());
        file.close();
        paths << path;
    }
    AppSettings settings;
    settings.databasePath = dir.filePath("application.sqlite");
    settings.language = "en";
    MainWindow window(settings, false);
    window.resize(1450, 900);
    window.show();
    const auto root = window.document()->addRoot("file_source").nodeId;
    window.document()->setFiles(root, paths);
    const auto joint = window.document()
                           ->addChild(root, ChildSlot::Down, "cross_spectrum",
                                      {{"psd_window", 64}, {"psd_overlap", 32}})
                           .nodeId;
    auto datasets = window.document()->datasets(root);
    const auto pair = QUuid::createUuid();
    QVERIFY(
        window.document()
            ->setPairs(joint,
                       {{pair, "A × B", {datasets[0].id, "series"}, {datasets[1].id, "series"}}})
            .isEmpty());
    window.canvas()->nodeSelected(joint);
    auto *preview = window.findChild<WorkflowPreviewPanel *>();
    QVERIFY(preview);
    QTRY_COMPARE_WITH_TIMEOUT(preview->controller()->state(),
                              WorkflowPreviewController::State::Ready, 10000);
    auto *sources = window.findChild<SourcePanel *>();
    QVERIFY(sources);
    QTRY_VERIFY_WITH_TIMEOUT(sources->readyToRun(), 10000);
    const auto pictures = qEnvironmentVariable("ASTER_JOINT_SCREENSHOT_DIR");
    if (!pictures.isEmpty())
    {
        QTest::qWait(100);
        QVERIFY(window.grab().save(pictures + "/joint-task.png"));
    }
    QSignalSpy finished(window.runner(), &RunController::batchReady);
    window.startRun();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    auto batch = qvariant_cast<std::shared_ptr<const BatchResult>>(finished[0][0]);
    QVERIFY(batch);
    QVERIFY2(batch->error.isEmpty(), qPrintable(batch->error));
    QCOMPARE(batch->items.size(), 3);
    for (const auto &item : batch->items)
    {
        QCOMPARE(item.computation, RunState::Succeeded);
        QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
    }
    if (!pictures.isEmpty())
    {
        QTest::qWait(500);
        QVERIFY(window.grab().save(pictures + "/joint-results.png"));
    }
}

void JointUiTests::compoundDocumentSignals()
{
    WorkflowDocument doc(makeScientificRegistry());
    QSignalSpy graph(&doc, &WorkflowDocument::changed), sources(&doc, &WorkflowDocument::sourcesChanged);
    bool complete = true;
    const auto observe = [&] {
        const auto& nodes = doc.model().definition().nodes;
        if (!nodes.isEmpty()) complete &= doc.sources().value(nodes[0].id).size() == 1;
        else complete &= doc.sources().isEmpty() && doc.grouping().isEmpty();
    };
    connect(&doc, &WorkflowDocument::changed, &doc, observe);
    connect(&doc, &WorkflowDocument::sourcesChanged, &doc, observe);
    OutputReference ref{"results.sqlite", 1, QUuid::createUuid(), QUuid::createUuid(), "series", "series"};
    const auto added = doc.addDatabaseSource(ref);
    QVERIFY2(added.ok(), qPrintable(added.error)); QVERIFY(complete);
    QCOMPARE(graph.size(), 1); QCOMPARE(sources.size(), 1);
    const auto id = doc.datasets(added.nodeId)[0].id;
    QVERIFY(doc.undo().ok()); QVERIFY(!doc.canUndo()); QVERIFY(complete);
    QVERIFY(doc.redo().ok()); QVERIFY(complete); QCOMPARE(doc.datasets(added.nodeId)[0].id, id);
    const auto revision = doc.revision();
    auto invalid = doc.beginEdit(); invalid.state.definition.layout.clear(); invalid.state.sources.clear();
    QVERIFY(!doc.prepareEdit(invalid).edit); QCOMPARE(doc.revision(), revision);
    QCOMPARE(graph.size(), 3); QCOMPARE(sources.size(), 3);
    QVERIFY(doc.reset().ok()); QVERIFY(doc.sources().isEmpty());
    QVERIFY(doc.undo().ok()); QCOMPARE(doc.datasets(added.nodeId)[0].id, id);
}
void JointUiTests::fileImportIsOneEdit()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("input.csv");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("0,1\n1,2\n"); file.close();
    WorkflowDocument doc(makeScientificRegistry());
    SourcePanel panel(&doc);
    panel.addFiles({path});
    QCOMPARE(doc.model().definition().nodes.size(), 1);
    const auto root = doc.model().definition().nodes[0].id;
    const auto dataset = doc.datasets(root)[0].id;
    QVERIFY(doc.undo().ok()); QVERIFY(doc.model().definition().nodes.isEmpty());
    QVERIFY(doc.sources().isEmpty()); QVERIFY(!doc.canUndo());
    QVERIFY(doc.redo().ok()); QCOMPARE(doc.datasets(root)[0].id, dataset);
    const auto revision = doc.revision(); panel.addFiles({path});
    QCOMPARE(doc.revision(), revision); // duplicate import is not an edit
}
void JointUiTests::sourceOptionHistory()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root = doc.addRoot("file_source").nodeId;
    doc.setFiles(root, {"a.csv", "b.csv"});
    const auto data = doc.datasets(root); const auto paths = doc.files(root);
    const auto original = doc.fileOptions(root, paths[0]);
    auto options = original; options["x_unit"] = "m";
    QVERIFY(doc.setFileOptions(root, {paths[0]}, options));
    const auto revision = doc.revision();
    QSignalSpy sourceSignal(&doc, &WorkflowDocument::sourcesChanged);
    QVERIFY(doc.setFileOptions(root, {paths[0]}, options));
    QCOMPARE(doc.revision(), revision); QCOMPARE(sourceSignal.size(), 0);
    QVERIFY(doc.undo().ok()); QCOMPARE(doc.fileOptions(root, paths[0]), original);
    QVERIFY(doc.redo().ok()); QCOMPARE(doc.fileOptions(root, paths[0])["x_unit"].toString(), QString("m"));
    doc.setFiles(root, {paths[0]}); QVERIFY(doc.undo().ok());
    QCOMPARE(doc.datasets(root)[1].id, data[1].id); QCOMPARE(doc.files(root), paths);
    auto bad = options; bad["y_column"] = -1;
    const auto before = doc.revision(); QVERIFY(!doc.setFileOptions(root, {paths[0]}, bad));
    QCOMPARE(doc.revision(), before);
}
void JointUiTests::longJointSourceCaptionKeepsPlotSpace()
{
    setLanguage("zh"); auto cleanup = qScopeGuard([] { setLanguage("en"); });
    Fixture f;
    auto sources = f.doc.sources().value(f.root);
    sources[0].label = "D:\\very-long-folder\\project\\datasets\\A-256.csv";
    sources[1].label = "/a/very/long/source/folder/B-256.csv";
    QVERIFY(f.doc.setSources(f.root, sources).isEmpty());
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    WorkflowPreviewPanel panel(&f.doc); panel.resize(300, 650); panel.show();
    panel.controller()->selectNode(f.joint); panel.controller()->selectPair(f.pair);
    QTRY_COMPARE_WITH_TIMEOUT(panel.controller()->state(), WorkflowPreviewController::State::Ready, 10000);
    auto *view = panel.findChild<PlotView *>("sourcePreviewPlot"); QVERIFY(view);
    QLabel *heading = nullptr;
    for (auto *label : panel.findChildren<QLabel *>("PreviewMethod"))
        if (label->text().startsWith(text("ui.pair_input"))) heading = label;
    QVERIFY(heading); QVERIFY(heading->text().contains("A-256.csv"));
    QVERIFY(!heading->text().contains("very-long-folder")); QVERIFY(heading->toolTip().contains("very-long-folder"));
    QVERIFY(heading->height() <= 2 * heading->fontMetrics().lineSpacing() + 4);
    QVERIFY(view->height() >= 150); QVERIFY(view->hasPlot());
    QCOMPARE(view->style().x.tickCount, 4);
    panel.findChild<QComboBox *>("pairInputPreview")->setCurrentIndex(1);
    QVERIFY(heading->text().contains("B-256.csv")); QVERIFY(heading->toolTip().contains("/a/very/long"));
    QCOMPARE(dataAs<SeriesData>(view->currentData())->yAxis.name, QString("Alternative"));
    // A long validation error must release its extra rows after correction.
    QLabel *status = nullptr;
    for (auto *label : panel.findChildren<QLabel *>("Muted"))
        if (label->text().startsWith(text("ui.preview_complete").section('%', 0, 0))) status = label;
    QVERIFY(status);
    const auto compactHeight = status->minimumHeight();
    QVERIFY(f.doc.setParameters(f.joint, {{"psd_window", 512}, {"psd_overlap", 256}}).ok());
    QTRY_COMPARE_WITH_TIMEOUT(panel.controller()->state(), WorkflowPreviewController::State::Failed, 10000);
    QVERIFY(status->minimumHeight() > compactHeight);
    QVERIFY(f.doc.setParameters(f.joint, {{"psd_window", 64}, {"psd_overlap", 32}}).ok());
    QTRY_COMPARE_WITH_TIMEOUT(panel.controller()->state(), WorkflowPreviewController::State::Ready, 10000);
    QTRY_VERIFY(status->minimumHeight() <= compactHeight);
    const auto captures = qEnvironmentVariable("ASTER_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) QVERIFY(panel.grab().save(captures + "/joint-caption-recovered.png"));
}
void JointUiTests::restoredPairRefreshesPreview()
{
    Fixture f; QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    WorkflowPreviewController preview(&f.doc);
    preview.selectNode(f.joint); preview.selectPair(f.pair); preview.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    auto sources = f.doc.sources().value(f.root); sources.removeAt(1);
    QVERIFY(f.doc.setSources(f.root, sources).isEmpty());
    QCOMPARE(preview.state(), WorkflowPreviewController::State::Idle); QVERIFY(!preview.result());
    QVERIFY(f.doc.undo().ok());
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    QCOMPARE(preview.result()->labelB, QString("same name · alternative"));
    QVERIFY(f.doc.redo().ok()); QCOMPARE(preview.state(), WorkflowPreviewController::State::Idle);
    QVERIFY(!preview.result()); QVERIFY(f.doc.undo().ok());
    auto draft = f.doc.beginEdit();
    draft.state.grouping[f.joint][0].b.port = "default";
    draft.state.sources[f.root][1].label = "Restored B";
    draft.state.definition.nodes[1].parameters["psd_window"] = 128;
    const auto prepared = f.doc.prepareEdit(draft); QVERIFY(prepared.edit);
    QVERIFY(f.doc.commitEdit(prepared.edit).ok());
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    QCOMPARE(preview.result()->labelB, QString("Restored B · default"));
    QVERIFY(f.doc.undo().ok());
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    QCOMPARE(preview.result()->labelB, QString("same name · alternative"));
}
void JointUiTests::documentEditLock()
{
    Fixture f; QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    const auto plan = f.doc.plan(); QVERIFY(plan.validation.ok());
    const auto manifest = plan.batches[0]->manifest();
    QSignalSpy changed(&f.doc, &WorkflowDocument::changed), sources(&f.doc, &WorkflowDocument::sourcesChanged);
    f.doc.setEditingLocked(true);
    QVERIFY(!f.doc.removeSubtree(f.root).ok()); QVERIFY(!f.doc.reset().ok());
    QVERIFY(!f.doc.setParameters(f.joint, {{"psd_window", 128}}).ok());
    QVERIFY(!f.doc.setPairs(f.joint, {}).isEmpty()); QVERIFY(!f.doc.setSources(f.root, {}).isEmpty());
    QVERIFY(!f.doc.undo().ok()); QVERIFY(!f.doc.redo().ok());
    QCOMPARE(changed.size(), 0); QCOMPARE(sources.size(), 0);
    f.doc.setEditingLocked(false); QVERIFY(f.doc.removeSubtree(f.root).ok());
    QVERIFY(f.doc.sources().isEmpty()); QVERIFY(f.doc.grouping().isEmpty());
    QCOMPARE(plan.batches[0]->manifest(), manifest);
    QVERIFY(f.doc.undo().ok()); QVERIFY(f.doc.plan().validation.ok());
}
void JointUiTests::identitiesAndInvalidation()
{
    Fixture f;
    const auto before = f.doc.datasets(f.root);
    QVERIFY(before[0].id != before[1].id);
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    auto selected = f.doc.sources().value(f.root);
    std::swap(selected[0], selected[1]);
    QVERIFY(f.doc.setSources(f.root, selected).isEmpty());
    QCOMPARE(f.doc.datasets(f.root)[1].id, before[0].id);
    auto plan = f.doc.plan();
    QVERIFY2(plan.validation.ok(), qPrintable(plan.validation.message()));
    QCOMPARE(plan.requests.size(), 0);
    QCOMPARE(plan.batches.size(), 1);
    const auto immutable = plan.batches.front()->manifest();
    selected.removeLast();
    f.doc.setSources(f.root, selected);
    QCOMPARE(plan.batches.front()->manifest(), immutable);
    QVERIFY(f.doc.plan().validation.ok()); // unassigned removal
    selected.removeFirst();
    f.doc.setSources(f.root, selected);
    QVERIFY(!f.doc.plan().validation.ok()); // missing B never silently replaced
    QVERIFY(!f.doc.pairPreview(f.joint, f.pair).validation.ok());
    // File imports preserve identity across options/reordering but not remove/reimport.
    WorkflowDocument files(makeScientificRegistry());
    auto root = files.addRoot("file_source").nodeId;
    files.setFiles(root, {"a.csv", "b.csv"});
    auto original = files.datasets(root);
    files.setFiles(root, {"b.csv", "a.csv"});
    QCOMPARE(files.datasets(root)[1].id, original[0].id);
    auto options = files.fileOptions(root, files.files(root)[1]);
    options["x_unit"] = "m";
    QVERIFY(files.setFileOptions(root, {files.files(root)[1]}, options));
    QCOMPARE(files.datasets(root)[1].id, original[0].id);
    files.setFiles(root, {"b.csv"});
    files.setFiles(root, {"b.csv", "a.csv"});
    QVERIFY(files.datasets(root)[1].id != original[0].id);
}
void JointUiTests::missingAndDuplicatePairs()
{
    Fixture f;
    QVERIFY(!f.doc.plan().validation.ok());
    auto pair = f.group();
    pair.b = pair.a;
    QVERIFY(!f.doc.setPairs(f.joint, {pair}).isEmpty());
    pair = f.group();
    auto duplicate = pair;
    duplicate.id = QUuid::createUuid();
    QVERIFY(!f.doc.setPairs(f.joint, {pair, duplicate}).isEmpty());
    pair.b.port = "missing";
    QVERIFY(!f.doc.setPairs(f.joint, {pair}).isEmpty());
    pair = f.group();
    auto reverse = pair;
    reverse.id = QUuid::createUuid();
    std::swap(reverse.a, reverse.b);
    QVERIFY(f.doc.setPairs(f.joint, {pair, reverse}).isEmpty());
    QVERIFY(f.doc.undo().ok());
    QVERIFY(f.doc.grouping().value(f.joint).isEmpty());
    QVERIFY(f.doc.redo().ok());
    QCOMPARE(f.doc.grouping().value(f.joint).size(), 2);
    auto scoped = f.doc.pairPreview(f.joint, pair.id);
    QVERIFY(scoped.validation.ok());
    QCOMPARE(scoped.snapshot->jobs().size(), 3);
    // Graph undo keeps the binding keyed by its node ID.
    QVERIFY(f.doc.removeSubtree(f.joint).ok());
    QVERIFY(f.doc.undo().ok());
    QVERIFY(f.doc.plan().validation.ok());
}
void JointUiTests::executionPersistenceAndFailureIsolation()
{
    Fixture f;
    auto group = f.group();
    auto bad = group;
    bad.id = QUuid::createUuid();
    bad.label = "bad grid";
    bad.b.dataset = f.doc.datasets(f.root)[2].id;
    auto data = f.doc.sources().value(f.root);
    data[2].data = signal(.8, .01);
    f.doc.setSources(f.root, data);
    QVERIFY(f.doc.setPairs(f.joint, {group, bad}).isEmpty());
    auto plan = f.doc.plan();
    QVERIFY(plan.validation.ok());
    QTemporaryDir dir;
    QString path = dir.filePath("results.sqlite");
    auto run = BatchRunner().run(plan, path, 2);
    QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
    QCOMPARE(run.items.size(), 5);
    int success = 0, failed = 0;
    ResultRepository repo;
    QVERIFY(repo.open(path, false).isEmpty());
    for (const auto &item : run.items)
    {
        QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
        auto stored = repo.load(item.persistence.recordId);
        QVERIFY(stored.ok());
        if (item.sourceLabel == "A × B")
        {
            QCOMPARE(item.computation, RunState::Succeeded);
            ++success;
            const auto &result = stored.run.nodes[f.joint];
            auto direct = CrossSpectrumTask({{"psd_window", 64}, {"psd_overlap", 32}})
                              .execute({data[0].data, makeData(
                                                          [&]
                                                          {
                                                              auto s =
                                                                  *dataAs<SeriesData>(data[1].data);
                                                              for (auto &v : s.y)
                                                                  v *= 3;
                                                              s.yAxis = {"Alternative", "v"};
                                                              return s;
                                                          }())},
                                       {});
            QCOMPARE(encodeData(result.outputs["cross_spectrum"]).bytes,
                     encodeData(direct.outputs["cross_spectrum"]).bytes);
            QCOMPARE(stored.run.provenance["input_records"].toArray().size(), 2);
            QVERIFY(stored.run.provenance.contains("batch_execution"));
            QCOMPARE(result.report["batch_pair"].toObject()["B"].toObject()["output"].toString(),
                     QString("alternative"));
        }
        else if (item.sourceLabel == "bad grid")
        {
            QCOMPARE(item.computation, RunState::Failed);
            ++failed;
        }
        else
        {
            QCOMPARE(item.computation, RunState::Succeeded);
            QVERIFY(stored.run.nodes.contains(f.ordinary));
        }
    }
    QCOMPARE(success, 1);
    QCOMPARE(failed, 1);
}
void JointUiTests::cancelledRecords()
{
    Fixture f;
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    QTemporaryDir dir;
    CancellationToken cancel;
    cancel.cancel();
    auto result = runJointBatch(f.doc.plan().batches.front(), dir.filePath("cancel.sqlite"), 1, {},
                                {}, cancel);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.items.size(), 4);
    ResultRepository repo;
    QVERIFY(repo.open(dir.filePath("cancel.sqlite"), false).isEmpty());
    for (const auto &item : result.items)
    {
        QVERIFY(item.persistence.saved());
        QCOMPARE(item.computation, RunState::Cancelled);
        QVERIFY(repo.load(item.persistence.recordId).run.executionOrder.isEmpty());
    }
}
void JointUiTests::retainedResultBudget()
{
    Fixture f;
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    QTemporaryDir dir;
    auto result = runJointBatch(f.doc.plan().batches.front(), dir.filePath("limited.sqlite"), 1, {},
                                {}, {}, 128);
    QVERIFY(result.error.contains("retention budget"));
    QCOMPARE(result.items.size(), 4);
    for (const auto &item : result.items)
        QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
}
void JointUiTests::previewPortsAndSupersededCallbacks()
{
    Fixture f;
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    WorkflowPreviewController preview(&f.doc);
    preview.selectNode(f.joint);
    preview.selectPair(f.pair);
    preview.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    auto result = preview.result();
    QVERIFY(result->paired);
    QCOMPARE(result->labelB, QString("same name · alternative"));
    QCOMPARE(dataAs<SeriesData>(result->second.outputs["series"])->yAxis.name,
             QString("Alternative"));
    QCOMPARE(result->output.report["y_axis_b"].toObject()["unit"].toString(), QString("v"));
    auto sources = f.doc.sources().value(f.root);
    sources[0].provenance["slow"] = true;
    f.doc.setSources(f.root, sources);
    QTRY_VERIFY_WITH_TIMEOUT(preview.busy(), 10000);
    auto changed = f.group("alternative", "default");
    QVERIFY(f.doc.setPairs(f.joint, {changed}).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    QCOMPARE(preview.result()->labelB, QString("same name · default"));
    QCOMPARE(preview.result()->output.report["y_axis_b"].toObject()["unit"].toString(),
             QString("u"));
    sources.removeAt(1);
    f.doc.setSources(f.root, sources);
    QCOMPARE(preview.state(), WorkflowPreviewController::State::Idle);
    QVERIFY(!preview.result());
    QTest::qWait(150);
    QVERIFY(!preview.result());
}
void JointUiTests::pairPanelScope()
{
    Fixture f;
    PairingPanel panel(&f.doc);
    panel.show();
    panel.selectNode(f.root);
    QVERIFY(panel.isHidden());
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    panel.selectNode(f.joint);
    QVERIFY(!panel.isHidden());
    auto *choice = panel.findChild<QComboBox *>("previewPair");
    QVERIFY(choice);
    QCOMPARE(choice->count(), 1);
    QCOMPARE(choice->currentData().toUuid(), f.pair);
    panel.selectNode(f.ordinary);
    QVERIFY(panel.isHidden());
}
void JointUiTests::pairingDialogCommitsActualChoices_data()
{
    QTest::addColumn<bool>("keyboard");
    QTest::newRow("mouse") << false;
    QTest::newRow("keyboard") << true;
}
void JointUiTests::pairingDialogCommitsActualChoices()
{
    QFETCH(bool,keyboard);Fixture f;PairingPanel panel(&f.doc);panel.resize(1050,700);panel.selectNode(f.joint);panel.show();
    panel.findChild<QPushButton*>("managePairs")->click();
    auto* dialog=panel.findChild<PairingDialog*>();QVERIFY(dialog);
    dialog->findChild<QPushButton*>("addPair")->click();
    QVERIFY(aster::test::selectMember(aster::test::openMember(dialog,0,1,keyboard),f.root,0,keyboard));
    QVERIFY(aster::test::selectMember(aster::test::openMember(dialog,0,2,keyboard),f.root,3,keyboard));
    const auto pictures=qEnvironmentVariable("ASTER_JOINT_SCREENSHOT_DIR");
    if(!pictures.isEmpty())QVERIFY(dialog->grab().save(pictures+"/pair-dialog.png"));
    dialog->findChild<QPushButton*>("pairingConfirm")->click();
    const auto pairs=f.doc.grouping().value(f.joint);QCOMPARE(pairs.size(),1);
    QCOMPARE(pairs[0].a.dataset,f.doc.datasets(f.root)[0].id);QCOMPARE(pairs[0].b.dataset,f.doc.datasets(f.root)[1].id);
    QCOMPARE(pairs[0].b.port,QString("alternative"));QVERIFY(pairs[0].a.node.isNull());QVERIFY(pairs[0].b.node.isNull());
}
void JointUiTests::downstreamPreviewAndPersistence()
{
    Fixture f;
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    auto child = f.doc.addChild(f.joint, ChildSlot::Down, "pass_table_test");
    QVERIFY(child.ok());
    WorkflowPreviewController preview(&f.doc);
    preview.selectNode(child.nodeId);
    preview.selectPair(f.pair);
    preview.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(), WorkflowPreviewController::State::Ready, 10000);
    QVERIFY(preview.result()->paired);
    QVERIFY(dataAs<TableData>(preview.result()->output.outputs["table"]));
    QCOMPARE(dataAs<SeriesData>(preview.result()->second.outputs["series"])->yAxis.name,
             QString("Alternative"));
    PairingPanel panel(&f.doc);
    panel.selectNode(child.nodeId);
    QVERIFY(!panel.isHidden());
    QTemporaryDir dir;
    auto result = BatchRunner().run(f.doc.plan(), dir.filePath("downstream.sqlite"), 1);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    ResultRepository repo;
    QVERIFY(repo.open(dir.filePath("downstream.sqlite"), false).isEmpty());
    for (const auto &item : result.items)
        if (item.sourceLabel == "A × B")
        {
            QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
            auto stored = repo.load(item.persistence.recordId);
            QVERIFY(stored.ok());
            QCOMPARE(stored.run.nodes.size(), 2);
            QVERIFY(stored.run.nodes.contains(child.nodeId));
            QCOMPARE(stored.run.executionOrder.size(), 2);
        }
}
void JointUiTests::plotMetricMasksAndExport()
{
    auto result = calculated();
    QCOMPARE(result.state, NodeState::Succeeded);
    auto table = *dataAs<TableData>(result.outputs["cross_spectrum"]);
    if (table.defined.isEmpty())
        for (const auto &row : table.rows)
            table.defined << QVector<quint8>(row.size(), 1);
    table.defined[4][2] = 0;
    table.rows[4][2] = 0;
    table.rows[5][3] = 179;
    table.rows[6][3] = -179;
    result.outputs["cross_spectrum"] = makeData(table);
    PlotView view;
    view.resize(640, 480);
    view.showOutput(result, "cross_spectrum");
    QVERIFY(view.hasPlot());
    auto *metric = view.findChild<QComboBox *>("crossMetric");
    QVERIFY(metric);
    QCOMPARE(metric->count(), 8);
    QCOMPARE(view.style().crossMetric, QString("coherence"));
    auto appearance = view.style(); appearance.legend = false; appearance.axisFontSize = 8;
    QVERIFY(view.applyStyle(appearance).isEmpty());
    QCOMPARE(view.plotWidget()->graphCount(), 1);
    bool gap = false;
    for (auto it = view.plotWidget()->graph()->data()->constBegin();
         it != view.plotWidget()->graph()->data()->constEnd(); ++it)
        gap |= std::isnan(it->value);
    QVERIFY(gap);
    metric->setCurrentIndex(metric->findData("phase_degrees"));
    QVERIFY(view.hasPlot());
    QCOMPARE(view.style().crossMetric, QString("phase_degrees"));
    QVERIFY(!view.style().legend); QCOMPARE(view.style().axisFontSize, 8.);
    gap = false;
    for (auto it = view.plotWidget()->graph()->data()->constBegin();
         it != view.plotWidget()->graph()->data()->constEnd(); ++it)
        gap |= std::isnan(it->value);
    QVERIFY(gap);
    QTemporaryDir dir;
    QVERIFY(view.exportCsv(dir.filePath("all.csv")).isEmpty());
    QFile csv(dir.filePath("all.csv"));
    QVERIFY(csv.open(QIODevice::ReadOnly));
    auto content = csv.readAll();
    QVERIFY(content.startsWith("\"frequency (1/(ka))\""));
    QVERIFY(content.contains("phase_degrees (deg)"));
    QVERIFY(view.exportPng(dir.filePath("phase.png")));
    auto style = view.style();
    PlotView other;
    other.showOutput(result, "cross_spectrum");
    QVERIFY(other.applyStyle(style).isEmpty());
    QCOMPARE(other.style().crossMetric, QString("phase_degrees"));
    metric->setCurrentIndex(metric->findData("table"));
    QVERIFY(!view.hasPlot());
    metric->setCurrentIndex(metric->findData("auto_a"));
    QVERIFY(view.hasPlot());
    for (auto &mask : table.defined)
    {
        mask[2] = 0;
        mask[3] = 0;
    }
    for (auto &row : table.rows)
    {
        row[2] = 0;
        row[3] = 0;
    }
    result.outputs["cross_spectrum"] = makeData(table);
    view.showOutput(result, "cross_spectrum");
    metric->setCurrentIndex(metric->findData("coherence"));
    QVERIFY(!view.hasPlot());
    metric->setCurrentIndex(metric->findData("cross_magnitude"));
    QVERIFY(view.hasPlot());
}
void JointUiTests::asyncCsvAndDatabaseReload()
{
    Fixture f;
    QVERIFY(f.doc.setPairs(f.joint, {f.group()}).isEmpty());
    QTemporaryDir dir;
    QString path = dir.filePath("r.sqlite");
    auto result = BatchRunner().run(f.doc.plan(), path, 1);
    QVERIFY(result.error.isEmpty());
    BatchItem pair;
    for (const auto &item : result.items)
        if (item.sourceLabel == "A × B")
            pair = item;
    QVERIFY(pair.persistence.saved());
    OutputReference ref{path,    pair.persistence.recordId, pair.runId,
                        f.joint, "cross_spectrum",          "Table"};
    auto full = loadPlotData({ref}, {}, {});
    QCOMPARE(full.state, NodeState::Succeeded);
    QCOMPARE(full.report["cross_spectrum_schema"].toInt(), 1);
    QVERIFY(!full.report["display_reduced"].toBool());
    ExportController exporter;
    QSignalSpy finished(&exporter, &ExportController::finished);
    QVERIFY(exporter.startOutput(ref, dir.filePath("export.csv")));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QVERIFY(finished[0][1].toString().isEmpty());
    QFile file(dir.filePath("export.csv"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readLine().contains("phase_degrees (deg)"));
    PlotView view;
    view.resize(640, 480);
    view.showOutput(full, "cross_spectrum");
    auto *metric = view.findChild<QComboBox *>("crossMetric");
    metric->setCurrentIndex(metric->findData("phase_degrees"));
    FigureOptions options;
    options.path = dir.filePath("figure.pdf");
    options.format = FigureOptions::Format::Pdf;
    auto error =
        renderFigure({{{ref}, view.styleWithViewport(), view.plotRequest()}}, {full}, options);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(QFileInfo(options.path).size() > 1000);
}

namespace
{
QString databaseSql(const QString &path, const QString &command)
{
    const auto name=QUuid::createUuid().toString(); QString error;
    { auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);
      if(!db.open())error=db.lastError().text();
      else {QSqlQuery query(db);if(!query.exec(command))error=query.lastError().text();} }
    QSqlDatabase::removeDatabase(name);return error;
}
void auditMetric(const QString &name,const QJsonObject &record)
{
    const auto dir=qEnvironmentVariable("ASTER_JOINT_AUDIT_DIR");
    if(dir.isEmpty())return;
    QFile file(dir+"/"+name+".json");
    if(file.open(QIODevice::WriteOnly))file.write(QJsonDocument(record).toJson());
}
}
void JointUiTests::provenanceGrowthBounded_data()
{
    QTest::addColumn<int>("groups");
    QTest::newRow("4-pairs")<<4;QTest::newRow("64-pairs")<<64;QTest::newRow("128-pairs")<<128;
}
void JointUiTests::provenanceGrowthBounded()
{
    QFETCH(int,groups);
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("input_series").nodeId;
    const auto joint=doc.addChild(root,ChildSlot::Down,"cross_spectrum",{{"psd_window",64},{"psd_overlap",32}}).nodeId;
    QVector<SourceItem> sources;
    for(int i=0;i<=groups;++i)sources<<SourceItem{QString("Input %1").arg(i),signal(i*.01),{}};
    QVERIFY(doc.setSources(root,sources).isEmpty());
    const auto datasets=doc.datasets(root);QVector<BatchPair> pairs;
    for(int i=0;i<groups;++i)pairs<<BatchPair{QUuid::createUuid(),QString("Pair %1").arg(i),{datasets[i].id,"series"},{datasets.back().id,"series"}};
    QVERIFY(doc.setPairs(joint,pairs).isEmpty());QTemporaryDir dir;
    QElapsedTimer clock;clock.start();
    const auto batch=BatchRunner().run(doc.plan(),dir.filePath("benchmark.sqlite"),4);
    const auto elapsed=clock.elapsed();QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
    QCOMPARE(batch.items.size(),2*groups+1);
    ResultRepository repo;QVERIFY(repo.open(dir.filePath("benchmark.sqlite"),false).isEmpty());
    qint64 total=0,largest=0;
    for(const auto &item:batch.items)
    {
        QVERIFY(item.persistence.saved());QCOMPARE(item.computation,RunState::Succeeded);
        const auto loaded=repo.load(item.persistence.recordId,true);QVERIFY(loaded.ok());
        if(loaded.run.provenance.contains("batch_pair"))
        {
            const auto n=QJsonDocument(loaded.run.provenance).toJson(QJsonDocument::Compact).size();
            total+=n;largest=std::max<qint64>(largest,n);
        }
    }
    const QJsonObject metric{{"pairs",groups},{"elapsed_ms",double(elapsed)},
        {"pair_provenance_bytes",double(total)},{"max_pair_provenance_bytes",double(largest)},
        {"database_bytes",double(QFileInfo(dir.filePath("benchmark.sqlite")).size())}};
    qInfo().noquote()<<QJsonDocument(metric).toJson(QJsonDocument::Compact);
    auditMetric(QString("pairs-%1").arg(groups),metric);
    // One pair's reproducibility must not duplicate every unrelated pair's metadata.
    QVERIFY2(largest<32768,qPrintable(QString("One pair retains %1 metadata bytes").arg(largest)));
}
void JointUiTests::saveFailureRetry_data()
{
    QTest::addColumn<bool>("atPair");QTest::addColumn<int>("concurrency");
    QTest::newRow("source-serial")<<false<<1;QTest::newRow("source-parallel")<<false<<4;
    QTest::newRow("pair-serial")<<true<<1;QTest::newRow("pair-parallel")<<true<<4;
}
void JointUiTests::saveFailureRetry()
{
    QFETCH(bool,atPair);QFETCH(int,concurrency);
    Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.group()}).isEmpty());
    QTemporaryDir dir;const auto path=dir.filePath("retry.sqlite");
    {ResultRepository r;QVERIFY(r.open(path).isEmpty());}
    const QString label=atPair?QString("A × B"):QString("same name");
    QVERIFY(databaseSql(path,"CREATE TRIGGER reject_payload BEFORE INSERT ON aster_outputs "
        "WHEN (SELECT source_label FROM aster_runs WHERE id=NEW.run_id)='"+label+"' "
        "BEGIN SELECT RAISE(ABORT,'injected late save failure'); END").isEmpty());
    auto batch=BatchRunner().run(f.doc.plan(),path,concurrency);
    QVERIFY(batch.error.contains("could not be saved"));
    QCOMPARE(batch.items.size(),4); // Three sources and one pair all get terminal records.
    int retryCount=0;
    QVERIFY(databaseSql(path,"DROP TRIGGER reject_payload").isEmpty());
    ResultRepository repo;QVERIFY(repo.open(path,false).isEmpty());
    for(const auto &item:batch.items)
        if(!item.persistence.saved())
        {
            QVERIFY(item.unsavedResult);++retryCount;
            const auto saved=repo.save(*item.unsavedResult);QVERIFY2(saved.saved(),qPrintable(saved.error));
            auto loaded=repo.load(saved.recordId);QVERIFY(loaded.ok());
            QCOMPARE(loaded.run.runId,item.runId);
            for(auto it=item.unsavedResult->nodes.begin();it!=item.unsavedResult->nodes.end();++it)
                for(auto out=it->outputs.begin();out!=it->outputs.end();++out)
                    QCOMPARE(encodeData(loaded.run.nodes[it.key()].outputs[out.key()]).bytes,encodeData(out.value()).bytes);
        }
    QVERIFY(retryCount>0);QCOMPARE(repo.list().records.size(),4);
}
void JointUiTests::cancelDuringCalculation()
{
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("input_series").nodeId;
    SeriesData a;a.xAxis={"Time","ka"};a.yAxis={"Signal","1"};
    for(int i=0;i<32768;++i){a.x<<i*.25;a.y<<std::sin(i*.1);}
    auto b=a;for(auto &v:b.y)v*=2;
    QVERIFY(doc.setSources(root,{{"A",makeData(a),{}},{"B",makeData(b),{}}}).isEmpty());
    const auto joint=doc.addChild(root,ChildSlot::Down,"cross_spectrum",{{"psd_window",512},{"psd_overlap",510}}).nodeId;
    const auto ds=doc.datasets(root);QVERIFY(doc.setPairs(joint,{{QUuid::createUuid(),"cancel pair",{ds[0].id,"series"},{ds[1].id,"series"}}}).isEmpty());
    QTemporaryDir dir;CancellationToken cancellation;bool requested=false;
    QElapsedTimer timer;timer.start();
    auto result=BatchRunner().run(doc.plan(),dir.filePath("cancel.sqlite"),2,[&](const RunEvent &e){
        if(e.nodeId==joint && e.progressPercent>=1){requested=true;cancellation.cancel();}
    },{},cancellation);
    QVERIFY(requested);QCOMPARE(result.items.size(),3);
    for(const auto &item:result.items)if(item.sourceLabel=="cancel pair")
    {
        QCOMPARE(item.computation,RunState::Cancelled);QVERIFY(item.persistence.saved());
        ResultRepository repo;QVERIFY(repo.open(dir.filePath("cancel.sqlite"),false).isEmpty());
        const auto loaded=repo.load(item.persistence.recordId);QVERIFY(loaded.ok());
        QVERIFY(loaded.run.nodes[joint].outputs.isEmpty());QCOMPARE(loaded.run.executionOrder.size(),1);
    }
    auditMetric("cancellation",{{"elapsed_ms",double(timer.elapsed())},{"requested_during_algorithm",requested}});
}
void JointUiTests::realPairWorkflow()
{
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("input_series").nodeId;
    auto a=examples::load("odp846_d18o","age"),b=examples::load("odp846_d13c","age");
    QVERIFY(doc.setSources(root,{{"ODP 846 oxygen",makeData(a.series),a.provenance},{"ODP 846 carbon",makeData(b.series),b.provenance}}).isEmpty());
    const auto prep=doc.addChild(root,ChildSlot::Down,"preprocess",{{"spacing_mode","fixed"},{"spacing",1.}}).nodeId;
    const auto joint=doc.addChild(prep,ChildSlot::Down,"cross_spectrum",{{"psd_window",128},{"psd_overlap",64}}).nodeId;
    const auto ds=doc.datasets(root);const auto pair=QUuid::createUuid();
    QVERIFY(doc.setPairs(joint,{{pair,"ODP 846: δ18O / δ13C",{ds[0].id,"series"},{ds[1].id,"series"}}}).isEmpty());
    WorkflowPreviewController preview(&doc);preview.selectNode(joint);preview.selectPair(pair);preview.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
    const auto previewBytes=encodeData(preview.result()->output.outputs["cross_spectrum"]).bytes;
    QTemporaryDir dir;
    for(int concurrency:{1,4})
    {
        const auto path=dir.filePath(QString("real-%1.sqlite").arg(concurrency));
        const auto batch=BatchRunner().run(doc.plan(),path,concurrency);QVERIFY(batch.error.isEmpty());
        QCOMPARE(batch.items.size(),3);ResultRepository repo;QVERIFY(repo.open(path,false).isEmpty());
        for(const auto &item:batch.items)
        {
            QVERIFY(item.persistence.saved());
            auto stored=repo.load(item.persistence.recordId);QVERIFY(stored.ok());
            if(!stored.run.provenance.contains("batch_pair"))continue;
            QCOMPARE(stored.displayName,QString("ODP 846: δ18O / δ13C"));
            QCOMPARE(encodeData(stored.run.nodes[joint].outputs["cross_spectrum"]).bytes,previewBytes);
            const auto source=stored.run.provenance["batch_pair"].toObject();
            QCOMPARE(source["A"].toObject()["source"].toObject()["sha256"],a.provenance["sha256"]);
            QCOMPARE(source["B"].toObject()["source"].toObject()["sha256"],b.provenance["sha256"]);
        }
    }
}


void JointUiTests::restoreLogMetricFromUndefinedCoherence()
{
    auto zero=*dataAs<SeriesData>(signal());for(auto &v:zero.y)v=0;
    auto result=CrossSpectrumTask({{"psd_window",64},{"psd_overlap",32}}).execute({makeData(zero),signal()},{});
    QCOMPARE(result.state,NodeState::Succeeded);
    PlotView original;original.showOutput(result,"cross_spectrum");
    QVERIFY(!original.hasPlot());
    original.findChild<QComboBox *>("crossMetric")->setCurrentIndex(6); // auto_b
    QVERIFY(original.hasPlot());auto style=original.style();style.y.logarithmic=true;
    QVERIFY(original.applyStyle(style).isEmpty());style=original.styleWithViewport();
    PlotView restored;restored.showOutput(result,"cross_spectrum");
    const auto error=restored.applyStyle(style);QVERIFY2(error.isEmpty(),qPrintable(error));
    QVERIFY(restored.hasPlot());QVERIFY(restored.style().y.logarithmic);
    QTemporaryDir dir;FigureOptions options;options.path=dir.filePath("auto-b.png");
    OutputReference ref{dir.filePath("unused.sqlite"),1,QUuid::createUuid(),QUuid::createUuid(),"cross_spectrum","Table"};
    const auto exportError=renderFigure({{{ref},style,{}}},{result},options);
    QVERIFY2(exportError.isEmpty(),qPrintable(exportError));
}


void JointUiTests::scopedManifestReplay()
{
    Fixture f;
    const auto datasets=f.doc.datasets(f.root);
    const BatchPair other{QUuid::createUuid(),"other",{datasets[1].id,"default"},{datasets[2].id,"default"}};
    QVERIFY(f.doc.setPairs(f.joint,{f.group(),other}).isEmpty());
    const auto child=f.doc.addChild(f.joint,ChildSlot::Down,"pass_table_test",{},"cross_spectrum").nodeId;
    const auto plan=f.doc.plan().batches.front();
    const auto manifest=plan->manifest(f.pair);
    QCOMPARE(manifest["scope"].toString(),QString("item_dependencies"));
    QCOMPARE(manifest["datasets"].toArray().size(),2);
    QCOMPARE(manifest["jobs"].toArray().size(),4); // A, B, joint and its descendant
    const auto decoded=workflowFromJson(manifest["workflow"].toObject());
    QVERIFY(decoded.validation.ok());QCOMPARE(decoded.definition.nodes.size(),3);
    for(const auto &node:decoded.definition.nodes)QVERIFY(node.id!=f.ordinary);
    const auto grouping=batchGroupingFromJson(manifest["grouping"].toObject());
    QVERIFY(grouping.validation.ok());QCOMPARE(grouping.grouping.size(),1);
    QCOMPARE(grouping.grouping[f.joint].size(),1);QCOMPARE(grouping.grouping[f.joint][0].id,f.pair);
    QCOMPARE(grouping.grouping[f.joint][0].b.port,QString("alternative"));
    WorkflowModel model(registry());QVERIFY(model.replaceDefinition(decoded.definition).ok());
    const auto rebuilt=prepareBatch(model.freeze().snapshot,f.root,{datasets[0],datasets[1]},grouping.grouping);
    QVERIFY2(rebuilt.validation.ok(),qPrintable(rebuilt.validation.message()));
    QByteArray original,replayed;
    BatchObserver first,second;
    first.completed=[&](const BatchJob &j,const NodeResult &r){if(j.node==child&&j.item==f.pair)original=encodeData(r.outputs["table"]).bytes;};
    second.completed=[&](const BatchJob &j,const NodeResult &r){if(j.node==child&&j.item==f.pair)replayed=encodeData(r.outputs["table"]).bytes;};
    QCOMPARE(BatchWorkflowExecutor().run(plan,{},first).state,RunState::Succeeded);
    QCOMPARE(BatchWorkflowExecutor().run(rebuilt.snapshot,{},second).state,RunState::Succeeded);
    QVERIFY(!original.isEmpty());QCOMPARE(replayed,original);
    QVERIFY(plan->manifest(QUuid::createUuid()).isEmpty());
    QCOMPARE(plan->manifest()["datasets"].toArray().size(),3);
    QCOMPARE(batchGroupingFromJson(plan->manifest()["grouping"].toObject()).grouping[f.joint].size(),2);
}
void JointUiTests::orbitalPairPorts()
{
    WorkflowDocument doc(makeScientificRegistry());
    const auto root=doc.addRoot("precession_tilt_source",{{"orbit_start_ka",0.},{"orbit_end_ka",2000.},{"orbit_step_ka",1.}}).nodeId;
    const auto joint=doc.addChild(root,ChildSlot::Down,"cross_spectrum",{{"psd_window",256},{"psd_overlap",128}},"precession").nodeId;
    const auto data=doc.datasets(root);QCOMPARE(data.size(),1);
    const auto pair=QUuid::createUuid();
    QVERIFY(doc.setPairs(joint,{{pair,"PT-ZB18a",{data[0].id,"obliquity"},{data[0].id,"precession"}}}).isEmpty());
    WorkflowPreviewController preview(&doc);preview.selectNode(joint);preview.selectPair(pair);preview.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
    auto result=preview.result();QVERIFY(result->paired);
    const auto a=dataAs<SeriesData>(result->original.outputs["series"]),b=dataAs<SeriesData>(result->second.outputs["series"]);
    QVERIFY(a);QVERIFY(b);QCOMPARE(a->yAxis.unit,QString("deg"));QCOMPARE(b->yAxis.unit,QString("1"));
    QVERIFY(a->yAxis.name!=b->yAxis.name);QVERIFY(a->y!=b->y);
    const auto expected=encodeData(result->output.outputs["cross_spectrum"]).bytes;
    QTemporaryDir dir;const auto path=dir.filePath("orbital.sqlite");
    const auto batch=BatchRunner().run(doc.plan(),path,2);QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
    QCOMPARE(batch.items.size(),2);ResultRepository repo;QVERIFY(repo.open(path,false).isEmpty());
    bool found=false;
    for(const auto &item:batch.items){QVERIFY(item.persistence.saved());const auto r=repo.load(item.persistence.recordId);QVERIFY(r.ok());
        if(r.run.provenance.contains("batch_pair")){found=true;QCOMPARE(encodeData(r.run.nodes[joint].outputs["cross_spectrum"]).bytes,expected);}}
    QVERIFY(found);
}


void JointUiTests::batchDirectoryRefreshCoalesces_data()
{
    QTest::addColumn<bool>("partialError");
    QTest::newRow("completed") << false;
    QTest::newRow("saved-before-error") << true;
}
void JointUiTests::batchDirectoryRefreshCoalesces()
{
    QFETCH(bool, partialError);
    QTemporaryDir dir;
    const auto path = dir.filePath("refresh.sqlite");
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("sine_source",{0,0}).nodeId;
    const auto run = WorkflowExecutor().run({model.freeze().snapshot,root,{"saved",{}, {}}});
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto receipt = repo.save(run); QVERIFY(receipt.saved());
    AppSettings settings; settings.databasePath=path; settings.language="en";
    MainWindow window(settings,false); window.show();
    QTRY_VERIFY_WITH_TIMEOUT(!window.results()->databaseBusy(),5000);
    auto* service=window.findChild<RecordDirectoryService*>(); QVERIFY(service);
    QSignalSpy busy(service,&RecordDirectoryService::busyChanged);
    auto* progress=window.findChild<QProgressBar*>("runProgress"); QVERIFY(progress);
    progress->setRange(0,1000); progress->setValue(0);
    QSignalSpy paintedProgress(progress,&QProgressBar::valueChanged);
    BatchItem item{run.runId,run.sourceLabel,RunState::Succeeded,receipt,{},root};
    // A burst of saved notifications must not issue a query per record.
    for (int i=0;i<1000;++i) {
        RunEvent event; event.runId=run.runId; event.root=root;
        event.nodeId=QUuid::createUuid(); event.state=NodeState::Succeeded;
        window.runner()->eventReady(event);
        window.runner()->itemReady(item);
    }
    QCOMPARE(busy.count(),0);
    QCOMPARE(paintedProgress.count(),0);
    QTRY_COMPARE_WITH_TIMEOUT(busy.count(),2,5000);
    QCOMPARE(progress->value(),1000); QCOMPARE(paintedProgress.count(),1);
    QTRY_VERIFY_WITH_TIMEOUT(!window.results()->databaseBusy(),5000);
    busy.clear();
    auto* status=window.findChild<QLabel*>("runStatus"); QVERIFY(status);
    auto* cancel=window.findChild<QPushButton*>("cancelRun"); QVERIFY(cancel);
    RunEvent inProgress; inProgress.state=NodeState::Running;
    inProgress.progressPercent=42; inProgress.sourceLabel="slow input";
    window.runner()->eventReady(inProgress); // Delayed progress must not replace cancellation.
    cancel->click(); const auto cancelling=status->text();
    window.runner()->eventReady(inProgress);
    QTest::qWait(150); QCOMPARE(status->text(),cancelling);
    window.runner()->itemReady(item);
    auto batch=std::make_shared<BatchResult>();
    if (partialError) batch->error="Stopped after a saved result";
    else batch->items.push_back(item);
    window.runner()->batchReady(batch);
    QTRY_COMPARE_WITH_TIMEOUT(busy.count(),2,5000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.results()->databaseBusy(),5000);
    if (!partialError) QCOMPARE(window.results()->recordId(),receipt.recordId);
    const auto finished=status->text(); QVERIFY(finished!=cancelling);
    QTest::qWait(200); // A stale delayed refresh must not fire after completion.
    QCOMPARE(busy.count(),2); QCOMPARE(status->text(),finished);
    QVERIFY(repo.load(receipt.recordId).ok());
    // A new run clears the cancellation latch and accepts progress again.
    window.runner()->busyChanged(true);
    window.runner()->eventReady(inProgress);
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("slow input"),1000);
    window.runner()->batchReady(std::make_shared<BatchResult>());
    window.runner()->busyChanged(false);
}

void JointUiTests::largePairDialogCancelsEdits_data()
{
    QTest::addColumn<int>("count");
    QTest::newRow("1000-pairs") << 1000;
    QTest::newRow("9999-pairs") << 9999;
}
void JointUiTests::largePairDialogCancelsEdits()
{
    QFETCH(int, count);
    WorkflowDocument doc(makeScientificRegistry());const auto root=doc.addRoot("input_series").nodeId;
    const auto joint=doc.addChild(root,ChildSlot::Down,"cross_spectrum").nodeId;
    QVector<SourceItem> sources;const auto input=signal();
    for(int i=0;i<=count;++i)sources<<SourceItem{QString("Input %1").arg(i),input,{}};
    QVERIFY(doc.setSources(root,sources).isEmpty());const auto datasets=doc.datasets(root);QVector<BatchPair> pairs;
    for(int i=0;i<count;++i)pairs<<BatchPair{QUuid::createUuid(),QString("Pair %1").arg(i),{datasets[i].id,"series"},{datasets.back().id,"series"}};
    QVERIFY(doc.setPairs(joint,pairs).isEmpty());PairingPanel panel(&doc);panel.resize(1050,700);panel.selectNode(joint);panel.show();
    QElapsedTimer timer;timer.start();panel.findChild<QPushButton*>("managePairs")->click();
    auto* dialog=panel.findChild<PairingDialog*>();QVERIFY(dialog);auto* table=dialog->findChild<QTableWidget*>("pairTable");
    QCOMPARE(table->rowCount(),count);QCOMPARE(table->findChildren<QComboBox*>().size(),0);
    auto* picker=aster::test::openMember(dialog,count-1,2);QVERIFY(picker);
    QCOMPARE(picker->findChild<QTableView*>("jointMemberTable")->model()->rowCount(),count+1);
    QVERIFY(aster::test::selectMember(picker,root,count-2));
    QCOMPARE(table->item(count-1,2)->data(Qt::UserRole).toMap().value("dataset").toUuid(),datasets[count-2].id);
    QVERIFY(table->item(count-1,2)->toolTip().contains(QString("Input %1").arg(count-2)));
    qInfo()<<count<<"pair editor and candidate selection, ms:"<<timer.elapsed();QTest::keyClick(dialog,Qt::Key_Escape);
    const auto retained=doc.grouping().value(joint);QCOMPARE(retained.size(),count);
    for(int i=0;i<retained.size();++i){QCOMPARE(retained[i].id,pairs[i].id);QVERIFY(retained[i].a==pairs[i].a);QVERIFY(retained[i].b==pairs[i].b);}
}

QTEST_MAIN(JointUiTests)
#include "joint_ui_tests.moc"
