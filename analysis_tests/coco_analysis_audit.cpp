// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "coco_analysis_audit.h"
#include "math/chronology/coco_analysis.h"
#include "processing/scientific_registry.h"
#include "processing/coco_analysis_tasks.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <cmath>
using namespace aster;
using namespace aster::chronology;
namespace
{
std::vector<double> values(QJsonValue j)
{
    std::vector<double> v;
    for (auto x : j.toArray())
        v.push_back(x.toDouble());
    return v;
}
std::vector<double> values(const QVector<double> &v)
{
    return {v.begin(), v.end()};
}
bool near(const std::vector<double> &a, const std::vector<double> &b, double tol = 3e-10)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!std::isfinite(a[i]) || std::abs(a[i] - b[i]) > tol * (1 + std::abs(b[i])))
        {
            qWarning() << i << a[i] << b[i];
            return false;
        }
    return true;
}
CocoOptions options()
{
    CocoOptions o;
    o.periodsKa = {405, 125, 95};
    o.relativePower = {1, .6, .3};
    o.minimumRate = 2;
    o.maximumRate = 6;
    o.rateStep = .5;
    o.maximumFrequency = .02;
    return o;
}
QJsonObject parameters()
{
    return {{"coco_periods", "405,125,95"}, {"coco_powers", "1,.6,.3"}, {"coco_rate_min", 2},
            {"coco_rate_max", 6},           {"coco_rate_step", .5},     {"coco_max_frequency", .02},
            {"coco_simulations", 79},       {"ecoco_window", 128},      {"ecoco_step", 64}};
}
QJsonObject paramsFor(const QString &method)
{
    auto p = parameters();
    if (method == "coco_monte_carlo")
    {
        p.remove("ecoco_window");
        p.remove("ecoco_step");
    }
    else
        p.remove("coco_simulations");
    return p;
}
SeriesData series(const std::vector<double> &y)
{
    SeriesData s;
    s.xAxis = {"Depth", "m"};
    s.yAxis = {"Proxy", "u"};
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        s.x << 10 + i * .5;
        s.y << y[i];
    }
    return s;
}
NodeResult task(QString method, const SeriesData &s, QJsonObject p = {})
{
    auto r = makeScientificRegistry();
    if (p.isEmpty())
        p = paramsFor(method);
    auto t = r.create(method, p);
    if (!t)
        return NodeResult::failure("Invalid test configuration");
    return t->execute({makeData(s)}, {});
}
} // namespace
class CocoAnalysisAudit : public QObject
{
    Q_OBJECT
    QJsonObject reference;
  private slots:
    void initTestCase();
    void generators();
    void monteCarloReference_data();
    void monteCarloReference();
    void evolutionReference();
    void invariantsAndConcurrentRuns();
    void invalidInputsAndBudgets();
    void cancellationAndProgress();
    void adapterContracts_data();
    void adapterContracts();
    void persistence_data();
    void persistence();
    void calibration();
};
void CocoAnalysisAudit::initTestCase()
{
    QFile f(QFINDTESTDATA("fixtures/coco_analysis_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    reference = QJsonDocument::fromJson(f.readAll()).object();
    QVERIFY(!reference.isEmpty());
}
void CocoAnalysisAudit::generators()
{
    std::mt19937_64 a(1729), b(1729);
    QVERIFY(near(cocoAr1(25, .75, a), values(reference["ar1"]), 2e-14));
    QVERIFY(near(cocoRandomPeaks(128, 256, 7, b), values(reference["random_peaks"]), 2e-14));
    std::mt19937_64 c(5489);
    QCOMPARE(c(), std::uint64_t(14514284786278117030ULL));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoAr1(0, 0, a));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoAr1(128, 1, a));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoAr1(128, qQNaN(), a));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoRandomPeaks(128, 256, 0, a));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoRandomPeaks(16, 262144, 8192, a));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoRandomPeaks(128, 255, 3, a));
}
void CocoAnalysisAudit::monteCarloReference_data()
{
    QTest::addColumn<QJsonObject>("j");
    QFile f(QFINDTESTDATA("fixtures/coco_analysis_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    for (auto v : QJsonDocument::fromJson(f.readAll()).object()["mc"].toArray())
    {
        auto j = v.toObject();
        QTest::newRow(qPrintable(j["model"].toString() + j["mode"].toString())) << j;
    }
}
void CocoAnalysisAudit::monteCarloReference()
{
    QFETCH(QJsonObject, j);
    auto o = options();
    o.detrend = j["mode"] == "linear" ? CocoOptions::Detrend::Linear
                : j["mode"] == "none" ? CocoOptions::Detrend::None
                                      : CocoOptions::Detrend::Mean;
    CocoMonteCarloOptions mc;
    mc.simulations = std::size_t(j["B"].toInt());
    mc.seed = std::uint64_t(j["seed"].toInt());
    mc.rho = j["rho"].toDouble();
    mc.peakCount = std::size_t(j["peaks"].toInt());
    if (j["model"] == "random_peaks")
        mc.nullModel = CocoMonteCarloOptions::Null::RandomPeaks;
    const auto result = cocoMonteCarlo(values(j["y"]), .5, o, mc);
    std::vector<double> correlation;
    for (auto r : result.observed.rows)
        correlation.push_back(r.correlation);
    QVERIFY(near(correlation, values(j["correlations"])));
    QVERIFY(near(result.rates, values(reference["rates"])));
    QVERIFY(near(result.pointwiseP, values(j["pointwise"]), 1e-14));
    QVERIFY(near(result.maximum.adjustedP, values(j["adjusted"]), 1e-14));
    QVERIFY(near(result.maximum.nullMaxima, values(j["maxima"])));
    QCOMPARE(result.maximum.globalP, j["global_p"].toDouble());
    QVERIFY(std::abs(result.maximum.criticalValue - j["critical"].toDouble()) < 3e-10);
    for (std::size_t i = 0; i < result.rates.size(); ++i)
    {
        QVERIFY(result.pointwiseP[i] >= 1. / (mc.simulations + 1));
        QVERIFY(result.maximum.adjustedP[i] >= result.pointwiseP[i]);
        if (result.evidence[i] == 0)
            QCOMPARE(result.pointwiseP[i], 1.);
    }
    QCOMPARE(result.maximum.globalP,
             *std::min_element(result.maximum.adjustedP.begin(), result.maximum.adjustedP.end()));
}
void CocoAnalysisAudit::evolutionReference()
{
    const auto j = reference["evolution"].toObject();
    CocoEvolutionOptions e;
    e.windowSamples = 128;
    e.stepSamples = 64;
    const auto v = cocoEvolution(values(j["y"]), .5, options(), e);
    QVERIFY(near(v.centers, values(j["centers"])));
    QVERIFY(near(v.correlation, values(j["matrix"])));
    QVERIFY(near(v.bestRate, values(j["best_rate"])));
    QVERIFY(near(v.bestCorrelation, values(j["best_correlation"])));
    QCOMPARE(v.unusedTail, std::size_t(j["tail"].toInt()));
    QCOMPARE(v.fftSize, std::size_t(256));
    // Compare individual windows with the public single-window scan; protects equivalent window semantics.
    auto y = values(j["y"]);
    for (std::size_t w = 0; w < v.centers.size(); ++w)
    {
        std::vector<double> window(y.begin() + w * 64, y.begin() + w * 64 + 128);
        const auto scan = cocoScan(window, .5, options());
        for (std::size_t k = 0; k < v.rates.size(); ++k)
            QVERIFY(std::abs(scan.rows[k].correlation - v.correlation[k * v.centers.size() + w]) <
                    1e-13);
    }
}
void CocoAnalysisAudit::invariantsAndConcurrentRuns()
{
    auto y = values(reference["mc"].toArray()[0].toObject()["y"]);
    auto o = options();
    CocoMonteCarloOptions mc;
    mc.simulations = 39;
    const auto a = cocoMonteCarlo(y, .5, o, mc);
    const auto b = cocoMonteCarlo(y, .5, o, mc);
    QCOMPARE(a.maximum.nullMaxima, b.maximum.nullMaxima);
    QCOMPARE(a.maximum.adjustedP, b.maximum.adjustedP);
    auto f1 = std::async(std::launch::async, [=] { return cocoMonteCarlo(y, .5, o, mc); });
    auto f2 = std::async(std::launch::async, [=] { return cocoMonteCarlo(y, .5, o, mc); });
    QCOMPARE(f1.get().maximum.nullMaxima, a.maximum.nullMaxima);
    QCOMPARE(f2.get().maximum.nullMaxima, a.maximum.nullMaxima);
    for (auto &v : y)
        v = -3 * v + 100;
    const auto changed = cocoMonteCarlo(y, .5, o, mc);
    QVERIFY(near(changed.evidence, a.evidence, 1e-12));
    QCOMPARE(changed.maximum.adjustedP, a.maximum.adjustedP);
    mc.seed++;
    QVERIFY(cocoMonteCarlo(y, .5, o, mc).maximum.nullMaxima != a.maximum.nullMaxima);
    mc.nullModel = CocoMonteCarloOptions::Null::RandomPeaks;
    mc.peakCount = 0;
    auto automatic = cocoMonteCarlo(y, .5, o, mc);
    QVERIFY(automatic.peaks > 0);
    mc.peakCount = automatic.peaks;
    QCOMPARE(cocoMonteCarlo(y, .5, o, mc).maximum.nullMaxima, automatic.maximum.nullMaxima);
}
void CocoAnalysisAudit::invalidInputsAndBudgets()
{
    auto y = values(reference["mc"].toArray()[0].toObject()["y"]);
    auto o = options();
    CocoMonteCarloOptions mc;
    mc.simulations = 19;
    auto b = mc;
    b.simulations = 18;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoMonteCarlo(y, .5, o, b));
    b = mc;
    b.alpha = .01;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoMonteCarlo(y, .5, o, b));
    b = mc;
    b.rho = -1;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoMonteCarlo(y, .5, o, b));
    b = mc;
    b.nullModel = static_cast<CocoMonteCarloOptions::Null>(99);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoMonteCarlo(y, .5, o, b));
    b = mc;
    b.simulations = 10000;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoMonteCarlo(y, .5, o, b));
    auto bad = y;
    bad[0] = qQNaN();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoMonteCarlo(bad, .5, o, mc));
    bad.assign(y.size(), 1);
    QVERIFY_THROWS_EXCEPTION(std::domain_error, cocoMonteCarlo(bad, .5, o, mc));
    bad.assign(16384, 1);
    b = mc;
    b.simulations = 9999;
    QVERIFY_THROWS_EXCEPTION(std::length_error, cocoMonteCarlo(bad, .5, o, b));
    CocoEvolutionOptions e;
    e.windowSamples = 128;
    e.stepSamples = 64;
    auto yy = values(reference["evolution"].toObject()["y"]);
    auto ee = e;
    ee.stepSamples = 0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoEvolution(yy, .5, o, ee));
    ee = e;
    ee.windowSamples = yy.size() + 1;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoEvolution(yy, .5, o, ee));
    ee = e;
    ee.windowSamples = yy.size();
    int invalidProgress = 0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        cocoEvolution(yy, .5, o, ee, {}, [&](int) { ++invalidProgress; }));
    QCOMPARE(invalidProgress, 0); // Reject unusable geometry before building target spectra.
    bad.assign(1000, 1);
    ee = e;
    ee.stepSamples = 1;
    QVERIFY_THROWS_EXCEPTION(std::length_error, cocoEvolution(bad, .5, o, ee));
    auto one = o;
    one.maximumRate = one.minimumRate;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        cocoEvolution(yy, .5, one, e, {}, [&](int) { ++invalidProgress; }));
    QCOMPARE(invalidProgress, 0);
    bad = y;
    bad.resize(256, 1);
    ee = e;
    ee.stepSamples = 128;
    try
    {
        cocoEvolution(bad, .5, o, ee);
        QFAIL("Constant window was not rejected");
    }
    catch (const std::domain_error &err)
    {
        QVERIFY(QString(err.what()).contains("window 2"));
    }
    auto s = series(yy);
    s.x[10] = s.x[9];
    QCOMPARE(task("evolution_coco", s).state, NodeState::Failed);
    s = series(yy);
    s.xAxis.unit = "ka";
    QCOMPARE(task("evolution_coco", s).diagnostic().code, QString("CHRONOLOGY_UNIT"));
    auto registry = makeScientificRegistry();
    for (auto method : {"coco_monte_carlo", "evolution_coco"})
    {
        auto t = registry.create(method, paramsFor(method));
        QVERIFY(t);
        QCOMPARE(t->execute({makeData(TableData{{"a"}, {{1}}})}, {}).diagnostic().code,
                 QString("INPUT_TYPE"));
        QCOMPARE(t->execute({}, {}).state, NodeState::Failed);
    }
}
void CocoAnalysisAudit::cancellationAndProgress()
{
    auto yy = values(reference["evolution"].toObject()["y"]);
    auto o = options();
    CocoMonteCarloOptions mc;
    mc.simulations = 39;
    QVERIFY_THROWS_EXCEPTION(CocoCancelled, cocoMonteCarlo(yy, .5, o, mc, [] { return true; }));
    bool stop = false;
    int last = -1;
    QVERIFY_THROWS_EXCEPTION(CocoCancelled, cocoMonteCarlo(
                                                yy, .5, o, mc, [&] { return stop; },
                                                [&](int p)
                                                {
                                                    QVERIFY(p >= last);
                                                    last = p;
                                                    if (p >= 30)
                                                        stop = true;
                                                }));
    QVERIFY(stop);
    stop = false;
    last = -1;
    CocoEvolutionOptions e;
    e.windowSamples = 128;
    e.stepSamples = 64;
    QVERIFY_THROWS_EXCEPTION(CocoCancelled, cocoEvolution(
                                                yy, .5, o, e, [&] { return stop; },
                                                [&](int p)
                                                {
                                                    QVERIFY(p >= last);
                                                    last = p;
                                                    if (p >= 30)
                                                        stop = true;
                                                }));
    QVERIFY(stop);
    auto r = makeScientificRegistry();
    ExecutionContext c;
    c.progress = [&](int p, const QString &)
    {
        if (p > 25)
            c.cancellation.cancel();
    };
    auto t = r.create("coco_monte_carlo", paramsFor("coco_monte_carlo"));
    QVERIFY(t);
    const auto result = t->execute({makeData(series(yy))}, c);
    QCOMPARE(result.state, NodeState::Cancelled);
    QVERIFY(result.outputs.isEmpty());
}
void CocoAnalysisAudit::adapterContracts_data()
{
    QTest::addColumn<QString>("method");
    QTest::newRow("MC") << QString("coco_monte_carlo");
    QTest::newRow("eCOCO") << QString("evolution_coco");
}
void CocoAnalysisAudit::adapterContracts()
{
    QFETCH(QString, method);
    auto r = makeScientificRegistry();
    auto p = paramsFor(method);
    auto y = values(reference["evolution"].toObject()["y"]);
    auto s = series(y);
    auto result = task(method, s, p);
    QVERIFY2(result.state == NodeState::Succeeded, qPrintable(result.error));
    const auto *entry = r.find(method);
    QVERIFY(entry);
    QVERIFY(r.validateOutput(entry->descriptor, result).isEmpty());
    QCOMPARE(result.outputs.size(), method == "evolution_coco" ? 6 : 9);
    auto defaults = r.normalize(method, {});
    QVERIFY(defaults.validation.ok());
    if (method == "coco_monte_carlo")
    {
        CocoMonteCarloOptions mc;
        QCOMPARE(defaults.parameters["coco_null"].toString(),
            mc.nullModel == CocoMonteCarloOptions::Null::FixedAr1
                ? QString("fixed_ar1") : QString("random_peaks"));
        QCOMPARE(defaults.parameters["coco_simulations"].toInt(), int(mc.simulations));
        QCOMPARE(defaults.parameters["coco_rho"].toDouble(), mc.rho);
        QVERIFY(result.report["null_maximum_adjustment"].toBool());
        QVERIFY(!result.report["rho_estimated"].toBool());
        p["coco_null"] = "random_peaks";
        p["coco_null_peaks"] = 7;
        auto rr = task(method, s, p);
        QCOMPARE(rr.state, NodeState::Succeeded);
        QVERIFY(rr.warnings.contains("warning.coco_random_reference"));
    }
    else
    {
        CocoEvolutionOptions e;
        QCOMPARE(defaults.parameters["ecoco_window"].toInt(), int(e.windowSamples));
        QCOMPARE(defaults.parameters["ecoco_step"].toInt(), int(e.stepSamples));
        const auto *g = dataAs<GridData>(result.outputs["correlation_map"]);
        QVERIFY(g);
        QCOMPARE(g->columns, qsizetype(6));
        QCOMPARE(g->rows, qsizetype(9));
        QCOMPARE(g->x.front(), 41.75);
        QCOMPARE(result.report["unused_tail_samples"].toInt(), 37);
        QVERIFY(result.warnings.contains("warning.ecoco_tail"));
    }
    // Depth units/origin and amplitude do not change correlation or rank p values.
    for (auto &x : s.x)
        x = 100 * x + 5000;
    s.xAxis.unit = "cm";
    for (auto &v : s.y)
        v = 2 * v - 50;
    p = paramsFor(method);
    p["chrono_depth_unit"] = "cm";
    auto scaled = task(method, s, p);
    QCOMPARE(scaled.state, NodeState::Succeeded);
    if (method == "coco_monte_carlo")
        QCOMPARE(dataAs<SeriesData>(scaled.outputs["scan_adjusted_p"])->y,
                 dataAs<SeriesData>(result.outputs["scan_adjusted_p"])->y);
    else
        QVERIFY(near(values(dataAs<GridData>(scaled.outputs["correlation_map"])->values),
                     values(dataAs<GridData>(result.outputs["correlation_map"])->values)));
    for (auto lang : {"zh", "en"})
    {
        QFile f(QFINDTESTDATA(qPrintable(QString("../resources/i18n/") + lang + ".json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto words = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(words.contains("method." + method));
        for (auto param : entry->descriptor.parameters)
        {
            QCOMPARE(defaults.parameters[param.key], param.defaultValue);
            QVERIFY2(words.contains("parameter." + param.key), qPrintable(param.key));
            QVERIFY2(words.contains("parameterhelp." + param.key), qPrintable(param.key));
            QVERIFY(words.contains("parametergroup." + param.group));
            for (auto choice : param.choices)
                QVERIFY(words.contains("choice." + param.key + "." + choice));
        }
        for (auto port : entry->descriptor.outputs)
            QVERIFY2(words.contains("output." + method + "." + port.key), qPrintable(port.key));
        for (auto warning : result.warnings)
            QVERIFY2(words.contains(warning), qPrintable(warning));
        for (auto data : result.outputs)
            if (const auto *t = dataAs<TableData>(data))
                for (auto col : t->columns)
                    QVERIFY2(words.contains("column." + col), qPrintable(col));
    }
}
void CocoAnalysisAudit::persistence_data()
{
    adapterContracts_data();
}
void CocoAnalysisAudit::persistence()
{
    QFETCH(QString, method);
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    auto child = model.addChild(root, ChildSlot::Down, method, paramsFor(method));
    QVERIFY(child.ok());
    auto source = makeData(series(values(reference["evolution"].toObject()["y"])));
    auto original = encodeData(source).bytes;
    const auto run = WorkflowExecutor().run(
        {model.freeze().snapshot, root, {"COCO statistics proxy", source, {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(encodeData(source).bytes, original);
    QTemporaryDir dir;
    ResultRepository db;
    QVERIFY(db.open(dir.filePath("results.sqlite")).isEmpty());
    const auto saved = db.save(run);
    QVERIFY(saved.saved());
    auto loaded = db.load(saved.recordId);
    QVERIFY(loaded.ok());
    const auto expected = run.nodes.value(child.nodeId);
    const auto actual = loaded.run.nodes.value(child.nodeId);
    QCOMPARE(expected.report, actual.report);
    QCOMPARE(expected.warnings, actual.warnings);
    for (auto it = expected.outputs.cbegin(); it != expected.outputs.cend(); ++it)
    {
        QCOMPARE(encodeData(actual.outputs[it.key()]).bytes, encodeData(it.value()).bytes);
        QVERIFY(exportDataCsv(it.value(), dir.filePath(it.key() + ".csv")).ok());
    }
}
void CocoAnalysisAudit::calibration()
{
    // Complete-null check only: fixed rho, target and grid; independently seeded
    // stationary draws. Conditional plug-in/random-peak inference is NOT claimed.
    QJsonArray records;
    const int trials = 200;
    auto o = options();
    o.rateStep = 1;
    for (double rho : {0., .7, .95})
    {
        int rejected = 0;
        CocoMonteCarloOptions mc;
        mc.simulations = 99;
        mc.rho = rho;
        for (int t = 0; t < trials; ++t)
        {
            std::mt19937_64 rng(730019 + std::uint64_t(t) * 43 + std::uint64_t(1000 * rho));
            auto y = cocoAr1(128, rho, rng);
            mc.seed = 901923 + std::uint64_t(t) * 17;
            const auto result = cocoMonteCarlo(y, .5, o, mc);
            if (result.maximum.globalP <= mc.alpha)
                ++rejected;
        }
        const double phat = double(rejected) / trials, z = 1.959963984540054,
                     den = 1 + z * z / trials, center = (phat + z * z / (2 * trials)) / den;
        const double half =
            z * std::sqrt(phat * (1 - phat) / trials + z * z / (4. * trials * trials)) / den;
        qInfo() << "COCO Gaussian AR1 complete-null rho" << rho << "rejections" << rejected << "/"
                << trials << "Wilson95" << center - half << center + half;
        records.append(QJsonObject{{"rho", rho},
                                   {"trials", trials},
                                   {"simulations", int(mc.simulations)},
                                   {"alpha", mc.alpha},
                                   {"rejections", rejected},
                                   {"rate", phat},
                                   {"wilson95", QJsonArray{center - half, center + half}}});
        // Detect gross inflation without turning a random 95% interval into a flaky test.
        QVERIFY2(rejected <= 30,
                 "Gross false-positive inflation under a predeclared complete null");
    }
    const auto output = qEnvironmentVariable("ASTER_CAPTURE_DIR");
    if (!output.isEmpty())
    {
        QDir().mkpath(output);
        QFile f(output + "/coco_calibration.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(
            QJsonDocument(QJsonObject{{"scope", "fixed Gaussian AR1; one predeclared grid; not "
                                                "upstream filtered data or arbitrary red noise"},
                                      {"cases", records}})
                .toJson());
    }
}
int runCocoAnalysisAudit(int argc, char **argv)
{
    CocoAnalysisAudit t;
    return QTest::qExec(&t, argc, argv);
}
#include "coco_analysis_audit.moc"
