// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "odp_examples_audit.h"
#include "io/examples/example_catalog.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <cstring>
#include <future>
#include <stdexcept>
using namespace aster;
namespace
{
QByteArray pairDigest(const SeriesData &series)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (qsizetype i = 0; i < series.x.size(); ++i)
        for (double value : {series.x[i], series.y[i]})
        {
            quint64 bits;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            bits = qToLittleEndian(bits);
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(&bits), sizeof(bits)));
        }
    return hash.result().toHex();
}
} // namespace
class OdpExamplesAudit : public QObject
{
    Q_OBJECT
  private slots:
    void allPublishedValues_data();
    void allPublishedValues();
    void workflowAndPersistence_data();
    void workflowAndPersistence();
    void cocoDepthWorkflow_data();
    void cocoDepthWorkflow();
    void invalidAndCancellation();
    void concurrentIndependentReads();
};
void OdpExamplesAudit::allPublishedValues_data()
{
    QTest::addColumn<QJsonObject>("reference");
    QFile file(QFINDTESTDATA("fixtures/odp_reference.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto array = QJsonDocument::fromJson(file.readAll()).array();
    QCOMPARE(array.size(), 10);
    for (const auto &value : array)
    {
        const auto row = value.toObject();
        QTest::newRow(qPrintable(row["id"].toString() + "/" + row["axis"].toString())) << row;
    }
}
void OdpExamplesAudit::allPublishedValues()
{
    QFETCH(QJsonObject, reference);
    const auto loaded = examples::load(reference["id"].toString(), reference["axis"].toString());
    QCOMPARE(loaded.series.x.size(), reference["count"].toInt());
    QCOMPARE(loaded.provenance["omitted_missing_pairs"], reference["omitted"]);
    QCOMPARE(loaded.provenance["sha256"], reference["source_sha256"]);
    QCOMPARE(pairDigest(loaded.series), reference["pairs_sha256"].toString().toLatin1());
    QVERIFY(validateData(makeData(loaded.series)).isEmpty());
    QCOMPARE(loaded.provenance["license"].toString(), QString("CC-BY-3.0"));
    QVERIFY(loaded.provenance["doi"].toString().startsWith("https://doi.org/10.1594/PANGAEA."));
    QVERIFY(!loaded.series.xAxis.unit.isEmpty());
    QVERIFY(!loaded.series.yAxis.unit.isEmpty());
}
void OdpExamplesAudit::workflowAndPersistence_data()
{
    QTest::addColumn<QString>("method");
#define ODP_TEST_ROW(id, type) QTest::newRow(#id) << QString(#id);
    ODP_EXAMPLE_METHODS(ODP_TEST_ROW)
#undef ODP_TEST_ROW
}
void OdpExamplesAudit::workflowAndPersistence()
{
    QFETCH(QString, method);
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot(method, {0, 0}).nodeId;
    QVERIFY(!root.isNull());
    const auto direct = model.addChild(root, ChildSlot::Right, "mtm").nodeId;
    const auto prep = model.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    const auto mtm = model.addChild(prep, ChildSlot::Down, "mtm", {{"ar1", false}, {"swa", false}});
    QVERIFY(mtm.ok());
    const auto plan = prepareRuns(model.freeze().snapshot, {});
    QVERIFY(plan.validation.ok());
    QCOMPARE(plan.requests.size(), 1);
    const auto run = WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(run.nodes[root].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[direct].state, NodeState::Failed);
    QVERIFY(run.nodes[direct].diagnostic().code.startsWith("INPUT_X_"));
    QCOMPARE(run.nodes[prep].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[mtm.nodeId].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[root].report["example_source"].toObject()["id"].toString(), method);
    QTemporaryDir dir;
    ResultRepository repo;
    QVERIFY(repo.open(dir.filePath("examples.sqlite")).isEmpty());
    const auto receipt = repo.save(run);
    QVERIFY2(receipt.saved(), qPrintable(receipt.error));
    const auto restored = repo.load(receipt.recordId);
    QVERIFY2(restored.ok(), qPrintable(restored.error));
    QCOMPARE(restored.run.nodes[root].report, run.nodes[root].report);
    QCOMPARE(pairDigest(*dataAs<SeriesData>(restored.run.nodes[root].outputs["series"])),
             pairDigest(*dataAs<SeriesData>(run.nodes[root].outputs["series"])));
}
void OdpExamplesAudit::cocoDepthWorkflow_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("method");
    QTest::addColumn<QString>("axis");
    QTest::addColumn<QString>("declaredUnit");
    QTest::addColumn<bool>("accepted");
    for (const auto *method : {"coco_scan", "coco_monte_carlo", "evolution_coco"})
    {
        for (const auto *source : {"odp1143_d18o", "odp846_d18o", "odp926_d13c"})
            QTest::newRow(qPrintable(QString("%1/%2/depth").arg(source, method)))
                << QString(source) << QString(method) << QString("depth") << QString("m") << true;
        QTest::newRow(qPrintable(QString("%1/reject-age").arg(method)))
            << QString("odp1143_d18o") << QString(method) << QString("age") << QString("m") << false;
        QTest::newRow(qPrintable(QString("%1/reject-wrong-scale").arg(method)))
            << QString("odp1143_d18o") << QString(method) << QString("depth") << QString("cm") << false;
    }
}
void OdpExamplesAudit::cocoDepthWorkflow()
{
    QFETCH(QString, source);
    QFETCH(QString, method);
    QFETCH(QString, axis);
    QFETCH(QString, declaredUnit);
    QFETCH(bool, accepted);
    const auto registry = makeScientificRegistry();
    WorkflowModel model(registry);
    const auto root = model.addRoot(source, {0, 0}, {{"example_axis", axis}}).nodeId;
    QVERIFY(!root.isNull());
    const auto prep = model.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    QVERIFY(!prep.isNull());
    QJsonObject parameters{{"chrono_depth_unit", declaredUnit}};
    // Exercise the reported ODP 1143 chain with the production defaults.
    // Other sources need only the minimum valid number of simulations here.
    if (method == "coco_monte_carlo" && source != "odp1143_d18o")
        parameters["coco_simulations"] = 19;
    const auto coco = model.addChild(prep, ChildSlot::Down, method, parameters).nodeId;
    QVERIFY(!coco.isNull());
    const auto plan = prepareRuns(model.freeze().snapshot, {});
    QVERIFY(plan.validation.ok());
    QCOMPARE(plan.requests.size(), 1);
    const auto run = WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(run.nodes[root].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[prep].state, NodeState::Succeeded);
    const auto original = examples::load(source, axis);
    const auto *input = dataAs<SeriesData>(run.nodes[prep].outputs["series"]);
    QVERIFY(input);
    QCOMPARE(input->xAxis.unit, original.series.xAxis.unit);
    QCOMPARE(input->xAxis.name, original.series.xAxis.name);
    QCOMPARE(run.nodes[root].report["example_source"].toObject()["selected_axis"].toString(), axis);
    QCOMPARE(pairDigest(*dataAs<SeriesData>(run.nodes[root].outputs["series"])),
             pairDigest(original.series));
    const auto result = run.nodes[coco];
    if (!accepted)
    {
        QCOMPARE(result.state, NodeState::Failed);
        QCOMPARE(result.diagnostic().code, QString("CHRONOLOGY_UNIT"));
        QCOMPARE(result.diagnostic().context["input_unit"].toString(), input->xAxis.unit);
        QCOMPARE(result.diagnostic().context["declared_unit"].toString(), declaredUnit);
        QVERIFY(result.outputs.isEmpty());
        return;
    }
    QVERIFY2(result.state == NodeState::Succeeded, qPrintable(result.error));
    QCOMPARE(run.state, RunState::Succeeded);

    // The same metre-valued coordinates must produce the same numerical result
    // as ordinary metres, while the original composite-depth metadata survives.
    auto metreInput = *input;
    metreInput.xAxis.unit = "m";
    const auto task = registry.create(method, parameters);
    QVERIFY(task);
    const auto metreResult = task->execute({makeData(metreInput)}, {});
    QVERIFY2(metreResult.state == NodeState::Succeeded, qPrintable(metreResult.error));
    if (method == "evolution_coco")
    {
        const auto *actual = dataAs<GridData>(result.outputs["correlation_map"]);
        const auto *expected = dataAs<GridData>(metreResult.outputs["correlation_map"]);
        QVERIFY(actual && expected);
        QVERIFY(!actual->values.isEmpty());
        QCOMPARE(actual->xAxis.unit, input->xAxis.unit);
        QCOMPARE(actual->xAxis.name, input->xAxis.name);
        QCOMPARE(actual->x, expected->x);
        QCOMPARE(actual->y, expected->y);
        QCOMPARE(actual->values, expected->values);
    }
    else
    {
        const QString output = method == "coco_scan" ? "correlation" : "scan_adjusted_p";
        const auto *actual = dataAs<SeriesData>(result.outputs[output]);
        const auto *expected = dataAs<SeriesData>(metreResult.outputs[output]);
        QVERIFY(actual && expected);
        QVERIFY(!actual->x.isEmpty());
        QCOMPARE(actual->x, expected->x);
        QCOMPARE(actual->y, expected->y);
    }
}
void OdpExamplesAudit::invalidAndCancellation()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, examples::load("bad", "age"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, examples::load("odp846_d18o", "bad"));
    QVERIFY(examples::load("odp846_d18o", "age", [] { return true; }).cancelled);
    int calls = 0;
    QVERIFY(examples::load("odp926_lightness", "age", [&] { return ++calls > 2; }).cancelled);
    const auto registry = makeScientificRegistry();
    QVERIFY(!registry.normalize("odp846_d18o", {{"example_axis", "bad"}}).validation.ok());
    ExampleSourceTask bad("bad", {});
    const auto failed = bad.execute({}, {});
    QCOMPARE(failed.state, NodeState::Failed);
    QCOMPARE(failed.diagnostic().code, QString("EXAMPLE_DATA"));
}
void OdpExamplesAudit::concurrentIndependentReads()
{
    auto a = std::async(std::launch::async, [] { return examples::load("odp846_d18o", "age"); });
    auto b =
        std::async(std::launch::async, [] { return examples::load("odp926_lightness", "depth"); });
    const auto age = a.get(), depth = b.get();
    QCOMPARE(age.series.xAxis.unit, QString("ka BP"));
    QCOMPARE(depth.series.xAxis.unit, QString("mcd"));
    QCOMPARE(age.series.x.size(), 697);
    QCOMPARE(depth.series.x.size(), 1981);
}
int runOdpExamplesAudit(int argc, char **argv)
{
    OdpExamplesAudit test;
    return QTest::qExec(&test, argc, argv);
}
#include "odp_examples_audit.moc"
