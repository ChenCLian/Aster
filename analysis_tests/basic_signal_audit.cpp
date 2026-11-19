// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "basic_signal_audit.h"
#include "math/smoothing/smoothing.h"
#include "math/spectrum/evolutionary_spectrum.h"
#include "math/spectrum/mtm_spectrum.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <future>
#include <limits>
using namespace aster;
namespace
{
std::vector<double> values(const QJsonValue &v)
{
    std::vector<double> x;
    for (auto a : v.toArray())
        x.push_back(a.toDouble());
    return x;
}
bool near(const std::vector<double> &a, const std::vector<double> &b, double tolerance = 5e-9)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!std::isfinite(a[i]) || std::abs(a[i] - b[i]) > 1e-10 + tolerance * std::abs(b[i]))
        {
            qWarning() << "Mismatch" << i << a[i] << b[i];
            return false;
        }
    return true;
}
SeriesData sample(int n = 256, double dt = 1)
{
    SeriesData s;
    s.xAxis = {"Age", "ka"};
    s.yAxis = {"Proxy", "u"};
    for (int i = 0; i < n; ++i)
    {
        s.x << 100 + i * dt;
        s.y << 2 + 3 * std::sin(2 * 3.14159265358979323846 * i / 16.);
    }
    return s;
}
NodeResult run(const char *key, const DataRef &source, QJsonObject p = {})
{
    const auto registry = makeScientificRegistry();
    QString error;
    auto task = registry.create(key, p, &error);
    if (!task)
        return NodeResult::failure(error);
    auto result = task->execute({source}, {});
    if (result.state == NodeState::Succeeded)
    {
        const auto invalid = registry.validateOutput(registry.find(key)->descriptor, result);
        if (!invalid.isEmpty())
            return NodeResult::failure(invalid);
    }
    return result;
}
} // namespace
class BasicSignalAudit : public QObject
{
    Q_OBJECT
  private slots:
    void references_data();
    void references();
    void polynomialAndMeanEdges();
    void isolatedSpikeOutsideWindow();
    void spectraOnLargeBaseline();
    void linearResidualOnLargeBaseline();
    void amplitudeAndLowessOffsets();
    void numericErrorsAndCancellation();
    void spectrumUnitsAndStreaming();
    void contractsAndFailures();
    void workflowDatabaseCsv();
    void defaultsAndTranslations();
    void integratedBandPower();
    void bandWorkflowAndStoredSpectrum();
};
void BasicSignalAudit::integratedBandPower()
{
    const auto registry = makeScientificRegistry();
    const SpectrumData curve{{0, 1, 2, 4}, {0, 2, 4, 8}, {"Frequency", ""}, {"Power", "PSD"}};
    TaskInput input{makeData(curve)};
    input.origin = {"mtm", "spectrum", {{"rayleigh_frequency", .25}}};
    const auto power = [&](QJsonObject parameters, ExecutionContext context = {}) {
        return registry.create("band_power", parameters)->execute(input, context);
    };
    QJsonObject params{{"band_axis", "frequency"}, {"band_min", .5}, {"band_max", 2.5}};
    const auto initial = power(params);
    QCOMPARE(initial.state, NodeState::Succeeded);
    QCOMPARE(initial.report["target_power"].toDouble(), 6.);
    QCOMPARE(initial.report["reference_power"].toDouble(), 16.);
    QCOMPARE(initial.report["fraction"].toDouble(), .375);
    QVERIFY(initial.report["period_unit"].toString().isEmpty());
    params = {{"band_min", .4}, {"band_max", 2.}, {"band_reference", "custom"},
              {"band_ref_min", .25}, {"band_ref_max", 4.}};
    const auto reciprocal = power(params);
    QCOMPARE(reciprocal.state, NodeState::Succeeded);
    QCOMPARE(reciprocal.report["target_power"].toDouble(), 6.);
    QCOMPARE(reciprocal.report["reference_power"].toDouble(), 16. - .0625);
    params["band_ref_min"] = .5;
    QCOMPARE(power(params).diagnostic().code, QString("BAND_COVERAGE"));
    params = {{"band_axis", "frequency"}, {"band_min", 1.}, {"band_max", 1.1}};
    QCOMPARE(power(params).diagnostic().code, QString("BAND_RESOLUTION"));
    params["band_max"] = 2.;
    input.origin.port = "ar1_95";
    QCOMPARE(power(params).diagnostic().code, QString("BAND_PSD_INPUT"));
    input.origin.port = "spectrum";
    auto zero = curve; zero.power.fill(0);
    input.data = makeData(zero);
    QCOMPARE(power(params).diagnostic().code, QString("BAND_ZERO_POWER"));
    ExecutionContext cancelled; cancelled.cancellation.cancel();
    QCOMPARE(power(params, cancelled).state, NodeState::Cancelled);
}
void BasicSignalAudit::bandWorkflowAndStoredSpectrum()
{
    auto s = sample(256);
    for (int i = 0; i < s.y.size(); ++i)
        s.y[i] = 2 * std::sin(2 * std::acos(-1.) * i / 16.) + std::sin(2 * std::acos(-1.) * i / 8.);
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0,0}).nodeId;
    const auto spec = model.addChild(root, ChildSlot::Append, "fft_spectrum", {{"psd_taper", "rectangular"}}).nodeId;
    const QJsonObject band{{"band_axis", "frequency"}, {"band_min", 15./256}, {"band_max", 17./256}};
    const auto node = model.addChild(spec, ChildSlot::Append, "band_power", band).nodeId;
    const auto result = WorkflowExecutor{}.run({model.freeze().snapshot, root, {"demo", makeData(s), {}}});
    QCOMPARE(result.state, RunState::Succeeded);
    const auto calc = result.nodes[node];
    QVERIFY(std::abs(calc.report["fraction"].toDouble() - .8) < 1e-10);
    QCOMPARE(calc.report["period_unit"].toString(), QString("ka"));
    QTemporaryDir dir; const auto path = dir.filePath("bands.sqlite");
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    const auto saved = repo.save(result); QVERIFY(saved.saved());
    OutputReference ref{path, saved.recordId, result.runId, spec, "spectrum", "Spectrum"};
    WorkflowModel reused(makeScientificRegistry());
    const auto db = reused.addRoot("database_spectrum", {0,0}).nodeId;
    const auto child = reused.addChild(db, ChildSlot::Append, "band_power", band).nodeId;
    const auto reread = WorkflowExecutor{}.run({reused.freeze().snapshot, db, {"saved", {}, {{"database_source", ref.toJson()}}}});
    QCOMPARE(reread.state, RunState::Succeeded);
    QCOMPARE(encodeData(reread.nodes[child].outputs["band_power"]).bytes, encodeData(calc.outputs["band_power"]).bytes);
}
void BasicSignalAudit::references_data()
{
    QTest::addColumn<QJsonObject>("c");
    QFile f(QFINDTESTDATA("fixtures/basic_signal_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    int i = 0;
    for (auto c : QJsonDocument::fromJson(f.readAll()).object()["cases"].toArray())
        QTest::newRow(qPrintable(c.toObject()["kind"].toString() + QString::number(i++)))
            << c.toObject();
}
void BasicSignalAudit::references()
{
    QFETCH(QJsonObject, c);
    const auto kind = c["kind"].toString();
    const auto y = values(c["y"]);
    if (kind == "sg" || kind == "mean")
    {
        const auto result =
            numeric::smoothSeries(y, c["window"].toInt(), c["degree"].toInt(),
                                  kind == "sg" ? numeric::SmoothingMethod::SavitzkyGolay
                                               : numeric::SmoothingMethod::MovingMean);
        QVERIFY(near(result, values(c["result"])));
    }
    else
    {
        auto s = sample(int(y.size()), c["dt"].toDouble());
        s.y = QVector<double>(y.begin(), y.end());
        QJsonObject p{{"psd_nfft", c["nfft"]},
                      {"psd_taper", c["taper"] == "boxcar" ? "rectangular" : "hann"},
                      {"psd_trend", c["trend"] == "linear" ? "linear" : "mean"}};
        if (kind == "welch")
        {
            p["psd_window"] = c["window"];
            p["psd_overlap"] = c["overlap"];
        }
        const auto result = run(kind == "fft" ? "fft_spectrum" : "welch_spectrum", makeData(s), p);
        QCOMPARE(result.state, NodeState::Succeeded);
        const auto *data = dataAs<SpectrumData>(result.outputs["spectrum"]);
        QVERIFY(data);
        QVERIFY(near(std::vector<double>(data->frequency.begin(), data->frequency.end()),
                     values(c["frequency"])));
        QVERIFY(
            near(std::vector<double>(data->power.begin(), data->power.end()), values(c["power"])));
    }
}
void BasicSignalAudit::polynomialAndMeanEdges()
{
    using namespace numeric;
    QVERIFY(near(smoothSeries({1, 2, 3, 4, 5}, 3, 0, SmoothingMethod::MovingMean),
                 {1.5, 2, 3, 4, 4.5}, 1e-13));
    for (int degree = 0; degree <= 5; ++degree)
        for (int w : {11, 51, 501})
        {
            std::vector<double> y(601);
            for (std::size_t i = 0; i < y.size(); ++i)
            {
                const double x = double(i) / 600;
                y[i] = std::pow(x, degree) + .3;
            }
            QVERIFY(near(smoothSeries(y, w, degree, SmoothingMethod::SavitzkyGolay), y, 2e-12));
        }
    for (auto method : {SmoothingMethod::MovingMean, SmoothingMethod::SavitzkyGolay})
    {
        const std::vector<double> constant(31, 1.7e308);
        QVERIFY(smoothSeries(constant, 11, 2, method) == constant);
        QVERIFY(near(smoothSeries({1, 2, 3}, 1, 0, method), {1, 2, 3}));
    }
}
void BasicSignalAudit::isolatedSpikeOutsideWindow()
{
    std::vector<double> values(48);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = (1 + i % 3) * 1e-200;
    auto withSpike = values;
    withSpike.back() = 1e300;
    for (auto method : {numeric::SmoothingMethod::MovingMean, numeric::SmoothingMethod::SavitzkyGolay}) {
        const auto base = numeric::smoothSeries(values, 11, 2, method);
        const auto affected = numeric::smoothSeries(withSpike, 11, 2, method);
        for (size_t i = 0; i < 32; ++i) {
            QVERIFY(base[i] > 0);
            QCOMPARE(affected[i], base[i]);
        }
    }
}
void BasicSignalAudit::spectraOnLargeBaseline()
{
    const double cycle[] = {0, 2, 4, 2};
    std::vector<double> base(1151), shifted(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        base[i] = cycle[i % 4];
        shifted[i] = 1e16 + base[i];
    }
    for (auto method : {numeric::EvolutionMethod::Fft, numeric::EvolutionMethod::Mtm}) {
        for (auto trend : {numeric::SegmentTrend::Mean, numeric::SegmentTrend::Linear}) {
            numeric::EvolutionOptions o;
            o.method = method; o.trend = trend; o.windowSamples = 256; o.stepSamples = 128;
            const auto a = numeric::evolutionarySpectrum(base, 1, o);
            const auto b = numeric::evolutionarySpectrum(shifted, 1, o);
            QVERIFY(near(a.power, b.power));
            QVERIFY(near(a.meanPower, b.meanPower));
            if (method == numeric::EvolutionMethod::Fft && trend == numeric::SegmentTrend::Mean)
                QVERIFY(std::abs(b.meanPower[64] - 1024. / 3) < 1e-10);
        }
    }
    // An odd record length exposes accumulated DC error in the MTM mean.
    const auto a = numeric::mtmSpectrum(base, 1, 2, 3, .5);
    const auto b = numeric::mtmSpectrum(shifted, 1, 2, 3, .5);
    QVERIFY(near(a.power, b.power));
}
void BasicSignalAudit::linearResidualOnLargeBaseline()
{
    const QVector<double> residual{-0.8, 1.2, -0.8, 1.2, -0.8};
    for (double baseline : {0., 1e16}) {
        SeriesData s{{0,1,2,3,4}, {baseline,baseline+2,baseline,baseline+2,baseline}, {}, {}};
        const auto result = run("linear_detrend", makeData(s));
        QCOMPARE(result.state, NodeState::Succeeded);
        const auto actual = dataAs<SeriesData>(result.outputs["detrended"])->y;
        for (int i = 0; i < residual.size(); ++i)
            QVERIFY(std::abs(actual[i] - residual[i]) < 1e-14);
    }
}
void BasicSignalAudit::amplitudeAndLowessOffsets()
{
    auto extreme = sample(32);
    extreme.y.fill(1e-200);
    extreme.y.back() = 1e300;
    const auto rejected = run("detrend_lowess", makeData(extreme));
    QCOMPARE(rejected.diagnostic().code, QString("NUMERIC_RANGE"));
    QVERIFY(rejected.outputs.isEmpty());
    for (int n : {31, 3001}) {
        auto a = sample(n), b = a;
        for (int i = 0; i < n; ++i) {
            a.y[i] = 2 * ((i % 7) - 3);
            b.y[i] = a.y[i] + 1e16;
        }
        for (const char* key : {"demean", "standardize", "hilbert_envelope", "detrend_lowess"}) {
            const QString task = key;
            const QString port = task == "hilbert_envelope" ? "signal" : task == "detrend_lowess" ? "detrended" : "series";
            auto parameters = task == "hilbert_envelope" ? QJsonObject{{"hilbert_remove_mean",true}} : QJsonObject{};
            const auto verify = [&] {
                const auto x = run(key, makeData(a), parameters), y = run(key, makeData(b), parameters);
                QCOMPARE(x.state, NodeState::Succeeded); QCOMPARE(y.state, NodeState::Succeeded);
                const auto v = dataAs<SeriesData>(x.outputs[port]), w = dataAs<SeriesData>(y.outputs[port]);
                QVERIFY(v && w);
                for (int i=0; i<n; ++i) QVERIFY(std::abs(v->y[i]-w->y[i])<1e-8);
            };
            verify();
            if (task == "detrend_lowess" && n == 31) {
                parameters["iterations"] = 3;
                verify();
            }
        }
    }
}
void BasicSignalAudit::numericErrorsAndCancellation()
{
    using namespace numeric;
    const std::vector<double> y(31, 1.);
    for (auto method : {SmoothingMethod::MovingMean, SmoothingMethod::SavitzkyGolay})
    {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, smoothSeries(y, 4, 2, method));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, smoothSeries(y, 33, 2, method));
        QVERIFY_THROWS_EXCEPTION(SmoothingCancelled,
                                 smoothSeries(y, 11, 2, method, [] { return true; }));
        auto bad = y;
        bad[4] = std::numeric_limits<double>::quiet_NaN();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, smoothSeries(bad, 11, 2, method));
    }
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             smoothSeries(y, 3, 3, SmoothingMethod::SavitzkyGolay));
    QVERIFY_THROWS_EXCEPTION(std::length_error, smoothSeries(std::vector<double>(503, 1), 503, 2,
                                                             SmoothingMethod::SavitzkyGolay));
    QVERIFY_THROWS_EXCEPTION(std::length_error, smoothSeries(std::vector<double>(200000, 1), 501, 2,
                                                             SmoothingMethod::SavitzkyGolay));
    QVERIFY_THROWS_EXCEPTION(std::overflow_error, smoothSeries({1.7e308, 1.7e308, -1.7e308}, 3, 1,
                                                               SmoothingMethod::SavitzkyGolay));
    int polls = 0;
    auto data = sample(5000);
    std::vector<double> v(data.y.begin(), data.y.end());
    QVERIFY_THROWS_EXCEPTION(
        SmoothingCancelled,
        smoothSeries(v, 51, 3, SmoothingMethod::SavitzkyGolay, [&] { return ++polls > 8; }));
}
void BasicSignalAudit::spectrumUnitsAndStreaming()
{
    for (double dt : {1., .25})
        for (const auto *key : {"fft_spectrum", "welch_spectrum"})
        {
            auto result = run(key, makeData(sample(512, dt)), {{"psd_taper", "rectangular"}});
            QCOMPARE(result.state, NodeState::Succeeded);
            const auto *p = dataAs<SpectrumData>(result.outputs["spectrum"]);
            QVERIFY(p);
            double integral = 0;
            for (double v : p->power)
                integral += v * (p->frequency[1] - p->frequency[0]);
            QVERIFY(std::abs(integral - 4.5) < 1e-10);
            const auto max = std::max_element(p->power.begin(), p->power.end());
            QVERIFY(std::abs(p->frequency[std::distance(p->power.begin(), max)] - 1 / (16 * dt)) <
                    1e-12);
            QCOMPARE(p->frequencyAxis.unit, QString("1/ka"));
            QCOMPARE(p->powerAxis.unit, QString("(u)^2 * (ka)"));
        }
    numeric::EvolutionOptions o;
    o.windowSamples = 32;
    o.stepSamples = 16;
    auto s = sample();
    std::vector<double> y(s.y.begin(), s.y.end());
    auto map = numeric::evolutionarySpectrum(y, 1, o);
    auto mean = numeric::evolutionarySpectrum(y, 1, o, {}, {}, false);
    QVERIFY(mean.power.empty());
    QVERIFY(map.meanPower == mean.meanPower);
    QVERIFY(map.frequency == mean.frequency);
    QVERIFY(map.centers == mean.centers);
    // The mean-only path may exceed the map cell limit without allocating a map.
    y.resize(8192);
    for (size_t i = 0; i < y.size(); ++i)
        y[i] = std::sin(.12 * i);
    o.windowSamples = 1024;
    o.stepSamples = 1;
    QVERIFY_THROWS_EXCEPTION(std::length_error, numeric::evolutionarySpectrum(y, 1, o));
    mean = numeric::evolutionarySpectrum(y, 1, o, {}, {}, false);
    QVERIFY(mean.power.empty());
    QVERIFY(!mean.meanPower.empty());
}
void BasicSignalAudit::contractsAndFailures()
{
    const auto source = makeData(sample());
    for (const auto *key : {"moving_average", "savitzky_golay", "fft_spectrum", "welch_spectrum"})
    {
        auto result = run(key, source);
        QCOMPARE(result.state, NodeState::Succeeded);
        QVERIFY(!result.warnings.isEmpty());
        auto grid = makeData(GridData{{0, 1}, {0, 1}, {1, 2, 3, 4}, 2, 2, {}, {}, {}});
        QCOMPARE(run(key, grid).state, NodeState::Failed);
        auto s = sample();
        s.x[12] = s.x[11];
        result = run(key, makeData(s));
        QCOMPARE(result.state, NodeState::Failed);
        QVERIFY(!result.diagnostic().isEmpty());
    }
    auto s = sample();
    s.x[20] += .1;
    auto m = run("moving_average", makeData(s));
    QCOMPARE(m.state, NodeState::Succeeded);
    QVERIFY(m.warnings.contains("warning.smoothing_point_window"));
    for (const auto *key : {"savitzky_golay", "fft_spectrum", "welch_spectrum"})
        QCOMPARE(run(key, makeData(s)).state, NodeState::Failed);
    QCOMPARE(run("welch_spectrum", source, {{"psd_window", 64}, {"psd_overlap", 64}}).state,
             NodeState::Failed);
    QCOMPARE(run("fft_spectrum", source, {{"psd_nfft", 128}}).state, NodeState::Failed);
    QCOMPARE(run("savitzky_golay", source, {{"smooth_window", 3}, {"smooth_degree", 3}}).state,
             NodeState::Failed);
    const auto before = encodeData(source);
    for (auto v : dataAs<SeriesData>(source)->y)
        QVERIFY(std::isfinite(v));
    QCOMPARE(before.bytes, encodeData(source).bytes);
}
void BasicSignalAudit::workflowDatabaseCsv()
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("input_series", {0, 0});
    QVERIFY(root.ok());
    auto mean = model.addChild(root.nodeId, ChildSlot::Right, "moving_average");
    auto sg = model.addChild(root.nodeId, ChildSlot::Down, "savitzky_golay");
    QVERIFY(mean.ok() && sg.ok());
    auto fft = model.addChild(mean.nodeId, ChildSlot::Right, "fft_spectrum");
    auto welch = model.addChild(sg.nodeId, ChildSlot::Right, "welch_spectrum");
    QVERIFY(fft.ok() && welch.ok());
    auto source = makeData(sample());
    const auto original = encodeData(source).bytes;
    auto result =
        WorkflowExecutor{}.run({model.freeze().snapshot, root.nodeId, {"signal.csv", source, {}}});
    QCOMPARE(result.state, RunState::Succeeded);
    QCOMPARE(encodeData(source).bytes, original);
    QTemporaryDir dir;
    ResultRepository db;
    QVERIFY(db.open(dir.filePath("test.sqlite")).isEmpty());
    const auto saved = db.save(result);
    QVERIFY(saved.saved());
    const auto loaded = db.load(saved.recordId);
    QVERIFY(loaded.ok());
    for (const auto &id : {mean.nodeId, sg.nodeId, fft.nodeId, welch.nodeId})
        for (auto it = result.nodes[id].outputs.begin(); it != result.nodes[id].outputs.end(); ++it)
        {
            QCOMPARE(encodeData(loaded.run.nodes[id].outputs[it.key()]).bytes,
                     encodeData(it.value()).bytes);
            const auto path = dir.filePath(id.toString() + it.key() + ".csv");
            QVERIFY(exportDataCsv(it.value(), path).ok());
            QVERIFY(QFileInfo(path).size() > 100);
        }
    QVERIFY(model.setParameters(sg.nodeId, {{"smooth_window", 4}}).ok());
    result =
        WorkflowExecutor{}.run({model.freeze().snapshot, root.nodeId, {"bad.csv", source, {}}});
    QCOMPARE(result.nodes[sg.nodeId].state, NodeState::Failed);
    QCOMPARE(result.nodes[welch.nodeId].state, NodeState::Blocked);
    QCOMPARE(result.nodes[fft.nodeId].state, NodeState::Succeeded);
}
void BasicSignalAudit::defaultsAndTranslations()
{
    const auto registry = makeScientificRegistry();
    for (const auto *language : {"en", "zh"})
    {
        QFile file(QFINDTESTDATA(qPrintable(QString("../resources/i18n/") + language + ".json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto words = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto *key :
             {"moving_average", "savitzky_golay", "fft_spectrum", "welch_spectrum"})
        {
            const auto *entry = registry.find(key);
            QVERIFY(entry);
            QVERIFY(words.contains("method." + QString(key)));
            const auto normalized = registry.normalize(key, {});
            QVERIFY(normalized.validation.ok());
            for (const auto &p : entry->descriptor.parameters)
            {
                QCOMPARE(normalized.parameters[p.key], p.defaultValue);
                QVERIFY(words.contains("parameter." + p.key));
                QVERIFY(words.contains("parametergroup." + p.group));
                QVERIFY(words.contains("parameterhelp." + p.key));
                for (const auto &value : p.choices)
                    QVERIFY(words.contains("choice." + p.key + '.' + value));
            }
        }
    }
}
int runBasicSignalAudit(int argc, char **argv)
{
    BasicSignalAudit tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "basic_signal_audit.moc"
