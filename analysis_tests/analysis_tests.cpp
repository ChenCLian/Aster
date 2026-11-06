// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QSqlError>
#include <future>
#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include "application/batch_runner.h"
#include "processing/scientific_registry.h"
#include "io/files/series_file_reader.h"
#include "io/database/data_codec.h"
#include "math/spectrum/mtm_spectrum.h"
#include "third_party/lowess/Lowess.h"
#include "third_party/wavelib/wavelib.h"
#include "spectral_audit.h"
#include "wavelet_audit.h"
#include "transfer_audit.h"
#include "mtm_simulation_audit.h"
#include "acycle_alignment_audit.h"
#include "backend_audit.h"
#include "orbital_tables_audit.h"
using namespace aster;
namespace
{
constexpr double tau = 6.2831853071795864769;
DataRef signal(int n = 256, double period = 16, double dt = 1)
{
    SeriesData data;
    data.xAxis = {"Time", "ka"};
    data.yAxis = {"Signal", "u"};
    for (int i = 0; i < n; ++i)
    {
        data.x.push_back(i * dt);
        data.y.push_back(std::sin(tau * i / period) + .4 * std::cos(tau * i / 39) + .001 * i);
    }
    return makeData(data);
}
NodeResult execute(const QString &key, DataRef input, QJsonObject parameters = {})
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0});
    const auto child = model.addChild(root.nodeId, ChildSlot::Down, key, parameters);
    if (!child.ok())
        return NodeResult::failure(child.error);
    return WorkflowExecutor()
        .run({model.freeze().snapshot, root.nodeId, {"test", input, {}}})
        .nodes.value(child.nodeId);
}
RunResult simpleRun(const QString &label = "test")
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0});
    model.addChild(root.nodeId, ChildSlot::Down, "series_summary");
    return WorkflowExecutor().run(
        {model.freeze().snapshot, root.nodeId, {label, signal(64), {{"file", "原始数据.csv"}}}});
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
RunPlan filePlan(const QVector<SourceItem> &files, bool generator = false)
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("file_source", {0, 0});
    model.addChild(root.nodeId, ChildSlot::Down, "series_summary");
    if (generator)
        model.addRoot("sine_source", {4, 0});
    return prepareRuns(model.freeze().snapshot, {{root.nodeId, files}});
}
QString sql(const QString &path, const QString &statement)
{
    const auto name = QUuid::createUuid().toString();
    QString error;
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
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return error;
}
} // namespace
class AnalysisTests : public QObject
{
    Q_OBJECT
  private slots:
    void structuredAlgorithmDiagnostics_data();
    void structuredAlgorithmDiagnostics();
    void directValidInputNeedsNoPreprocessingNode();
    void fileProbeAndFrozenOptions();
    void frequencySearchAndMetadata();
    void databaseV1Migration();
    void databaseMigrationRollback();
    void batchCollectsAnyCompletedInput();
    void scientificCatalogue();
    void quotedUtf8File();
    void missingValuePolicies();
    void readerBoundsAndCancellation();
    void deferredFileSource();
    void preprocessSortDuplicatesAndResample();
    void preprocessingErrors();
    void lowessMatchesReference();
    void lowessParameterIsolation();
    void foundationReferences_data();
    void foundationReferences();
    void lowessMeanAndRobustness();
    void mtmMatchesScipy();
    void mtmPeakAndIntegral();
    void mtmShortConstantAndCutoff();
    void mtmAbsoluteFrequencyLimit();
    void mtmTinyCutoffAndWorkspace();
    void rejectIrregularAxis();
    void largeOriginDoesNotHideIrregularSampling();
    void mtmIndependentNodes();
    void waveletWrapperMatchesLibrary();
    void waveletPeakAndConstant();
    void waveletParameterValidation();
    void dataCodecAllTypes();
    void dataCodecRejectsCorruption();
    void repositoryRoundTrip();
    void repositoryAtomicRollback();
    void repositoryRejectsOldSchema();
    void repositoryThreadGuard();
    void batchFilesAndGenerator();
    void batchFailureIsolation();
    void batchSaveFailureAndRetry();
    void lateSaveFailureRollsBackAndRetries();
    void batchCancellationAndObserver();
    void fullScientificRoundTrip();
};
void AnalysisTests::scientificCatalogue()
{
    const auto r = makeScientificRegistry();
    QCOMPARE(r.descriptors().size(), 53);
    QVERIFY(r.find("synthetic_orbital"));
    QVERIFY(r.find("file_source")->descriptor.requiresSourceBinding);
    QVERIFY(!r.find("file_source")->descriptor.boundDataKind);
    QCOMPARE(r.find("input_series")->descriptor.boundDataKind.value(), DataKind::Series);
    QCOMPARE(r.find("cwt")->descriptor.defaultOutput, QString("power"));
    QCOMPARE(r.find("detrend_lowess")->descriptor.version, 4);
    QCOMPARE(r.find("preprocess")->descriptor.version, 4);
    QVERIFY(r.normalize("mtm", {{"nw", 2.5}}).validation.ok());
    QVERIFY(!r.normalize("preprocess", {{"interpolation", "typo"}}).validation.ok());
}
void AnalysisTests::largeOriginDoesNotHideIrregularSampling()
{
    auto data = *dataAs<SeriesData>(signal(128));
    // Exactly representable grid at a large origin remains valid.
    const double step = 1. / 1024;
    for (qsizetype i = 0; i < data.x.size(); ++i)
        data.x[i] = 1e12 + i * step;
    for (const auto &method : {QString("mtm"), QString("cwt")})
    {
        const auto result = execute(method, makeData(data));
        QCOMPARE(result.state, NodeState::Succeeded);
    }
    data.x[31] += step / 4; // 25% interval error, previously below 16*eps*|X|.
    const auto input = makeData(data);
    for (const auto &method : {QString("mtm"), QString("cwt"), QString("ar1_global")})
    {
        const auto result = execute(method, input);
        QCOMPARE(result.state, NodeState::Failed);
        QCOMPARE(result.diagnostic().code, QString("INPUT_X_NONUNIFORM"));
        QCOMPARE(result.diagnostic().context["point"].toInt(), 32);
        QVERIFY(result.outputs.isEmpty());
    }
    QCOMPARE(dataAs<SeriesData>(input)->x, data.x);
}
void AnalysisTests::quotedUtf8File()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("数据.csv");
    QVERIFY(writeFile(path, QByteArray::fromHex("efbbbf") +
                                "# comment\n\"Time, ka\",\"Signal\"\n0,1.25\n1,2.5\n2,3.75\n"));
    const auto r = readSeriesFile(path, {});
    QVERIFY2(r.ok(), qPrintable(r.error));
    const auto *d = dataAs<SeriesData>(r.data);
    QVERIFY(d);
    QCOMPARE(d->xAxis.name, QString("Time, ka"));
    QCOMPARE(d->y, QVector<double>({1.25, 2.5, 3.75}));
}
void AnalysisTests::missingValuePolicies()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("bad.csv");
    QVERIFY(writeFile(path, "x,y\n0,1\n1,NaN\n2,\n3,4\n"));
    QVERIFY(!readSeriesFile(path, {}).ok());
    SeriesFileOptions p;
    p.skipInvalidRows = true;
    const auto r = readSeriesFile(path, p);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(dataAs<SeriesData>(r.data)->x, QVector<double>({0, 3}));
    QCOMPARE(r.report["skipped_rows"].toInt(), 2);
    QVERIFY(!r.warnings.isEmpty());
}
void AnalysisTests::readerBoundsAndCancellation()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("whitespace.txt");
    QVERIFY(writeFile(path, "0 9 1\n1 8 2\n2 7 3\n"));
    SeriesFileOptions p;
    p.header = false;
    p.yColumn = 2;
    const auto r = readSeriesFile(path, p);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(dataAs<SeriesData>(r.data)->y[2], 3.);
    p.maxRows = 2;
    QVERIFY(!readSeriesFile(path, p).ok());
    p.maxRows = 100;
    p.yColumn = 7;
    QVERIFY(!readSeriesFile(path, p).ok());
    CancellationToken cancel;
    cancel.cancel();
    QVERIFY(!readSeriesFile(path, {}, cancel).ok());
}
void AnalysisTests::deferredFileSource()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("input.csv");
    QVERIFY(writeFile(path, "x,y\n0,1\n1,3\n2,2\n"));
    auto plan = filePlan({{"sample", {}, {{"path", path}}}});
    QVERIFY(plan.validation.ok());
    auto r = WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(r.state, RunState::Succeeded);
    QCOMPARE(r.nodes.size(), 2);
    QVERIFY(dataAs<SeriesData>(r.nodes[r.root].outputs["series"]));
    plan.requests[0].source.provenance = {};
    QCOMPARE(WorkflowExecutor().run(plan.requests[0]).state, RunState::Failed);
}
void AnalysisTests::preprocessSortDuplicatesAndResample()
{
    auto input = makeData(SeriesData{{2, 0, 1, 1}, {4, 0, 1, 3}, {}, {}});
    auto r = execute("preprocess", input,
                     {{"spacing_mode", "fixed"}, {"spacing", .5}, {"interpolation", "linear"}});
    QCOMPARE(r.state, NodeState::Succeeded);
    const auto *d = dataAs<SeriesData>(r.outputs["series"]);
    QVERIFY(d);
    QCOMPARE(d->x, QVector<double>({0, .5, 1, 1.5, 2}));
    QCOMPARE(d->y, QVector<double>({0, 1, 2, 3, 4}));
    QCOMPARE(dataAs<SeriesData>(input)->x, QVector<double>({2, 0, 1, 1}));
    QCOMPARE(r.report["merged_rows"].toInt(), 1);
    auto endpoint =
        execute("preprocess", makeData(SeriesData{{0, .1, .2, .3}, {0, 1, 2, 3}, {}, {}}),
                {{"interpolation", "linear"}});
    QCOMPARE(dataAs<SeriesData>(endpoint.outputs["series"])->x.size(), 4);
}
void AnalysisTests::preprocessingErrors()
{
    auto repeated = makeData(SeriesData{{0, 0, 1}, {1, 2, 3}, {}, {}});
    QCOMPARE(execute("preprocess", repeated, {{"duplicate_policy", "error"}}).state,
             NodeState::Failed);
    QCOMPARE(execute("preprocess", signal(3), {{"interpolation", "akima"}}).state,
             NodeState::Failed);
    QCOMPARE(execute("preprocess", signal(3), {{"spacing_mode", "fixed"}, {"spacing", 100.}}).state,
             NodeState::Failed);
}
void AnalysisTests::lowessMatchesReference()
{
    const auto input = signal(128);
    const auto *d = dataAs<SeriesData>(input);
    std::vector<double> x(d->x.begin(), d->x.end()), y(d->y.begin(), d->y.end()), fitted(x.size()),
        robust(x.size()), residual(x.size());
    CppLowess::TemplatedLowess<std::vector<double>, double> ref;
    QCOMPARE(ref.lowess(x, y, .3, 3, 0., fitted, robust, residual), 0);
    const auto r = execute("detrend_lowess", input, {{"fraction", .3}, {"iterations", 3}});
    QCOMPARE(r.state, NodeState::Succeeded);
    for (int i = 0; i < 128; ++i)
    {
        QVERIFY(std::abs(dataAs<SeriesData>(r.outputs["trend"])->y[i] - fitted[i]) < 1e-10);
        QVERIFY(std::abs(dataAs<SeriesData>(r.outputs["detrended"])->y[i] - residual[i]) < 1e-10);
    }
}
void AnalysisTests::lowessParameterIsolation()
{
    auto input = signal();
    const auto a = execute("detrend_lowess", input, {{"fraction", .1}});
    const auto b = execute("detrend_lowess", input, {{"fraction", .7}});
    QVERIFY(dataAs<SeriesData>(a.outputs["trend"])->y != dataAs<SeriesData>(b.outputs["trend"])->y);
    QCOMPARE(dataAs<SeriesData>(input)->y, dataAs<SeriesData>(signal())->y);
}
void AnalysisTests::foundationReferences_data()
{
    QTest::addColumn<QJsonObject>("testCase");
    QFile file(QFINDTESTDATA("fixtures/foundation_reference.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isArray());
    for (const auto &value : document.array())
    {
        const auto item = value.toObject();
        QTest::newRow(qPrintable(item["name"].toString())) << item;
    }
    QFile external(QFINDTESTDATA("fixtures/lowess_statsmodels_reference.json"));
    QVERIFY(external.open(QIODevice::ReadOnly));
    const auto independent = QJsonDocument::fromJson(external.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    for (const auto &value : independent.object()["cases"].toArray()) {
        const auto item = value.toObject();
        QTest::newRow(qPrintable(item["name"].toString())) << item;
    }
}
void AnalysisTests::foundationReferences()
{
    QFETCH(QJsonObject, testCase);
    SeriesData source;
    source.xAxis = {"Age", "ka"};
    source.yAxis = {"Signal", "u"};
    for (const auto &value : testCase["x"].toArray()) source.x.append(value.toDouble());
    for (const auto &value : testCase["y"].toArray()) source.y.append(value.toDouble());
    const auto key = testCase["method"].toString();
    const auto parameters = testCase["parameters"].toObject();
    const auto input = makeData(source);
    NodeResult result;
    if (key == "sine_source")
    {
        WorkflowModel model(makeScientificRegistry());
        const auto root = model.addRoot(key, {0, 0}, parameters);
        QVERIFY2(root.ok(), qPrintable(root.error));
        result = WorkflowExecutor().run({model.freeze().snapshot, root.nodeId, {}})
                     .nodes.value(root.nodeId);
    }
    else
        result = execute(key, input, parameters);
    const auto reference = testCase["reference"].toObject();
    QCOMPARE(nodeStateName(result.state), reference["state"].toString());
    if (result.state == NodeState::Failed)
    {
        QVERIFY(result.outputs.isEmpty());
        if (testCase["name"] == "unrepresentable_grid")
            QCOMPARE(result.diagnostic().code, errorCodeName(ErrorCode::ResampleResolution));
        return;
    }
    const auto close = [&](double actual, double expected) {
        return std::isfinite(actual) && std::abs(actual - expected) <=
            testCase["absolute_tolerance"].toDouble(2e-8) +
            testCase["relative_tolerance"].toDouble(1e-9) * std::abs(expected);
    };
    const auto compareVector = [&](const QVector<double> &actual, const QJsonArray &expected) {
        QCOMPARE(actual.size(), expected.size());
        for (qsizetype i = 0; i < actual.size(); ++i)
            QVERIFY2(close(actual[i], expected[i].toDouble()),
                     qPrintable(QString("point %1: actual %2, reference %3")
                                    .arg(i).arg(actual[i], 0, 'g', 17)
                                    .arg(expected[i].toDouble(), 0, 'g', 17)));
    };
    const auto expectedPorts = reference["expected"].toObject();
    for (auto it = expectedPorts.begin(); it != expectedPorts.end(); ++it)
    {
        const auto expected = it.value().toObject();
        QVERIFY(result.outputs.contains(it.key()));
        if (expected.contains("rows"))
        {
            const auto *table = dataAs<TableData>(result.outputs[it.key()]);
            QVERIFY(table);
            const auto rows = expected["rows"].toArray();
            QCOMPARE(table->rows.size(), rows.size());
            QCOMPARE(table->columns, QStringList({"count", "mean", "population_variance", "minimum", "maximum"}));
            for (qsizetype i = 0; i < rows.size(); ++i)
                compareVector(table->rows[i], rows[i].toArray());
        }
        else
        {
            const auto *series = dataAs<SeriesData>(result.outputs[it.key()]);
            QVERIFY(series);
            if (expected.contains("x")) compareVector(series->x, expected["x"].toArray());
            if (expected.contains("y")) compareVector(series->y, expected["y"].toArray());
            if (expected.contains("mean"))
            {
                long double mean = 0;
                for (double y : series->y) mean += static_cast<long double>(y) / series->y.size();
                QVERIFY(close(static_cast<double>(mean), expected["mean"].toDouble()));
            }
        }
    }
    for (const auto &output : result.outputs)
        if (const auto *series = dataAs<SeriesData>(output); series && key != "sine_source")
        {
            QCOMPARE(series->xAxis.unit, source.xAxis.unit);
            QCOMPARE(series->yAxis.unit, source.yAxis.unit);
            if (key == "preprocess")
                for (qsizetype i = 1; i < series->x.size(); ++i)
                    QVERIFY(series->x[i] > series->x[i - 1]);
        }
    QCOMPARE(dataAs<SeriesData>(input)->x, source.x);
    QCOMPARE(dataAs<SeriesData>(input)->y, source.y);
    if (testCase["name"] == "lowess_tiny_window")
    {
        QCOMPARE(result.report["neighbor_count"].toInt(), 2);
        compareVector(dataAs<SeriesData>(result.outputs["detrended"])->y, QJsonArray{0, 0, 0, 0});
    }
}
void AnalysisTests::lowessMeanAndRobustness()
{
    SeriesData source;
    double x = 0;
    for (int i = 0; i < 61; ++i)
    {
        x += .7 + .5 * (1 + std::sin(i * .31));
        source.x.append(x);
        source.y.append(2 + .03 * x + std::sin(x * .4));
    }
    const double cleanValue = source.y[27];
    source.y[27] += 40;
    const auto input = makeData(source);
    const auto robust = execute("detrend_lowess", input, {{"iterations", 3}});
    const auto ordinary = execute("detrend_lowess", input, {{"iterations", 0}});
    const auto retained = execute("detrend_lowess", input, {{"retain_mean", true}, {"iterations", 3}});
    QCOMPARE(robust.state, NodeState::Succeeded);
    QCOMPARE(ordinary.state, NodeState::Succeeded);
    QCOMPARE(retained.state, NodeState::Succeeded);
    const auto *trend = dataAs<SeriesData>(robust.outputs["trend"]);
    const auto *residual = dataAs<SeriesData>(robust.outputs["detrended"]);
    const auto *adjusted = dataAs<SeriesData>(retained.outputs["detrended"]);
    QVERIFY(trend && residual && adjusted);
    QCOMPARE(dataAs<SeriesData>(retained.outputs["trend"])->y, trend->y);
    QCOMPARE(robust.report["variant"].toString(), QString("robust_lowess"));
    QCOMPARE(ordinary.report["variant"].toString(), QString("ordinary_lowess"));
    const auto *ordinaryTrend = dataAs<SeriesData>(ordinary.outputs["trend"]);
    QVERIFY(ordinaryTrend);
    QVERIFY(std::abs(trend->y[27] - cleanValue) < std::abs(ordinaryTrend->y[27] - cleanValue));
    long double inputMean = 0, outputMean = 0;
    for (qsizetype i = 0; i < source.y.size(); ++i)
    {
        inputMean += static_cast<long double>(source.y[i]) / source.y.size();
        outputMean += static_cast<long double>(adjusted->y[i]) / source.y.size();
        QVERIFY(std::abs(residual->y[i] + trend->y[i] - source.y[i]) < 1e-12);
        QVERIFY(std::abs(adjusted->y[i] + trend->y[i] -
                         retained.report["mean_adjustment"].toDouble() - source.y[i]) < 1e-12);
    }
    QVERIFY(std::abs(inputMean - outputMean) < 1e-12);
    QCOMPARE(dataAs<SeriesData>(input)->y, source.y);
}
void AnalysisTests::mtmMatchesScipy()
{
    QFile f(QFINDTESTDATA("fixtures/mtm_scipy.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const auto json = QJsonDocument::fromJson(f.readAll()).object();
    std::vector<double> y;
    for (const auto &v : json["input"].toArray())
        y.push_back(v.toDouble());
    const auto r = numeric::mtmSpectrum(y, 1, 3, 5, .5);
    const auto expected = json["power"].toArray();
    QCOMPARE(r.power.size(), size_t(expected.size()));
    double error = 0, scale = 0;
    for (int i = 0; i < expected.size(); ++i)
    {
        error = std::max(error, std::abs(r.power[i] - expected[i].toDouble()));
        scale = std::max(scale, expected[i].toDouble());
    }
    QVERIFY2(error / scale < 1e-8,
             qPrintable(QString("SciPy relative max error %1").arg(error / scale, 0, 'g', 16)));
}
void AnalysisTests::mtmPeakAndIntegral()
{
    const auto r = execute("mtm", signal(512), {{"swa", false}, {"ar1", false}});
    QCOMPARE(r.state, NodeState::Succeeded);
    const auto *d = dataAs<SpectrumData>(r.outputs["spectrum"]);
    QVERIFY(d);
    const auto i = std::max_element(d->power.begin(), d->power.end()) - d->power.begin();
    QVERIFY(std::abs(d->frequency[i] - 1. / 16) < .008);
    double integrated = 0;
    for (auto p : d->power)
        integrated += p * (d->frequency[1] - d->frequency[0]);
    QVERIFY(integrated > .45 && integrated < .8);
    QVERIFY(!dataAs<TableData>(r.outputs["peaks"])->rows.isEmpty());
    QVERIFY(!r.report["ar1_available"].toBool());
}
void AnalysisTests::mtmShortConstantAndCutoff()
{
    const auto shortRun = execute("mtm", signal(8), {{"nw", 1}});
    QCOMPARE(shortRun.state, NodeState::Succeeded);
    QVERIFY(!shortRun.outputs.contains("swa_background"));
    QVERIFY(shortRun.warnings.join(" ").contains("33 frequency bins"));
    SeriesData constant = *dataAs<SeriesData>(signal(32));
    constant.y.fill(5.);
    const auto zero = execute("mtm", makeData(constant), {{"ar1", true}});
    QCOMPARE(zero.state, NodeState::Succeeded);
    QVERIFY(!zero.outputs.contains("ar1_background"));
    for (auto p : dataAs<SpectrumData>(zero.outputs["spectrum"])->power)
        QCOMPARE(p, 0.);
    const auto cut = execute("mtm", signal(100), {{"fmax_ratio", .317}, {"swa", false}});
    QCOMPARE(cut.state, NodeState::Succeeded);
    QVERIFY(dataAs<SpectrumData>(cut.outputs["spectrum"])->frequency.back() <= .317 / 2);
}
void AnalysisTests::mtmAbsoluteFrequencyLimit()
{
    WorkflowModel original(makeScientificRegistry());
    const auto root = original.addRoot("input_series", {0, 0}).nodeId;
    const auto child = original.addChild(root, ChildSlot::Down, "mtm", {{"fmax_ratio", .32}}).nodeId;
    auto oldDefinition = original.definition();
    for (auto &node : oldDefinition.nodes)
        if (node.id == child) { node.parameters.remove("frequency_max"); node.methodVersion = 7; }
    WorkflowModel restored(makeScientificRegistry());
    const auto restoredEdit = restored.replaceDefinition(oldDefinition);
    QVERIFY2(restoredEdit.ok(), qPrintable(restoredEdit.error));
    const auto input = signal(256, 16, 2); // Nyquist = .25 1/ka.
    const auto full = execute("mtm", input);
    const auto cut = execute("mtm", input, {{"frequency_max", .08}});
    const auto legacy = execute("mtm", input, {{"fmax_ratio", .32}});
    const auto capped = execute("mtm", input, {{"frequency_max", 25}});
    const auto explicitLimit = execute("mtm", input, {{"frequency_max", .08}, {"fmax_ratio", .1}});
    for (const auto *run : {&full, &cut, &legacy, &capped, &explicitLimit})
        QCOMPARE(run->state, NodeState::Succeeded);
    QCOMPARE(cut.report["frequency_limit_mode"].toString(), QString("absolute"));
    QCOMPARE(legacy.report["frequency_limit_mode"].toString(), QString("legacy_ratio"));
    QCOMPARE(cut.report["analysis_frequency_max"], full.report["analysis_frequency_max"]);
    QCOMPARE(cut.report["ar1_rho"], full.report["ar1_rho"]);
    for (auto it = cut.outputs.begin(); it != cut.outputs.end(); ++it)
    {
        const auto *part = dataAs<SpectrumData>(it.value());
        if (!part) continue;
        const auto *all = dataAs<SpectrumData>(full.outputs[it.key()]);
        const auto *old = dataAs<SpectrumData>(legacy.outputs[it.key()]);
        const auto *explicitPart = dataAs<SpectrumData>(explicitLimit.outputs[it.key()]);
        const auto *cappedPart = dataAs<SpectrumData>(capped.outputs[it.key()]);
        QVERIFY(all && old && explicitPart && cappedPart);
        QVERIFY(part->frequency.back() <= .08);
        QCOMPARE(part->frequency, old->frequency);
        QCOMPARE(part->power, old->power);
        QCOMPARE(part->power, all->power.mid(0, part->power.size()));
        QCOMPARE(part->power, explicitPart->power);
        QCOMPARE(cappedPart->power, all->power);
    }
    const auto *peaks = dataAs<TableData>(cut.outputs["peaks"]);
    QVERIFY(peaks);
    for (const auto &row : peaks->rows) QVERIFY(row[0] <= .08);
    QCOMPARE(execute("mtm", input, {{"frequency_max", -1}}).state, NodeState::Failed);
    const auto tiny = execute("mtm", input, {{"frequency_max", 1e-10}});
    QCOMPARE(tiny.state, NodeState::Succeeded);
    QCOMPARE(dataAs<SpectrumData>(tiny.outputs["spectrum"])->frequency.size(), 1);
}
void AnalysisTests::rejectIrregularAxis()
{
    auto data = *dataAs<SeriesData>(signal());
    data.x[80] += .1;
    auto input = makeData(data);
    QCOMPARE(execute("mtm", input).state, NodeState::Failed);
    QCOMPARE(execute("cwt", input).state, NodeState::Failed);
    std::reverse(data.x.begin(), data.x.end());
    QCOMPARE(execute("detrend_lowess", makeData(data)).state, NodeState::Failed);
}
void AnalysisTests::mtmTinyCutoffAndWorkspace()
{
    const auto tiny = execute("mtm", signal(8), {{"nw", 1}, {"fmax_ratio", .001}, {"ar1", true}});
    QCOMPARE(tiny.state, NodeState::Succeeded);
    QVERIFY(tiny.outputs.contains("ar1_background"));
    QCOMPARE(dataAs<SpectrumData>(tiny.outputs["ar1_background"])->power.size(), 1);
    QVERIFY(!tiny.warnings.isEmpty());
    std::vector<double> big(1000000, 1.);
    QVERIFY_EXCEPTION_THROWN(numeric::mtmSpectrum(big, 1, 3, 5, .5), std::invalid_argument);
}
void AnalysisTests::mtmIndependentNodes()
{
    WorkflowModel m(makeScientificRegistry());
    auto root = m.addRoot("input_series", {0, 0}).nodeId;
    auto a = m.addChild(root, ChildSlot::Left, "mtm", {{"nw", 2}, {"swa", false}}).nodeId;
    auto b = m.addChild(root, ChildSlot::Right, "mtm", {{"nw", 4}, {"swa", false}}).nodeId;
    auto run = WorkflowExecutor().run({m.freeze().snapshot, root, {"same input", signal(), {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(run.nodes[a].report["tapers"].toInt(), 3);
    QCOMPARE(run.nodes[b].report["tapers"].toInt(), 7);
    QVERIFY(dataAs<SpectrumData>(run.nodes[a].outputs["spectrum"])->power !=
            dataAs<SpectrumData>(run.nodes[b].outputs["spectrum"])->power);
}
void AnalysisTests::waveletWrapperMatchesLibrary()
{
    const auto input = signal(128);
    auto values = dataAs<SeriesData>(input)->y;
    const int scales = 1 + int(std::log2(64.) / .15);
    auto raw = cwt_init("morlet", 6, 128, 1, scales);
    setCWTScales(raw, 2, .15, "pow", 2);
    setCWTPadding(raw, 1);
    cwt(raw, values.data());
    const auto r = execute("cwt", input, {{"significance", false}, {"method", "torrence_compo"}, {"dj", .15}});
    QCOMPARE(r.state, NodeState::Succeeded);
    const auto *grid = dataAs<GridData>(r.outputs["power"]);
    QVERIFY(grid);
    QCOMPARE(grid->rows, scales);
    QCOMPARE(grid->columns, 128);
    for (int j = 0; j < scales; ++j)
    {
        QCOMPARE(grid->y[j], raw->period[j]);
        for (int i = 0; i < 128; ++i)
        {
            auto v = raw->output[j * 128 + i];
            const double expected = v.re * v.re + v.im * v.im;
            QVERIFY(std::abs(grid->values[j * 128 + i] - expected) <= 2e-11 * std::max(1., expected));
        }
    }
    QCOMPARE(dataAs<SeriesData>(r.outputs["coi"])->y.front(), raw->coi[0]);
    cwt_free(raw);
}
void AnalysisTests::waveletPeakAndConstant()
{
    const auto r = execute("cwt", signal(256));
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(r.outputs.contains("significance_ratio"));
    const auto *grid = dataAs<GridData>(r.outputs["power"]);
    int row = 0;
    double peak = -1;
    for (int j = 0; j < grid->rows; ++j)
        if (grid->values[j * grid->columns + 128] > peak)
        {
            peak = grid->values[j * grid->columns + 128];
            row = j;
        }
    QVERIFY(grid->y[row] > 13 && grid->y[row] < 20);
    auto data = *dataAs<SeriesData>(signal(32));
    data.y.fill(1.);
    auto constant = execute("cwt", makeData(data), {{"method", "torrence_compo"}});
    QCOMPARE(constant.state, NodeState::Succeeded);
    QVERIFY(!constant.outputs.contains("significance_ratio"));
    QVERIFY(!constant.warnings.isEmpty());
}
void AnalysisTests::waveletParameterValidation()
{
    QCOMPARE(execute("cwt", signal(), {{"mother", "dog"}, {"parameter", 3}}).state,
             NodeState::Failed);
    QCOMPARE(execute("cwt", signal(), {{"mother", "paul"}, {"parameter", 2.5}}).state,
             NodeState::Failed);
    QCOMPARE(execute("cwt", signal(64), {{"mother", "paul"}, {"parameter", 4}}).state,
             NodeState::Succeeded);
    QCOMPARE(execute("cwt", signal(64), {{"mother", "dog"}, {"parameter", 2}}).state,
             NodeState::Succeeded);
}
void AnalysisTests::dataCodecAllTypes()
{
    QVector<DataRef> values{
        signal(16), makeData(SpectrumData{{0, .1}, {2, 3}, {"频率", "1/ka"}, {"功率", "u²"}}),
        makeData(GridData{{0, 1, 2},
                          {3, 4},
                          {1, 2, 3, 4, 5, 6},
                          2,
                          3,
                          {"时间", "ka"},
                          {"周期", "ka"},
                          {"功率", "u²"}}),
        makeData(TableData{{"x", "y"}, {{1, 2}, {3, 4}}}), makeData(TableData{{"empty"}, {}})};
    for (const auto &data : values)
    {
        auto e = encodeData(data);
        QVERIFY2(e.ok(), qPrintable(e.error));
        auto d = decodeData(e.bytes);
        QVERIFY2(d.ok(), qPrintable(d.error));
        QCOMPARE(encodeData(d.data).bytes, e.bytes);
    }
}
void AnalysisTests::dataCodecRejectsCorruption()
{
    const auto e = encodeData(signal(8));
    QVERIFY(e.ok());
    QVERIFY(!decodeData(e.bytes.left(e.bytes.size() - 3)).ok());
    QVERIFY(!decodeData(e.bytes + "extra").ok());
    auto bad = e.bytes;
    bad[0] = '?';
    QVERIFY(!decodeData(bad).ok());
    bad = e.bytes;
    bad[4] = 127;
    QVERIFY(!decodeData(bad).ok());
    QVERIFY(!encodeData({}).ok());
}
void AnalysisTests::repositoryRoundTrip()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("results.sqlite");
    const auto original = simpleRun(QString());
    qint64 id = 0;
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        auto saved = repo.save(original);
        QVERIFY2(saved.saved(), qPrintable(saved.error));
        id = saved.recordId;
    }
    ResultRepository repo;
    QVERIFY(repo.open(path).isEmpty());
    auto loaded = repo.load(id);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.run.runId, original.runId);
    QCOMPARE(loaded.run.provenance, original.provenance);
    QCOMPARE(loaded.run.workflowSnapshot, original.workflowSnapshot);
    QCOMPARE(loaded.run.executionOrder, original.executionOrder);
    QCOMPARE(loaded.run.nodes.size(), original.nodes.size());
    for (auto it = original.nodes.begin(); it != original.nodes.end(); ++it)
        for (auto out = it->outputs.begin(); out != it->outputs.end(); ++out)
            QCOMPARE(encodeData(loaded.run.nodes[it.key()].outputs[out.key()]).bytes,
                     encodeData(out.value()).bytes);
    QCOMPARE(repo.list().records.size(), 1);
    QVERIFY(!repo.load(id + 100).ok());
    QVERIFY(!repo.save(original).saved());
    QCOMPARE(repo.list().records.size(), 1);
}
void AnalysisTests::repositoryAtomicRollback()
{
    ResultRepository repo;
    QVERIFY(repo.open(":memory:").isEmpty());
    auto invalid = simpleRun();
    invalid.nodes[invalid.root].outputs["series"] = {};
    const auto receipt = repo.save(invalid);
    QVERIFY(!receipt.saved());
    QCOMPARE(receipt.recordId, 0);
    QCOMPARE(repo.list().records.size(), 0);
    auto valid = simpleRun("quote ' 中文");
    QVERIFY(repo.save(valid).saved());
    QCOMPARE(repo.list().records.size(), 1);
}
void AnalysisTests::repositoryRejectsOldSchema()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("old.sqlite");
    QVERIFY(sql(path, "CREATE TABLE old_results(id INTEGER)").isEmpty());
    ResultRepository repo;
    QVERIFY(!repo.open(path).isEmpty());
    QVERIFY(!repo.save(simpleRun()).saved());
}
void AnalysisTests::repositoryThreadGuard()
{
    ResultRepository repo;
    QVERIFY(repo.open(":memory:").isEmpty());
    auto f = std::async(std::launch::async, [&] { return repo.list().error; });
    QVERIFY(!f.get().isEmpty());
}
void AnalysisTests::batchFilesAndGenerator()
{
    QTemporaryDir dir;
    auto a = dir.filePath("a.csv"), b = dir.filePath("b.csv");
    QVERIFY(writeFile(a, "x,y\n0,1\n1,2\n2,3\n"));
    QVERIFY(writeFile(b, "x,y\n0,4\n1,5\n2,6\n"));
    auto plan = filePlan({{"a", {}, {{"path", a}}}, {"b", {}, {{"path", b}}}}, true);
    QCOMPARE(plan.requests.size(), 3);
    auto batch = BatchRunner().run(plan, dir.filePath("results.sqlite"), 2);
    QVERIFY2(batch.error.isEmpty(), qPrintable(batch.error));
    QCOMPARE(batch.items.size(), 3);
    QSet<RunId> ids;
    for (const auto &item : batch.items)
    {
        QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
        QCOMPARE(item.computation, RunState::Succeeded);
        ids.insert(item.runId);
    }
    QCOMPARE(ids.size(), 3);
}
void AnalysisTests::batchFailureIsolation()
{
    QTemporaryDir dir;
    auto a = dir.filePath("a.csv");
    QVERIFY(writeFile(a, "x,y\n0,1\n1,2\n2,3\n"));
    auto batch = BatchRunner().run(
        filePlan({{"bad", {}, {{"path", dir.filePath("absent")}}}, {"good", {}, {{"path", a}}}}),
        dir.filePath("results.sqlite"), 2);
    QCOMPARE(batch.items.size(), 2);
    QCOMPARE(batch.items[0].computation, RunState::Failed);
    QCOMPARE(batch.items[1].computation, RunState::Succeeded);
    for (const auto &item : batch.items)
        QVERIFY2(item.persistence.saved(), qPrintable(item.persistence.error));
}
void AnalysisTests::batchSaveFailureAndRetry()
{
    QTemporaryDir dir;
    const auto db = dir.filePath("results.sqlite");
    {
        ResultRepository r;
        QVERIFY(r.open(db).isEmpty());
    }
    QVERIFY(sql(db,
                "CREATE TRIGGER reject_one BEFORE INSERT ON aster_runs WHEN "
                "NEW.source_label='blocked' BEGIN SELECT RAISE(ABORT,'test write failure'); END")
                .isEmpty());
    auto path = dir.filePath("a.csv");
    QVERIFY(writeFile(path, "x,y\n0,1\n1,2\n2,3\n"));
    auto batch = BatchRunner().run(
        filePlan({{"blocked", {}, {{"path", path}}}, {"good", {}, {{"path", path}}}}), db, 2);
    QCOMPARE(batch.items.size(), 2);
    QCOMPARE(batch.items[0].computation, RunState::Succeeded);
    QVERIFY(!batch.items[0].persistence.saved());
    QVERIFY(batch.items[0].unsavedResult);
    QVERIFY(batch.items[1].persistence.saved());
    QVERIFY(sql(db, "DROP TRIGGER reject_one").isEmpty());
    ResultRepository r;
    QVERIFY(r.open(db).isEmpty());
    QVERIFY(r.save(*batch.items[0].unsavedResult).saved());
    QCOMPARE(r.list().records.size(), 2);
}
void AnalysisTests::lateSaveFailureRollsBackAndRetries()
{
    QTemporaryDir dir;
    const auto db = dir.filePath("late-failure.sqlite");
    { ResultRepository repo; QVERIFY(repo.open(db).isEmpty()); }
    // Fail only after the run and node rows have already been inserted.
    QVERIFY(sql(db, "CREATE TRIGGER reject_output BEFORE INSERT ON aster_outputs "
        "WHEN (SELECT source_label FROM aster_runs WHERE id=NEW.run_id)='blocked' "
        "BEGIN SELECT RAISE(ABORT,'late write failure'); END").isEmpty());
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series",{0,0}).nodeId;
    QVERIFY(model.addChild(root,ChildSlot::Down,"affine").ok());
    const auto plan = prepareRuns(model.freeze().snapshot,
        {{root, {{"blocked",signal(),{}},{"good",signal(),{}}}}});
    const auto batch = BatchRunner().run(plan,db,2);
    QVERIFY(batch.error.isEmpty()); QCOMPARE(batch.items.size(),2);
    QVERIFY(batch.items[0].unsavedResult);
    QVERIFY(!batch.items[0].persistence.saved());
    QCOMPARE(batch.items[0].computation,RunState::Succeeded);
    QVERIFY(batch.items[1].persistence.saved());
    ResultRepository repo; QVERIFY(repo.open(db).isEmpty());
    QCOMPARE(repo.list().records.size(),1);
    QVERIFY(sql(db,"DROP TRIGGER reject_output").isEmpty());
    // Retry the same RunId: a partially committed run would violate its unique key.
    const auto saved = repo.save(*batch.items[0].unsavedResult);
    QVERIFY2(saved.saved(),qPrintable(saved.error));
    const auto loaded = repo.load(saved.recordId); QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.nodes.size(),2);
    QCOMPARE(repo.list().records.size(),2);
}
void AnalysisTests::batchCancellationAndObserver()
{
    QTemporaryDir dir;
    WorkflowModel m(makeScientificRegistry());
    m.addRoot("sine_source", {0, 0});
    auto plan = prepareRuns(m.freeze().snapshot, {});
    CancellationToken cancel;
    cancel.cancel();
    const auto batch = BatchRunner().run(
        plan, dir.filePath("cancel.sqlite"), 2, {},
        [](const BatchItem &) { throw std::runtime_error("observer"); }, cancel);
    QCOMPARE(batch.items.size(), 1);
    QCOMPARE(batch.items[0].computation, RunState::Cancelled);
    QVERIFY(batch.items[0].persistence.saved());
    QCOMPARE(batch.observerWarnings.size(), 1);
}
void AnalysisTests::fullScientificRoundTrip()
{
    WorkflowModel m(makeScientificRegistry());
    auto root = m.addRoot("input_series", {0, 0}).nodeId;
    auto prep = m.addChild(root, ChildSlot::Down, "preprocess").nodeId;
    auto trend = m.addChild(prep, ChildSlot::Down, "detrend_lowess").nodeId;
    auto mtm = m.addChild(trend, ChildSlot::Left, "mtm", {{"ar1", true}}).nodeId;
    auto wave = m.addChild(trend, ChildSlot::Right, "cwt").nodeId;
    auto result = WorkflowExecutor().run({m.freeze().snapshot, root, {"scientific", signal(), {}}});
    QCOMPARE(result.state, RunState::Succeeded);
    QVERIFY(result.nodes[mtm].outputs.contains("swa_background"));
    QVERIFY(result.nodes[wave].outputs.contains("significance_ratio"));
    ResultRepository repo;
    QVERIFY(repo.open(":memory:").isEmpty());
    auto saved = repo.save(result);
    QVERIFY2(saved.saved(), qPrintable(saved.error));
    auto loaded = repo.load(saved.recordId);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    for (const auto &id : result.executionOrder)
        for (auto it = result.nodes[id].outputs.begin(); it != result.nodes[id].outputs.end(); ++it)
            QCOMPARE(encodeData(loaded.run.nodes[id].outputs[it.key()]).bytes,
                     encodeData(it.value()).bytes);
}

void AnalysisTests::fileProbeAndFrozenOptions()
{
    QTemporaryDir dir;
    const auto a = dir.filePath("header.csv"), b = dir.filePath("numeric.txt");
    QVERIFY(writeFile(a, "# comment\n\"Time, ka\",Signal,Other\n0,8,11\n1,9,12\n"));
    QVERIFY(writeFile(b, "0 20 31\n1 21 32\n"));
    SeriesFileOptions o;
    o.autoHeader = true;
    auto probe = probeSeriesFile(a, o);
    QVERIFY2(probe.ok(), qPrintable(probe.error));
    QVERIFY(probe.header);
    QCOMPARE(probe.columns[0], QString("Time, ka"));
    probe = probeSeriesFile(b, o);
    QVERIFY(probe.ok());
    QVERIFY(!probe.header);
    auto data = readSeriesFile(b, o);
    QVERIFY(data.ok());
    QCOMPARE(dataAs<SeriesData>(data.data)->x.size(), 2);
    o.yColumn = 2;
    o.xUnit = "ka";
    auto plan = filePlan({{a, {}, {{"path", a}, {"file_options", fileOptionsToJson(o)}}}});
    QVERIFY(plan.validation.ok());
    const auto run = WorkflowExecutor().run(plan.requests.front());
    QCOMPARE(run.state, RunState::Succeeded);
    const auto *series = dataAs<SeriesData>(run.nodes[run.root].outputs["series"]);
    QVERIFY(series);
    QCOMPARE(series->y, QVector<double>({11, 12}));
    QCOMPARE(series->xAxis.unit, QString("ka"));
    QCOMPARE(run.nodes[run.root].report["y_column"].toInt(), 2);
    QCOMPARE(run.provenance["file_options"].toObject()["x_unit"].toString(), QString("ka"));
    auto bad = o;
    QVERIFY(!applyFileOptions({{"y_column", 1.5}}, bad).isEmpty());
    QCOMPARE(bad.yColumn, 2);
    bad.yColumn = 5;
    QVERIFY(!readSeriesFile(a, bad).ok());
    const auto huge = dir.filePath("long.csv");
    QVERIFY(writeFile(huge, QByteArray(70000, 'x') + ",Y\n0,1\n"));
    QVERIFY(!probeSeriesFile(huge, o).ok());
}
namespace
{
RunResult peakedRun(QString label, QString unit = "1/ka")
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto mtm =
        model.addChild(root, ChildSlot::Down, "mtm", {{"ar1", false}, {"swa", false}}).nodeId;
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {label, signal(64), {}}});
    run.nodes[mtm].outputs["spectrum"] =
        makeData(SpectrumData{{0, .1, .2}, {0, 4, 2}, {"Frequency", unit}, {"Power", "PSD"}});
    run.nodes[mtm].outputs["peaks"] =
        makeData(TableData{{"frequency", "power"}, {{.1, 4}, {.2, 2}}});
    return run;
}
QString downgradeToV1(QString path)
{
    for (const auto &statement :
         {"DROP TABLE aster_dependencies", "DROP TABLE aster_peaks", "DROP INDEX aster_run_date",
          "ALTER TABLE aster_runs DROP COLUMN display_name",
          "ALTER TABLE aster_runs DROP COLUMN notes", "UPDATE aster_meta SET schema_version=1"})
    {
        const auto e = sql(path, statement);
        if (!e.isEmpty())
            return e;
    }
    return {};
}
} // namespace
void AnalysisTests::frequencySearchAndMetadata()
{
    ResultRepository repo;
    QVERIFY(repo.open(":memory:").isEmpty());
    const auto run = peakedRun("原始.csv");
    const auto id = repo.save(run).recordId;
    QVERIFY(id > 0);
    QVERIFY(repo.save(peakedRun("seconds", "1/s")).saved());
    RecordFilter f;
    f.minFrequency = .1;
    f.maxFrequency = .1;
    f.frequencyUnit = "1/ka";
    auto list = repo.list(f);
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    QCOMPARE(list.records.size(), 1);
    QCOMPARE(list.records[0].id, id);
    QCOMPARE(repo.matchingPeaks(id, f).peaks.size(), 1);
    f.maxFrequency = -1;
    QCOMPARE(repo.list(f).records.size(), 1);
    f.minFrequency = -1;
    f.maxFrequency = .2;
    QCOMPARE(repo.matchingPeaks(id, f).peaks.size(), 2);
    f.frequencyUnit = "";
    QCOMPARE(repo.list(f).records.size(), 0);
    f.frequencyUnit = "1/ka";
    f.minFrequency = .3;
    QVERIFY(!repo.list(f).error.isEmpty());
    QVERIFY(repo.updateMetadata(id, "测试名称", "备注 100% ' quoted").isEmpty());
    f = RecordFilter{};
    f.text = "100% '";
    QCOMPARE(repo.list(f).records.size(), 1);
    const auto loaded = repo.load(id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.displayName, QString("测试名称"));
    QCOMPARE(loaded.run.sourceLabel, run.sourceLabel);
    QCOMPARE(loaded.run.workflowSnapshot, run.workflowSnapshot);
    f = RecordFilter{};
    f.since = run.startedAt.addDays(1);
    QVERIFY(repo.list(f).records.isEmpty());
    QVERIFY(repo.remove(id).isEmpty());
    QVERIFY(!repo.load(id).ok());
    QVERIFY(repo.matchingPeaks(id, {}).peaks.isEmpty());
    QVERIFY(!repo.remove(id).isEmpty());
    QCOMPARE(repo.list().records.size(), 1);
}
void AnalysisTests::databaseV1Migration()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("v1.sqlite");
    const auto run = peakedRun("legacy-new-core");
    qint64 id;
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        id = repo.save(run).recordId;
        QVERIFY(id > 0);
    }
    QVERIFY(downgradeToV1(path).isEmpty());
    ResultRepository repo;
    const auto error = repo.open(path);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    RecordFilter f;
    f.minFrequency = .09;
    f.maxFrequency = .11;
    f.frequencyUnit = "1/ka";
    QCOMPARE(repo.list(f).records.size(), 1);
    QCOMPARE(repo.list(f).records[0].id, id);
    const auto loaded = repo.load(id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.workflowSnapshot, run.workflowSnapshot);
    for (auto node = run.nodes.begin(); node != run.nodes.end(); ++node)
        for (auto out = node->outputs.begin(); out != node->outputs.end(); ++out)
            QCOMPARE(encodeData(loaded.run.nodes[node.key()].outputs[out.key()]).bytes,
                     encodeData(out.value()).bytes);
}
void AnalysisTests::databaseMigrationRollback()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("bad-v1.sqlite");
    {
        ResultRepository repo;
        QVERIFY(repo.open(path).isEmpty());
        QVERIFY(repo.save(peakedRun("bad")).saved());
    }
    QVERIFY(downgradeToV1(path).isEmpty());
    QVERIFY(sql(path, "UPDATE aster_outputs SET data=X'00' WHERE port='peaks'").isEmpty());
    {
        ResultRepository repo;
        QVERIFY(!repo.open(path).isEmpty());
    }
    // If DDL wasn't rolled back, either statement fails (duplicate column/table).
    QVERIFY(sql(path, "ALTER TABLE aster_runs ADD COLUMN display_name TEXT NOT NULL DEFAULT ''")
                .isEmpty());
    QVERIFY(sql(path, "CREATE TABLE aster_peaks(test INTEGER)").isEmpty());
}
void AnalysisTests::batchCollectsAnyCompletedInput()
{
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool thirdStarted = false;
    };
    struct GatedTask : Task
    {
        std::shared_ptr<Gate> gate;
        explicit GatedTask(std::shared_ptr<Gate> g) : gate(g) {}
        NodeResult execute(const TaskInput &, const ExecutionContext &c) override
        {
            const auto label = c.sourceMetadata["name"].toString();
            if (label == "first")
            {
                std::unique_lock<std::mutex> lock(gate->mutex);
                if (!gate->changed.wait_for(lock, std::chrono::seconds(3),
                                            [&] { return gate->thirdStarted; }))
                    return NodeResult::failure("Head-of-line scheduling blocked third input");
            }
            else if (label == "third")
            {
                {
                    std::lock_guard<std::mutex> lock(gate->mutex);
                    gate->thirdStarted = true;
                }
                gate->changed.notify_all();
            }
            return NodeResult::success({{"series", signal(4)}});
        }
    };
    auto gate = std::make_shared<Gate>();
    auto registry = makeScientificRegistry();
    TaskDescriptor d;
    d.key = "gated";
    d.label = "gated";
    d.category = {"Test"};
    d.requiresSourceBinding = true;
    d.outputs = {{"series", "series", DataKind::Series, true}};
    d.defaultOutput = "series";
    QVERIFY(
        registry.add(d, [gate](const QJsonObject &) { return std::make_unique<GatedTask>(gate); })
            .ok());
    WorkflowModel model(registry);
    const auto root = model.addRoot("gated", {0, 0}).nodeId;
    QVector<SourceItem> sources;
    for (const auto &name : {"first", "second", "third"})
        sources.push_back({name, {}, {{"name", name}}});
    const auto plan = prepareRuns(model.freeze().snapshot, {{root, sources}});
    QVERIFY(plan.validation.ok());
    QTemporaryDir dir;
    const auto batch = BatchRunner().run(plan, dir.filePath("runs.sqlite"), 2);
    QVERIFY2(batch.error.isEmpty(), qPrintable(batch.error));
    QCOMPARE(batch.items.size(), 3);
    for (const auto &item : batch.items)
    {
        QCOMPARE(item.computation, RunState::Succeeded);
        QVERIFY(item.persistence.saved());
    }
}
void AnalysisTests::structuredAlgorithmDiagnostics_data()
{
    QTest::addColumn<QString>("method");
    QTest::addColumn<QString>("variant");
    QTest::addColumn<QJsonObject>("parameters");
    QTest::addColumn<QString>("code");
    QTest::newRow("lowess-duplicate") << "detrend_lowess" << "duplicate" << QJsonObject{} << "INPUT_X_DUPLICATE";
    QTest::newRow("lowess-descending") << "detrend_lowess" << "descending" << QJsonObject{} << "INPUT_X_DESCENDING";
    QTest::newRow("preprocess-duplicate-policy") << "preprocess" << "duplicate" << QJsonObject{{"duplicate_policy", "error"}} << "INPUT_X_DUPLICATE";
    QTest::newRow("preprocess-distinct-count") << "preprocess" << "constant_x" << QJsonObject{} << "INPUT_TOO_FEW_DISTINCT_X";
    QTest::newRow("preprocess-interpolation-count") << "preprocess" << "short" << QJsonObject{{"interpolation", "akima"}} << "INTERPOLATION_TOO_FEW_SAMPLES";
    QTest::newRow("preprocess-grid") << "preprocess" << "ordinary" << QJsonObject{{"spacing_mode", "fixed"}, {"spacing", 1e-12}} << "RESAMPLE_GRID_INVALID";
    QTest::newRow("linear-degenerate") << "linear_detrend" << "constant_x" << QJsonObject{} << "LINEAR_DETREND_X_DEGENERATE";
    QTest::newRow("mtm-nonuniform") << "mtm" << "nonuniform" << QJsonObject{} << "INPUT_X_NONUNIFORM";
    QTest::newRow("mtm-nw") << "mtm" << "short" << QJsonObject{} << "MTM_NW_INVALID";
    QTest::newRow("wavelet-count") << "cwt" << "short" << QJsonObject{} << "INPUT_TOO_FEW_SAMPLES";
    QTest::newRow("wavelet-nonuniform") << "cwt" << "nonuniform" << QJsonObject{} << "INPUT_X_NONUNIFORM";
    QTest::newRow("wavelet-paul-order") << "cwt" << "ordinary" << QJsonObject{{"mother", "paul"}, {"parameter", 1.5}} << "CWT_ORDER_INVALID";
    QTest::newRow("wavelet-dog-order") << "cwt" << "ordinary" << QJsonObject{{"mother", "dog"}, {"parameter", 3}} << "CWT_ORDER_INVALID";
    QTest::newRow("wavelet-grid") << "cwt" << "large" << QJsonObject{{"dj", .02}} << "CWT_GRID_LIMIT";
    QTest::newRow("affine-output-range") << "affine" << "huge_y" << QJsonObject{{"factor", 1e100}} << "TASK_OUTPUT_INVALID";
    QTest::newRow("summary-output-range") << "series_summary" << "huge_y" << QJsonObject{} << "TASK_OUTPUT_INVALID";
}
void AnalysisTests::structuredAlgorithmDiagnostics()
{
    QFETCH(QString, method);
    QFETCH(QString, variant);
    QFETCH(QJsonObject, parameters);
    QFETCH(QString, code);
    SeriesData data = *dataAs<SeriesData>(signal(variant == "short" ? 3 : variant == "large" ? 32768 : 64));
    if (variant == "duplicate") data.x[11] = data.x[10];
    if (variant == "descending") data.x[11] = data.x[10] - .5;
    if (variant == "constant_x") data.x.fill(1);
    if (variant == "nonuniform") data.x[11] += .1;
    if (variant == "huge_y") for (int i = 0; i < data.y.size(); ++i) data.y[i] = i % 2 ? 1e308 : -1e308;
    const auto input = makeData(data);
    const auto result = execute(method, input, parameters);
    QCOMPARE(result.state, NodeState::Failed);
    QCOMPARE(result.diagnostic().code, code);
    QCOMPARE(result.diagnostic().context["method"].toString(), method);
    QVERIFY(!result.diagnostic().context["node_id"].toString().isEmpty());
    QVERIFY(result.outputs.isEmpty());
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(dataAs<SeriesData>(input)->x, data.x); // No implicit sorting/resampling.
    if (variant == "duplicate" || variant == "descending")
    {
        QCOMPARE(result.diagnostic().context["point_before"].toInt(), 11);
        QCOMPARE(result.diagnostic().context["point"].toInt(), 12);
    }
    if (variant == "nonuniform") QVERIFY(result.diagnostic().context.contains("tolerance"));
}
void AnalysisTests::directValidInputNeedsNoPreprocessingNode()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto lowess = model.addChild(root, ChildSlot::Down, "detrend_lowess").nodeId;
    const auto mtm = model.addChild(lowess, ChildSlot::Down, "mtm").nodeId;
    const auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"already_valid", signal(128), {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(run.executionOrder.size(), 3);
    QVERIFY(run.nodes[mtm].outputs.contains("spectrum"));
    for (const auto &node : run.nodes) QVERIFY(node.diagnostic().isEmpty());
}
#include "ar1_global_audit.h"
#include "astronomy_audit.h"
#include "seasonal_audit.h"
#include "filter_audit.h"
#include "evolution_audit.h"
#include "basic_signal_audit.h"
#include "spectral_extension_audit.h"
#include "chronology_audit.h"
#include "cycle_tuning_audit.h"
#include "coco_audit.h"
#include "coco_analysis_audit.h"
#include "odp_examples_audit.h"
#include "synthetic_audit.h"
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    AnalysisTests tests;
    const int first = QTest::qExec(&tests, argc, argv);
    const int second = runSpectralAudit(argc, argv);
    return first | second | runWaveletAudit(argc, argv) | runTransferAudit(argc, argv) | runMtmSimulationAudit(argc, argv) | runAcycleAlignmentAudit(argc, argv) | runBackendAudit(argc, argv) | runAr1GlobalAudit(argc, argv) | runAstronomyAudit(argc, argv) | runSeasonalAudit(argc, argv) | runFilterAudit(argc, argv) | runOrbitalTablesAudit(argc,argv) | runEvolutionAudit(argc,argv) | runBasicSignalAudit(argc,argv) | runSpectralExtensionAudit(argc,argv) | runChronologyAudit(argc,argv) | runCycleTuningAudit(argc,argv) | runCocoAudit(argc,argv) | runCocoAnalysisAudit(argc,argv) | runOdpExamplesAudit(argc,argv) | runSyntheticAudit(argc,argv);
}
#include "analysis_tests.moc"
