// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "evolution_audit.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <future>
#include <limits>
#include "processing/builtin_tasks.h"
#include "processing/evolutionary_tasks.h"
#include "processing/series_utility_tasks.h"
#include "processing/task_categories.h"
#include "math/spectrum/evolutionary_spectrum.h"
#include "math/spectrum/mtm_spectrum.h"
#include "math/series/series_operations.h"
#include "core/workflow_executor.h"
#include "io/database/data_codec.h"
#include "io/database/result_repository.h"
using namespace aster;
namespace
{
std::vector<double> values(const QJsonValue &v)
{
    std::vector<double> out;
    for (const auto &n : v.toArray())
        out.push_back(n.toDouble());
    return out;
}
bool close(const std::vector<double> &actual, const std::vector<double> &expected,
           double tol = 2e-7)
{
    if (actual.size() != expected.size())
        return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (!std::isfinite(actual[i]) ||
            std::abs(actual[i] - expected[i]) > 1e-11 + tol * std::abs(expected[i]))
        {
            qWarning() << "Mismatch at" << i << actual[i] << expected[i];
            return false;
        }
    return true;
}
template <class T> void add(TaskRegistry &r, const char *key)
{
    auto d = T::descriptor();
    d.key = key;
    const auto v = r.add(d, [](const QJsonObject &p) { return std::make_unique<T>(p); });
    if (!v.ok())
        throw std::runtime_error(v.message().toStdString());
}
TaskRegistry registry()
{
    auto r = makeBuiltinRegistry();
    add<EvolutionFftTask>(r, "evolution_fft");
    add<EvolutionMtmTask>(r, "evolution_mtm");
    add<SelectIntervalTask>(r, "select_interval");
    add<DemeanTask>(r, "demean");
    add<StandardizeTask>(r, "standardize");
    add<SamplingCheckTask>(r, "sampling_check");
    return r;
}
SeriesData sample(int n = 256)
{
    SeriesData s;
    s.xAxis = {"Age", "ka"};
    s.yAxis = {"Proxy", "permil"};
    for (int i = 0; i < n; ++i)
    {
        s.x.push_back(100 + i * .25);
        s.y.push_back(3 + std::sin(i * .4) + .2 * std::cos(i * .9));
    }
    return s;
}
NodeResult run(const char *key, DataRef data, QJsonObject p = {})
{
    auto r = registry();
    QString error;
    auto t = r.create(key, p, &error);
    if (!t)
        return NodeResult::failure(error);
    auto out = t->execute({data}, {});
    if (out.state == NodeState::Succeeded)
    {
        const auto invalid = r.validateOutput(r.find(key)->descriptor, out);
        if (!invalid.isEmpty())
            return NodeResult::failure(invalid);
    }
    return out;
}
} // namespace
class EvolutionAudit : public QObject
{
    Q_OBJECT
  private slots:
    void references_data();
    void references();
    void physicalUnitsAndEndpoints();
    void chirpAndWindowPlacement();
    void matchesExistingMtm();
    void invalidNumericsAndCancellation();
    void taskDefaultsAndTypes();
    void absoluteLimitsAndLegacyWorkflows();
    void windowNormalization();
    void intervalSelection();
    void centeringAndStandardizing();
    void qualityInspection();
    void workflowDatabaseAndFailure();
    void concurrentSources();
    void translationsAndCategories();
};
void EvolutionAudit::references_data()
{
    QTest::addColumn<QJsonObject>("c");
    QFile f(QFINDTESTDATA("fixtures/evolution_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    for (const auto &v : QJsonDocument::fromJson(f.readAll()).object()["cases"].toArray())
    {
        auto c = v.toObject();
        QTest::newRow(qPrintable(c["name"].toString())) << c;
    }
}
void EvolutionAudit::references()
{
    QFETCH(QJsonObject, c);
    numeric::EvolutionOptions o;
    o.method = c["method"] == "mtm" ? numeric::EvolutionMethod::Mtm : numeric::EvolutionMethod::Fft;
    o.windowSamples = c["length"].toInt();
    o.stepSamples = c["step"].toInt();
    o.fftLength = c["nfft"].toInt();
    o.nw = c["nw"].toDouble();
    o.maximumFrequencyRatio = c["ratio"].toDouble();
    o.window = c["taper"] == "boxcar" ? numeric::FftWindow::Rectangular : numeric::FftWindow::Hann;
    o.trend = c["trend"] == "linear" ? numeric::SegmentTrend::Linear : numeric::SegmentTrend::Mean;
    const auto r = numeric::evolutionarySpectrum(values(c["input"]), c["dt"].toDouble(), o);
    QVERIFY(close(r.frequency, values(c["frequency"])));
    QVERIFY(close(r.centers, values(c["centers"])));
    QVERIFY(close(r.power, values(c["power"])));
    QVERIFY(close(r.meanPower, values(c["mean_power"])));
    QCOMPARE(r.unusedTail, size_t(c["tail"].toInt()));
    QCOMPARE(r.unconvergedWindows, 0);
}
void EvolutionAudit::physicalUnitsAndEndpoints()
{
    numeric::EvolutionOptions o;
    o.windowSamples = 64;
    o.stepSamples = 64;
    o.window = numeric::FftWindow::Rectangular;
    std::vector<double> y(64);
    for (int i = 0; i < 64; ++i)
        y[i] = 2 * std::sin(2 * M_PI * i * 5 / 64.);
    auto r = numeric::evolutionarySpectrum(y, .5, o);
    double integral = 0;
    for (double p : r.power)
        integral += p * (2. / 64);
    QVERIFY(std::abs(integral - 2) < 1e-12);
    QCOMPARE(r.power.size(), size_t(33));
    auto stretched = numeric::evolutionarySpectrum(y, 1., o);
    for (size_t i = 0; i < r.power.size(); ++i)
    {
        QVERIFY(std::abs(stretched.power[i] - 2 * r.power[i]) < 1e-10);
        QCOMPARE(stretched.frequency[i], r.frequency[i] / 2);
    }
    for (int i = 0; i < 64; ++i)
        y[i] = i % 2 ? -1 : 1;
    auto nyquist = numeric::evolutionarySpectrum(y, .5, o);
    QVERIFY(std::abs(nyquist.power.back() * (2. / 64) - 1) < 1e-12);
    for (double &v : y)
        v = 1.7;
    for (auto taper : {numeric::FftWindow::Hann, numeric::FftWindow::Rectangular})
    {
        o.window = taper;
        auto constant = numeric::evolutionarySpectrum(y, .5, o);
        for (double p : constant.power)
            QCOMPARE(p, 0.);
    }
}
void EvolutionAudit::chirpAndWindowPlacement()
{
    numeric::EvolutionOptions o;
    o.windowSamples = 64;
    o.stepSamples = 32;
    std::vector<double> y(257);
    for (size_t i = 0; i < y.size(); ++i)
        y[i] = std::sin(2 * M_PI * (.04 * i + .0003 * i * i));
    auto r = numeric::evolutionarySpectrum(y, 1, o);
    QCOMPARE(r.centers.front(), 31.5);
    QCOMPARE(r.unusedTail, size_t(1));
    double previous = 0;
    for (size_t w = 0; w < r.centers.size(); ++w)
    {
        size_t peak = 1;
        for (size_t f = 2; f < r.frequency.size(); ++f)
            if (r.power[f * r.centers.size() + w] > r.power[peak * r.centers.size() + w])
                peak = f;
        QVERIFY(r.frequency[peak] >= previous);
        QVERIFY(std::abs(r.frequency[peak] - (.04 + .0006 * r.centers[w])) < .025);
        previous = r.frequency[peak];
    }
}
void EvolutionAudit::matchesExistingMtm()
{
    auto s = sample(196);
    numeric::EvolutionOptions o;
    o.method = numeric::EvolutionMethod::Mtm;
    o.windowSamples = 48;
    o.stepSamples = 24;
    o.fftLength = 96;
    const std::vector<double> y(s.y.begin(), s.y.end());
    auto r = numeric::evolutionarySpectrum(y, .25, o);
    for (size_t w = 0; w < r.centers.size(); ++w)
    {
        std::vector<double> segment(y.begin() + w * 24, y.begin() + w * 24 + 48);
        const auto direct = numeric::mtmSpectrum(segment, 4, 2, 3, 2, {}, 96,
                                                 numeric::MtmConvergenceDomain::FullSpectrum);
        for (size_t f = 0; f < r.frequency.size(); ++f)
            QVERIFY(std::abs(r.power[f * r.centers.size() + w] - direct.power[f]) < 1e-10);
    }
}
void EvolutionAudit::invalidNumericsAndCancellation()
{
    numeric::EvolutionOptions o;
    o.windowSamples = 16;
    o.stepSamples = 8;
    std::vector<double> y(32, 1);
    auto invalid = o;
    invalid.stepSamples = 0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::evolutionarySpectrum(y, 1, invalid));
    invalid = o;
    invalid.fftLength = 8;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::evolutionarySpectrum(y, 1, invalid));
    invalid = o;
    invalid.method = numeric::EvolutionMethod::Mtm;
    invalid.nw = 8;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::evolutionarySpectrum(y, 1, invalid));
    invalid = o;
    invalid.maximumFrequencyRatio = .001;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::evolutionarySpectrum(y, 1, invalid));
    y[3] = std::numeric_limits<double>::quiet_NaN();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::evolutionarySpectrum(y, 1, o));
    y[3] = 1;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::evolutionarySpectrum(y, 0, o));
    bool cancel = false;
    int calls = 0;
    QVERIFY_THROWS_EXCEPTION(numeric::EvolutionCancelled, numeric::evolutionarySpectrum(
                                                              y, 1, o, [&] { return cancel; },
                                                              [&](int)
                                                              {
                                                                  cancel = true;
                                                                  ++calls;
                                                              }));
    QCOMPARE(calls, 1);
    std::vector<double> tiny(32);
    for (size_t i = 0; i < tiny.size(); ++i)
        tiny[i] = 1e-200 * std::sin(i * .3);
    QVERIFY_THROWS_EXCEPTION(std::overflow_error, numeric::evolutionarySpectrum(tiny, 1, o));
    invalid = o;
    invalid.windowSamples = 1024;
    invalid.stepSamples = 1;
    QVERIFY_THROWS_EXCEPTION(std::length_error, numeric::evolutionarySpectrum(
                                                    std::vector<double>(50000, 1), 1, invalid));
}
void EvolutionAudit::taskDefaultsAndTypes()
{
    auto reg = registry();
    auto normalized = reg.normalize("evolution_fft", {});
    QVERIFY(normalized.validation.ok());
    QCOMPARE(normalized.parameters["evo_window"].toInt(), 128);
    QCOMPARE(normalized.parameters["evo_step"].toInt(), 16);
    auto result = run("evolution_fft", makeData(sample()));
    QCOMPARE(result.state, NodeState::Succeeded);
    const auto *g = dataAs<GridData>(result.outputs["power_map"]);
    QVERIFY(g);
    QCOMPARE(g->columns, qsizetype(9));
    QCOMPARE(g->rows, qsizetype(65));
    QCOMPARE(g->x.front(), 100 + 127 * .25 / 2);
    QCOMPARE(g->xAxis.unit, QString("ka"));
    QCOMPARE(g->yAxis.unit, QString("1/ka"));
    QCOMPARE(g->valueAxis.unit, QString("(permil)^2 * (ka)"));
    auto encoded = encodeData(result.outputs["power_map"]);
    QVERIFY(encoded.ok());
    auto decoded = decodeData(encoded.bytes);
    QVERIFY(decoded.ok());
    QCOMPARE(dataAs<GridData>(decoded.data)->values, g->values);
    QCOMPARE(run("evolution_fft", result.outputs["power_map"]).state, NodeState::Failed);
    auto s = sample();
    s.x[4] = s.x[3];
    QCOMPARE(run("evolution_fft", makeData(s)).diagnostic().code, QString("INPUT_X_DUPLICATE"));
    s = sample();
    s.x[4] += .01;
    QCOMPARE(run("evolution_mtm", makeData(s)).state, NodeState::Failed);
    QVERIFY(!reg.normalize("evolution_fft", {{"evo_taper", "unknown"}}).validation.ok());
    QVERIFY(!reg.normalize("select_interval", {{"interval_action", "unknown"}}).validation.ok());
}
void EvolutionAudit::absoluteLimitsAndLegacyWorkflows()
{
    const auto data = makeData(sample()); // spacing .25, Nyquist 2
    for (const char *method : {"evolution_fft", "evolution_mtm"})
    {
        const auto full = run(method, data);
        const auto legacy = run(method, data, {{"evo_frequency_ratio", .5}});
        const auto absolute = run(method, data, {{"frequency_max", 1}, {"evo_frequency_ratio", .2}});
        QCOMPARE(absolute.state, NodeState::Succeeded);
        QCOMPARE(dataAs<GridData>(absolute.outputs["power_map"])->values,
                 dataAs<GridData>(legacy.outputs["power_map"])->values);
        QCOMPARE(absolute.report["frequency_limit_mode"].toString(), QString("absolute"));
        QCOMPARE(legacy.report["frequency_limit_mode"].toString(), QString("legacy_ratio"));
        const auto clamped = run(method, data, {{"frequency_max", 100}});
        QCOMPARE(dataAs<GridData>(clamped.outputs["power_map"])->values,
                 dataAs<GridData>(full.outputs["power_map"])->values);
        QCOMPARE(run(method, data, {{"frequency_max", .00001}}).state, NodeState::Failed);
        QCOMPARE(run(method, data, {{"frequency_max", -1}}).state, NodeState::Failed);
        const auto cropped = dataAs<GridData>(absolute.outputs["power_map"]);
        const auto original = dataAs<GridData>(full.outputs["power_map"]);
        QCOMPARE(cropped->values, original->values.mid(0, cropped->values.size()));
        QVERIFY(!full.report["normalization"].toString().contains("maximum"));
    }
    auto s = sample(705);
    for (qsizetype i = 0; i < s.x.size(); ++i) s.x[i] = 4 + .2 * i;
    const auto fine = run("evolution_fft", makeData(s),
        {{"evo_window", 100}, {"evo_step", 1}, {"evo_nfft", 400}, {"frequency_max", 2}});
    QCOMPARE(fine.state, NodeState::Succeeded);
    const auto grid = dataAs<GridData>(fine.outputs["power_map"]);
    QCOMPARE(grid->columns, qsizetype(606));
    QCOMPARE(grid->rows, qsizetype(161));
}
void EvolutionAudit::windowNormalization()
{
    for (const char *method : {"evolution_fft", "evolution_mtm"})
    {
        const auto s = sample();
        const QJsonObject options{{"evo_window", 64}, {"evo_step", 32}, {"evo_nfft", 128}};
        auto normalizedOptions = options;
        normalizedOptions["evo_normalize"] = true;
        const auto raw = run(method, makeData(s), options);
        const auto normalized = run(method, makeData(s), normalizedOptions);
        QCOMPARE(normalized.state, NodeState::Succeeded);
        const auto a = dataAs<GridData>(raw.outputs["power_map"]);
        const auto b = dataAs<GridData>(normalized.outputs["power_map"]);
        const auto mean = dataAs<SpectrumData>(normalized.outputs["mean_spectrum"]);
        QCOMPARE(b->valueAxis.name, QString("Relative power"));
        QCOMPARE(b->valueAxis.unit, QString("1"));
        QCOMPARE(mean->powerAxis.unit, QString("1"));
        for (qsizetype c = 0; c < a->columns; ++c)
        {
            double peak = 0, normalizedPeak = 0;
            for (qsizetype f = 0; f < a->rows; ++f)
                peak = std::max(peak, a->values[f * a->columns + c]);
            for (qsizetype f = 0; f < a->rows; ++f)
            {
                const auto i = f * a->columns + c;
                QVERIFY(std::abs(b->values[i] - a->values[i] / peak) < 1e-12);
                normalizedPeak = std::max(normalizedPeak, b->values[i]);
            }
            QCOMPARE(normalizedPeak, 1.);
        }
        for (qsizetype f = 0; f < b->rows; ++f)
        {
            double sum = 0;
            for (qsizetype c = 0; c < b->columns; ++c) sum += b->values[f * b->columns + c];
            QVERIFY(std::abs(mean->power[f] - sum / b->columns) < 1e-12);
        }
        // Cropping must not renormalize the visible band, even below the main peak.
        normalizedOptions["frequency_max"] = .125;
        const auto cropped = run(method, makeData(s), normalizedOptions);
        QCOMPARE(cropped.state, NodeState::Succeeded);
        const auto g = dataAs<GridData>(cropped.outputs["power_map"]);
        QCOMPARE(g->values, b->values.mid(0, g->values.size()));
        QVERIFY(*std::max_element(g->values.begin(), g->values.end()) < 1);
        auto scaled = s;
        for (auto &v : scaled.y) v *= 7;
        const auto scaledResult = run(method, makeData(scaled), normalizedOptions);
        QCOMPARE(scaledResult.state, NodeState::Succeeded);
        const auto scaledGrid = dataAs<GridData>(scaledResult.outputs["power_map"]);
        for (qsizetype i = 0; i < g->values.size(); ++i)
            QVERIFY(std::abs(g->values[i] - scaledGrid->values[i]) < 1e-9);
        auto zero = s;
        std::fill(zero.y.begin(), zero.y.end(), 0.);
        const auto zeros = run(method, makeData(zero), normalizedOptions);
        QCOMPARE(zeros.state, NodeState::Succeeded);
        const auto zg = dataAs<GridData>(zeros.outputs["power_map"]);
        QCOMPARE(zeros.report["zero_power_windows"].toInt(), int(zg->columns));
        for (const double v : zg->values) QCOMPARE(v, 0.);
        const auto encoded = encodeData(normalized.outputs["power_map"]);
        QVERIFY(encoded.ok());
        const auto decoded = decodeData(encoded.bytes);
        QVERIFY(decoded.ok());
        QCOMPARE(dataAs<GridData>(decoded.data)->valueAxis.unit, QString("1"));
        QCOMPARE(dataAs<GridData>(decoded.data)->values, b->values);
    }
}
void EvolutionAudit::intervalSelection()
{
    SeriesData s{{3, 1, 2, 2, 4}, {30, 10, 20, 21, 40}, {"Depth", "m"}, {"Y", "u"}};
    auto r = run("select_interval", makeData(s), {{"interval_lower", 2}, {"interval_upper", 3}});
    QCOMPARE(r.state, NodeState::Succeeded);
    auto p = dataAs<SeriesData>(r.outputs["series"]);
    QCOMPARE(p->x, QVector<double>({3, 2, 2}));
    QCOMPARE(p->y, QVector<double>({30, 20, 21}));
    r = run("select_interval", makeData(s),
            {{"interval_lower", 2}, {"interval_upper", 3}, {"interval_action", "remove"}});
    QCOMPARE(dataAs<SeriesData>(r.outputs["series"])->x, QVector<double>({1, 4}));
    QVERIFY(!r.warnings.isEmpty());
    QCOMPARE(run("select_interval", makeData(s), {{"interval_lower", 8}, {"interval_upper", 9}})
                 .diagnostic()
                 .code,
             QString("SELECTION_EMPTY"));
    QCOMPARE(run("select_interval", makeData(s), {{"interval_lower", 3}, {"interval_upper", 2}})
                 .diagnostic()
                 .code,
             QString("SELECTION_INTERVAL"));
    auto one = run("select_interval", makeData(s), {{"interval_lower", 4}, {"interval_upper", 4}});
    QCOMPARE(one.state, NodeState::Succeeded);
    QCOMPARE(run("evolution_fft", one.outputs["series"]).state, NodeState::Failed);
}
void EvolutionAudit::centeringAndStandardizing()
{
    SeriesData s{{0, 1, 2, 3}, {1, 2, 3, 4}, {"T", "s"}, {"Voltage", "V"}};
    auto r = run("demean", makeData(s));
    auto p = dataAs<SeriesData>(r.outputs["series"]);
    QVERIFY(p);
    QCOMPARE(p->y, QVector<double>({-1.5, -.5, .5, 1.5}));
    QCOMPARE(p->yAxis.unit, QString("V"));
    for (auto mode : {"sample", "population"})
    {
        r = run("standardize", makeData(s), {{"standardize_sd", mode}});
        p = dataAs<SeriesData>(r.outputs["series"]);
        QVERIFY(p);
        double sum = 0, square = 0;
        for (double v : p->y)
        {
            sum += v;
            square += v * v;
        }
        QVERIFY(std::abs(sum) < 1e-14);
        QVERIFY(std::abs(square - (QString(mode) == "sample" ? 3 : 4)) < 1e-13);
        QCOMPARE(p->yAxis.unit, QString("1"));
        QCOMPARE(p->x, s.x);
    }
    s.y.fill(2);
    QCOMPARE(run("standardize", makeData(s)).diagnostic().code, QString("STANDARDIZE_CONSTANT"));
    auto big = numeric::centerSeries({1e300, -1e300, 5e299, -5e299}, true, 1);
    QVERIFY(std::isfinite(big.scale));
    for (double v : big.values)
        QVERIFY(std::isfinite(v));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, numeric::centerSeries({1}, true, 1));
    QVERIFY_THROWS_EXCEPTION(numeric::SeriesCancelled,
                             numeric::centerSeries({1, 2}, false, 0, [] { return true; }));
}
void EvolutionAudit::qualityInspection()
{
    SeriesData s{{0, 1, 2, 2, 1, 10, 11}, {0, 0, 0, 0, 0, 0, 0}, {"X", "m"}, {"Y", ""}};
    auto source = makeData(s);
    auto r = run("sampling_check", source);
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(r.outputs["series"] == source);
    QCOMPARE(r.report["duplicate_x"].toInt(), 2);
    QCOMPARE(r.report["descending_pairs"].toInt(), 1);
    QCOMPARE(r.report["equal_pairs"].toInt(), 1);
    QCOMPARE(r.report["large_gap_count"].toInt(), 1);
    QCOMPARE(r.report["median_positive_spacing"].toDouble(), 1.);
    for (const auto &output : r.outputs)
        QVERIFY(validateData(output).isEmpty());
    r = run("sampling_check", makeData(sample(5000)));
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(r.report["uniform"].toBool());
    QVERIFY(r.report["interval_details_truncated"].toBool());
    QCOMPARE(dataAs<TableData>(r.outputs["sampling_intervals"])->rows.size(), qsizetype(4096));
    s.x = {3, 2, 1};
    s.y = {0, 0, 0};
    r = run("sampling_check", makeData(s));
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(!r.report["positive_spacing_available"].toBool());
}
void EvolutionAudit::workflowDatabaseAndFailure()
{
    WorkflowModel m(registry());
    auto root = m.addRoot("input_series", {0, 0});
    QVERIFY(root.ok());
    auto select = m.addChild(root.nodeId, ChildSlot::Right, "select_interval",
                             {{"interval_lower", 100}, {"interval_upper", 150}});
    QVERIFY(select.ok());
    auto norm = m.addChild(select.nodeId, ChildSlot::Right, "standardize");
    QVERIFY(norm.ok());
    auto evo = m.addChild(norm.nodeId, ChildSlot::Right, "evolution_fft",
                          {{"evo_window", 64}, {"evo_step", 1}});
    QVERIFY(evo.ok());
    auto bad = m.addChild(evo.nodeId, ChildSlot::Down, "demean", {}, "power_map");
    QVERIFY(!bad.ok());
    auto quality = m.addChild(select.nodeId, ChildSlot::Down, "sampling_check");
    QVERIFY(quality.ok());
    auto frozen = m.freeze();
    QVERIFY(frozen.validation.ok());
    auto source = makeData(sample());
    auto result =
        WorkflowExecutor{}.run({frozen.snapshot, root.nodeId, {"inspection.csv", source, {}}});
    QCOMPARE(result.state, RunState::Succeeded);
    QTemporaryDir tmp;
    ResultRepository db;
    QVERIFY2(db.open(tmp.filePath("audit.sqlite")).isEmpty(), "SQLite open failed");
    auto saved = db.save(result);
    QVERIFY2(saved.saved(), qPrintable(saved.error));
    auto loaded = db.load(saved.recordId);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(dataAs<GridData>(loaded.run.nodes[evo.nodeId].outputs["power_map"])->values,
             dataAs<GridData>(result.nodes[evo.nodeId].outputs["power_map"])->values);
    QCOMPARE(dataAs<TableData>(loaded.run.nodes[quality.nodeId].outputs["sampling_summary"])->rows,
             dataAs<TableData>(result.nodes[quality.nodeId].outputs["sampling_summary"])->rows);
    const OutputReference reference{tmp.filePath("audit.sqlite"),
                                    saved.recordId,
                                    result.runId,
                                    evo.nodeId,
                                    "power_map",
                                    "Grid"};
    const auto selected = db.loadOutput(reference);
    QVERIFY(selected.ok());
    PlotRequest request;
    request.columns = 32;
    request.rows = 32;
    const auto preview = db.loadPlotPreview(reference, request);
    QCOMPARE(preview.state, NodeState::Succeeded);
    const auto *previewGrid = dataAs<GridData>(preview.outputs["power_map"]);
    QVERIFY(previewGrid);
    QCOMPARE(previewGrid->columns, qsizetype(32));
    QCOMPARE(previewGrid->rows, qsizetype(32));
    QVERIFY(m.setParameters(select.nodeId, {{"interval_lower", 10000}, {"interval_upper", 20000}})
                .ok());
    result = WorkflowExecutor{}.run({m.freeze().snapshot, root.nodeId, {"bad.csv", source, {}}});
    QCOMPARE(result.nodes[select.nodeId].state, NodeState::Failed);
    QCOMPARE(result.nodes[norm.nodeId].state, NodeState::Blocked);
    QCOMPARE(result.nodes[evo.nodeId].state, NodeState::Blocked);
    QCOMPARE(result.executionOrder.size(), qsizetype(2));
}
void EvolutionAudit::concurrentSources()
{
    WorkflowModel m(registry());
    auto first = m.addRoot("input_series", {0, 0});
    auto second = m.addRoot("input_series", {10, 0});
    QVERIFY(first.ok());
    QVERIFY(second.ok());
    QVERIFY(m.addChild(first.nodeId, ChildSlot::Right, "evolution_mtm",
                       {{"evo_window", 32}, {"evo_step", 16}})
                .ok());
    QVERIFY(m.addChild(second.nodeId, ChildSlot::Right, "demean").ok());
    auto one = makeData(sample(128)), two = makeData(sample(160));
    SourceSelection selection{{first.nodeId, {{"one", one, {}}, {"two", two, {}}}},
                              {second.nodeId, {{"two", two, {}}}}};
    auto plan = prepareRuns(m.freeze().snapshot, selection);
    QVERIFY(plan.validation.ok());
    QCOMPARE(plan.requests.size(), qsizetype(3));
    std::vector<std::future<RunResult>> jobs;
    for (const auto &request : plan.requests)
        jobs.push_back(
            std::async(std::launch::async, [request] { return WorkflowExecutor{}.run(request); }));
    for (auto &job : jobs)
        QCOMPARE(job.get().state, RunState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(one)->x.size(), qsizetype(128));
    QCOMPARE(dataAs<SeriesData>(two)->x.size(), qsizetype(160));
}
void EvolutionAudit::translationsAndCategories()
{
    QFile en(QFINDTESTDATA("../resources/i18n/en.json")),
        zh(QFINDTESTDATA("../resources/i18n/zh.json"));
    QVERIFY(en.open(QIODevice::ReadOnly));
    QVERIFY(zh.open(QIODevice::ReadOnly));
    auto english = QJsonDocument::fromJson(en.readAll()).object(),
         chinese = QJsonDocument::fromJson(zh.readAll()).object();
    for (const auto &d : registry().descriptors())
    {
        QVERIFY(!d.category.isEmpty());
        for (const auto &c : d.category)
        {
            QVERIFY2(english.contains("category." + c), qPrintable(c));
            QVERIFY(chinese.contains("category." + c));
        }
        if (!QStringList{"evolution_fft", "evolution_mtm", "select_interval", "demean",
                         "standardize", "sampling_check"}
                 .contains(d.key))
            continue;
        QVERIFY(english.contains("method." + d.key));
        QVERIFY(chinese.contains("method." + d.key));
        for (const auto &p : d.parameters)
        {
            QVERIFY(english.contains("parameter." + p.key));
            QVERIFY(chinese.contains("parameter." + p.key));
            QVERIFY(english.contains("parametergroup." + p.group));
            QVERIFY(chinese.contains("parametergroup." + p.group));
            for (const auto &c : p.choices)
            {
                QVERIFY(english.contains("choice." + p.key + "." + c));
                QVERIFY(chinese.contains("choice." + p.key + "." + c));
            }
        }
    }
}
int runEvolutionAudit(int argc, char **argv)
{
    EvolutionAudit tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "evolution_audit.moc"
