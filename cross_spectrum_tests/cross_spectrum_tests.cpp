// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QDataStream>
#include <cmath>
#include <future>
#include <limits>
#include "math/spectrum/cross_spectrum.h"
#include "processing/cross_spectrum_task.h"
#include "processing/builtin_tasks.h"
#include "processing/basic_signal_tasks.h"
#include "processing/scientific_registry.h"
#include "core/batch_workflow.h"
#include "model/workflow_codec.h"
#include "io/database/data_codec.h"
#include "io/database/data_format.h"
#include "io/database/result_repository.h"
#include "io/database/database_transfer.h"
#include "io/exports/csv_export.h"
#include "io/examples/example_catalog.h"
#include "ui/results/preview_data.h"
using namespace aster;
using namespace aster::numeric;
namespace
{
constexpr double pi = 3.14159265358979323846;
std::vector<double> vector(const QJsonValue &value)
{
    std::vector<double> v;
    for (auto x : value.toArray())
        v.push_back(x.toDouble());
    return v;
}
SeriesData series(int n = 512, double phase = 0, double scale = 1)
{
    SeriesData d;
    d.xAxis = {"Time", "ka"};
    d.yAxis = {"Signal", "u"};
    for (int i = 0; i < n; ++i)
    {
        d.x << i * .25;
        d.y << scale * (std::sin(2 * pi * i / 32 + phase) + .3 * std::cos(2 * pi * i / 13 + .2));
    }
    return d;
}
NodeResult task(SeriesData a = series(), SeriesData b = series(512, .4), QJsonObject p = {})
{
    return CrossSpectrumTask(p).execute({makeData(std::move(a)), makeData(std::move(b))}, {});
}
std::vector<double> values(const SeriesData &d)
{
    return {d.y.begin(), d.y.end()};
}
void close(double a, double b, double tolerance = 1e-10)
{
    QVERIFY2(
        std::abs(a - b) <= tolerance,
        qPrintable(
            QString("%1 != %2 (tol %3)").arg(a, 0, 'g', 17).arg(b, 0, 'g', 17).arg(tolerance)));
}
void compare(const std::vector<double> &actual, const QJsonValue &expected, double floor = 0)
{
    const auto e = vector(expected);
    QCOMPARE(actual.size(), e.size());
    double peak = floor;
    for (double v : e)
        peak = std::max(peak, std::abs(v));
    for (std::size_t i = 0; i < e.size(); ++i)
        close(actual[i], e[i], 3e-11 * std::max(std::abs(e[i]), peak * 1e-3) + 1e-300);
}
QByteArray bytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
} // namespace
class CrossTests : public QObject
{
    Q_OBJECT
  private slots:
    void realReference_data();
    void realReference();
    void realUnalignedInputsRejected();
    void detrendedRectangularDc();
    void reference_data();
    void reference();
    void analyticPhaseAndSwap();
    void oppositeScaleAndPsd();
    void meanOfComplexNotMagnitude();
    void invalid_data();
    void invalid();
    void cancellation();
    void concurrent();
    void taskValidation_data();
    void taskValidation();
    void taskMetadata();
    void pairedExecution();
    void maskedCodec();
    void malformedCodec();
    void csvAndPreview();
    void curveGaps();
    void databaseRoundTrip();
    void legacyCodec();
    void catalogueBoundary();
    void ignoredTailDoesNotRescale();
    void streamedMaskedTableAndMerge();
    void maskedPayloadBudget();
};

void CrossTests::realReference_data()
{
    QTest::addColumn<QJsonObject>("record");
    QTest::addColumn<QJsonObject>("fixture");
    QFile f(QFINDTESTDATA("fixtures/real_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const auto data = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto &v : data["records"].toArray())
    {
        const auto record = v.toObject();
        for (const auto &c : record["cases"].toArray())
            QTest::newRow(qPrintable(c.toObject()["name"].toString())) << record << c.toObject();
    }
}
void CrossTests::realReference()
{
    QFETCH(QJsonObject, record);
    QFETCH(QJsonObject, fixture);
    const auto registry = makeScientificRegistry();
    SeriesData inputs[2];
    int side = 0;
    for (const auto *key : {"a_source", "b_source"})
    {
        const auto source = record[key].toObject();
        auto original = examples::load(source["id"].toString(), "age");
        QCOMPARE(original.provenance["sha256"], source["sha256"]);
        auto &input = inputs[side];
        if (record["alignment"] == "native_fixed_resample")
        {
            auto prep = registry.create("preprocess", {{"spacing_mode", "fixed"}, {"spacing", record["spacing"]}});
            const auto result = prep->execute({makeData(original.series)}, {});
            QCOMPARE(result.state, NodeState::Succeeded);
            input = *dataAs<SeriesData>(result.outputs["series"]);
        }
        else
        {
            // Deliberately prepared shared grid, not implicit alignment in the Task.
            input.xAxis = original.series.xAxis; input.yAxis = original.series.yAxis;
            for (const auto &v : record["x"].toArray()) input.x << v.toDouble();
            for (const auto &v : record[side ? "b" : "a"].toArray()) input.y << v.toDouble();
        }
        const auto x = record["x"].toArray(), y = record[side ? "b" : "a"].toArray();
        QCOMPARE(input.x.size(), x.size());
        for (int i=0;i<x.size();++i)
        { close(input.x[i],x[i].toDouble(),1e-10); close(input.y[i],y[i].toDouble(),1e-10); }
        ++side;
    }
    const auto result = task(inputs[0], inputs[1], fixture["parameters"].toObject());
    QCOMPARE(result.state, NodeState::Succeeded);
    const auto *table = dataAs<TableData>(result.outputs["cross_spectrum"]);
    QVERIFY(table);
    const auto expected = fixture["rows"].toArray();
    QCOMPARE(table->rows.size(), expected.size());
    QVector<double> peak(8,0);
    for (const auto &v : expected) for (int c=0;c<8;++c)
        peak[c]=std::max(peak[c],std::abs(v.toArray()[c].toDouble()));
    for (int i=0;i<expected.size();++i) for (int c=0;c<8;++c)
    {
        const auto parameters=fixture["parameters"].toObject();
        const bool structuralZero=i==0 && parameters["psd_taper"]=="rectangular" && parameters["psd_trend"]!="none";
        if (structuralZero && (c==2 || c==3)) { QVERIFY(!table->isDefined(i,c));continue; }
        QVERIFY(table->isDefined(i,c));
        const double a=table->rows[i][c], b=expected[i].toArray()[c].toDouble();
        if (c==3) { if(expected[i].toArray()[1].toDouble()>peak[1]*1e-10) close(std::remainder(a-b,360.),0,2e-7); }
        else if (c==2) close(a,b,2e-9);
        else close(a,b,3e-10*std::max(std::abs(b),std::max(peak[c],(c==4||c==5)?peak[1]:0.)*1e-3)+1e-300);
    }
}

void CrossTests::detrendedRectangularDc()
{
    const auto a=values(series()),b=values(series(512,.4));
    for(const auto trend:{CrossDetrend::Mean,CrossDetrend::Linear})
        for(const std::size_t nfft:{64u,127u})
        {
            CrossSpectrumOptions o;o.segment=64;o.overlap=32;o.nfft=nfft;
            o.window=CrossWindow::Rectangular;o.detrend=trend;
            const auto r=crossSpectrum(a,b,.25,o);
            QCOMPARE(r.autoA[0],0.);QCOMPARE(r.autoB[0],0.);QCOMPARE(r.magnitude[0],0.);
            QVERIFY(!r.coherenceDefined[0]);QVERIFY(!r.phaseDefined[0]);
            o.detrend=CrossDetrend::None;const auto unchanged=crossSpectrum(a,b,.25,o);
            QVERIFY(unchanged.autoA[0]>0);QVERIFY(unchanged.coherenceDefined[0]);
            o.window=CrossWindow::Hann;o.detrend=trend;const auto weighted=crossSpectrum(a,b,.25,o);
            QVERIFY(weighted.autoA[0]>0);QVERIFY(weighted.coherenceDefined[0]);
        }
}

void CrossTests::realUnalignedInputsRejected()
{
    auto a=examples::load("odp846_d18o","age"),b=examples::load("odp846_d13c","age");
    auto rejected=task(a.series,b.series);
    QCOMPARE(rejected.state,NodeState::Failed);
    QVERIFY(rejected.diagnostic().code.startsWith("INPUT_X_"));
    const auto r=makeScientificRegistry();
    auto prep=r.create("preprocess",{{"spacing_mode","fixed"},{"spacing",5.}});
    a=examples::load("odp926_d13c","age");b=examples::load("odp926_lightness","age");
    auto aa=prep->execute({makeData(a.series)},{}),bb=prep->execute({makeData(b.series)},{});
    QCOMPARE(aa.state,NodeState::Succeeded);QCOMPARE(bb.state,NodeState::Succeeded);
    // Equal spacing does not imply equal endpoints or matching sample counts.
    rejected=CrossSpectrumTask({}).execute({aa.outputs["series"],bb.outputs["series"]},{});
    QCOMPARE(rejected.diagnostic().code,QString("CROSS_ALIGNMENT"));
}

void CrossTests::reference_data()
{
    QTest::addColumn<QJsonObject>("fixture");
    QFile f(QFINDTESTDATA("fixtures/scipy_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root["cases"].toArray().size(), 16);
    for (auto v : root["cases"].toArray())
    {
        const auto o = v.toObject();
        QTest::newRow(qPrintable(o["name"].toString())) << o;
    }
}
void CrossTests::reference()
{
    QFETCH(QJsonObject, fixture);
    CrossSpectrumOptions o;
    o.segment = fixture["segment"].toInt();
    o.overlap = fixture["overlap"].toInt();
    o.nfft = fixture["nfft"].toInt();
    o.window = fixture["window"] == "hann" ? CrossWindow::Hann : CrossWindow::Rectangular;
    o.detrend = fixture["detrend"] == "none"     ? CrossDetrend::None
                : fixture["detrend"] == "linear" ? CrossDetrend::Linear
                                                 : CrossDetrend::Mean;
    const auto a = vector(fixture["a"]), b = vector(fixture["b"]);
    const auto r = crossSpectrum(a, b, fixture["spacing"].toDouble(), o);
    compare(r.frequency, fixture["frequency"]);
    compare(r.autoA, fixture["auto_a"]);
    compare(r.autoB, fixture["auto_b"]);
    double crossPeak = 0;
    for (auto v : fixture["magnitude"].toArray())
        crossPeak = std::max(crossPeak, v.toDouble());
    compare(r.real, fixture["real"], crossPeak);
    compare(r.imaginary, fixture["imaginary"], crossPeak);
    compare(r.magnitude, fixture["magnitude"]);
    const auto ec = fixture["coherence"].toArray(), ep = fixture["phase"].toArray();
    double peak = 0;
    for (double v : r.magnitude)
        peak = std::max(peak, v);
    for (std::size_t i = 0; i < r.frequency.size(); ++i)
    {
        // Exact zeros are deliberately tested with rectangular-window constants
        // and separately with zero/mean-removed constant inputs.
        const bool c = fixture["coherence_defined"].toArray()[i].toInt(),
                   p = fixture["phase_defined"].toArray()[i].toInt();
        if (r.magnitude[i] > peak * 1e-12 || peak == 0)
        {
            QCOMPARE(bool(r.coherenceDefined[i]), c);
            QCOMPARE(bool(r.phaseDefined[i]), p);
            if (c)
                close(r.coherence[i], ec[i].toDouble(), 2e-10);
            if (p)
                close(std::remainder(r.phaseDegrees[i] - ep[i].toDouble(), 360.), 0, 2e-8);
        }
    }
    QCOMPARE(r.segments, 1 + (a.size() - o.segment) / (o.segment - o.overlap));
    QCOMPARE(r.unusedTail, a.size() - ((r.segments - 1) * (o.segment - o.overlap) + o.segment));
}
void CrossTests::analyticPhaseAndSwap()
{
    std::vector<double> a(512), b(512);
    for (int i = 0; i < 512; ++i)
    {
        a[i] = std::cos(2 * pi * i / 32);
        b[i] = 2 * std::cos(2 * pi * i / 32 + .6);
    }
    CrossSpectrumOptions o;
    o.segment = 64;
    o.overlap = 32;
    o.window = CrossWindow::Rectangular;
    o.detrend = CrossDetrend::None;
    const auto r = crossSpectrum(a, b, .25, o), s = crossSpectrum(b, a, .25, o);
    close(r.frequency[2], .125);
    close(r.autoA[2], 8);
    close(r.autoB[2], 32);
    close(r.real[2], 16 * std::cos(.6));
    close(r.imaginary[2], 16 * std::sin(.6));
    close(r.coherence[2], 1);
    close(r.phaseDegrees[2], .6 * 180 / pi);
    for (std::size_t k = 0; k < r.frequency.size(); ++k)
    {
        close(r.real[k], s.real[k]);
        close(r.imaginary[k], -s.imaginary[k]);
        close(r.coherence[k], s.coherence[k]);
    }
    // DC/Nyquist retain factor 1, interior bins factor 2; total density integrates variance.
    double integral = 0;
    for (double p : r.autoA)
        integral += p * (r.frequency[1] - r.frequency[0]);
    close(integral, .5);
}
void CrossTests::oppositeScaleAndPsd()
{
    auto a = series(), b = series(512, .4);
    auto r = task(a, b);
    QCOMPARE(r.state, NodeState::Succeeded);
    WelchSpectrumTask w(makeScientificRegistry().normalize("welch_spectrum", {}).parameters);
    auto legacy = w.execute({makeData(a)}, {});
    QCOMPARE(legacy.state, NodeState::Succeeded);
    const auto *psd = dataAs<SpectrumData>(legacy.outputs["spectrum"]);
    const auto *t = dataAs<TableData>(r.outputs["cross_spectrum"]);
    QCOMPARE(t->rows.size(), psd->power.size());
    for (qsizetype i = 0; i < t->rows.size(); ++i)
        close(t->rows[i][6], psd->power[i], 1e-10);
    for (auto &v : a.y)
        v *= 1e70;
    for (auto &v : b.y)
        v *= 1e-70;
    auto scaled = task(a, b);
    QCOMPARE(scaled.state, NodeState::Succeeded);
    const auto *s = dataAs<TableData>(scaled.outputs["cross_spectrum"]);
    for (qsizetype i = 0; i < s->rows.size(); ++i)
    {
        close(t->rows[i][1], s->rows[i][1], 1e-10);
        close(t->rows[i][2], s->rows[i][2], 1e-9);
    }
}
void CrossTests::meanOfComplexNotMagnitude()
{
    std::vector<double> a(128), b(128);
    for (int i = 0; i < 128; ++i)
    {
        a[i] = std::cos(2 * pi * (i % 64) / 16);
        b[i] = (i < 64 ? 1 : -1) * a[i];
    }
    CrossSpectrumOptions o;
    o.segment = 64;
    o.overlap = 0;
    o.window = CrossWindow::Rectangular;
    const auto r = crossSpectrum(a, b, 1, o);
    close(r.magnitude[4], 0);
    close(r.coherence[4], 0);
    QVERIFY(r.coherenceDefined[4]);
    QVERIFY(!r.phaseDefined[4]);
}
void CrossTests::invalid_data()
{
    QTest::addColumn<QString>("which");
    for (const char *n :
         {"length", "short", "segment", "overlap", "nfft", "single", "spacing_zero", "spacing_nan",
          "spacing_inf", "spacing_underflow", "nan_a", "inf_b", "window", "trend", "fft_budget",
          "work_budget", "input_budget", "overflow", "underflow"})
        QTest::newRow(n) << QString(n);
}
void CrossTests::invalid()
{
    QFETCH(QString, which);
    auto a = values(series()), b = a;
    CrossSpectrumOptions o;
    double dt = 1;
    if (which == "length")
        b.pop_back();
    if (which == "short")
    {
        a.resize(3);
        b = a;
    }
    if (which == "segment")
        o.segment = 7;
    if (which == "overlap")
        o.overlap = o.segment;
    if (which == "nfft")
        o.nfft = 8;
    if (which == "single")
    {
        o.segment = 512;
        o.overlap = 0;
    }
    if (which == "spacing_zero")
        dt = 0;
    if (which == "spacing_nan")
        dt = std::numeric_limits<double>::quiet_NaN();
    if (which == "spacing_inf")
        dt = INFINITY;
    if (which == "spacing_underflow")
        dt = std::numeric_limits<double>::denorm_min();
    if (which == "nan_a")
        a[9] = NAN;
    if (which == "inf_b")
        b[9] = INFINITY;
    if (which == "window")
        o.window = CrossWindow(23);
    if (which == "trend")
        o.detrend = CrossDetrend(23);
    if (which == "fft_budget")
        o.nfft = CrossSpectrumLimits::maxFft + 1;
    if (which == "work_budget")
    {
        a.resize(100000, 1);
        b = a;
        o.overlap = 255;
    }
    if (which == "input_budget")
    {
        a.resize(CrossSpectrumLimits::maxSamples + 1);
        b = a;
    }
    if (which == "overflow")
        for (auto &v : a)
            v *= 1e200;
    if (which == "underflow")
    {
        for (auto &v : a)
            v *= 1e-200;
        for (auto &v : b)
            v *= 1e-200;
    }
    bool threw = false;
    try
    {
        crossSpectrum(a, b, dt, o);
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    QVERIFY(threw);
}
void CrossTests::cancellation()
{
    const auto a = values(series());
    QVERIFY_THROWS_EXCEPTION(CrossSpectrumCancelled,
                             crossSpectrum(a, a, 1, {}, [] { return true; }));
    bool stop = false;
    int last = -1;
    QVERIFY_THROWS_EXCEPTION(CrossSpectrumCancelled, crossSpectrum(
                                                         a, a, 1, {}, [&] { return stop; },
                                                         [&](int n)
                                                         {
                                                             QVERIFY(n >= last);
                                                             last = n;
                                                             stop = true;
                                                         }));
    QVERIFY(last >= 0);
    CancellationToken c;
    c.cancel();
    QCOMPARE(CrossSpectrumTask({}).execute({}, {{}, {}, c, {}, {}, {}}).state,
             NodeState::Cancelled);
}
void CrossTests::concurrent()
{
    const auto a = values(series()), b = values(series(512, .4));
    const auto expected = crossSpectrum(a, b, 1);
    std::vector<std::future<CrossSpectrumResult>> jobs;
    for (int i = 0; i < 8; ++i)
        jobs.push_back(std::async(std::launch::async, [=] { return crossSpectrum(a, b, 1); }));
    for (auto &job : jobs)
    {
        auto r = job.get();
        QCOMPARE(r.real, expected.real);
        QCOMPARE(r.imaginary, expected.imaginary);
    }
}
void CrossTests::taskValidation_data()
{
    QTest::addColumn<QString>("which");
    QTest::addColumn<QString>("code");
    for (auto p : QList<QPair<QString, QString>>{{"absent_b", "INPUT_TYPE"},
                                                 {"type_b", "INPUT_TYPE"},
                                                 {"length", "CROSS_ALIGNMENT"},
                                                 {"shift", "CROSS_ALIGNMENT"},
                                                 {"unit", "CROSS_X_AXIS"},
                                                 {"origin", "CROSS_X_AXIS"},
                                                 {"jitter", "INPUT_X_NONUNIFORM"},
                                                 {"descending", "INPUT_X_DESCENDING"},
                                                 {"duplicate", "INPUT_X_DUPLICATE"},
                                                 {"nan", "INPUT_INVALID"},
                                                 {"negative_parameter", "TASK_PARAMETERS_INVALID"},
                                                 {"fraction", "TASK_PARAMETERS_INVALID"},
                                                 {"unknown_parameter", "TASK_PARAMETERS_INVALID"},
                                                 {"choice", "TASK_PARAMETERS_INVALID"},
                                                 {"overlap", "CROSS_OVERLAP"},
                                                 {"single", "CROSS_SEGMENTS"}})
        QTest::newRow(qPrintable(p.first)) << p.first << p.second;
}
void CrossTests::taskValidation()
{
    QFETCH(QString, which);
    QFETCH(QString, code);
    auto a = series(), b = series();
    QJsonObject p;
    if (which == "length")
    {
        b.x.removeLast();
        b.y.removeLast();
    }
    if (which == "shift")
        for (auto &v : b.x)
            v += .01;
    if (which == "unit")
        b.xAxis.unit = "m";
    if (which == "origin")
        b.xAxis.name = "Age before J2000";
    if (which == "jitter")
        b.x[50] += .001;
    if (which == "descending")
        b.x[50] = b.x[49] - .1;
    if (which == "duplicate")
        b.x[50] = b.x[49];
    if (which == "nan")
        b.y[50] = NAN;
    if (which == "negative_parameter")
        p["psd_window"] = -1;
    if (which == "fraction")
        p["psd_window"] = 32.5;
    if (which == "unknown_parameter")
        p["window"] = 32;
    if (which == "choice")
        p["psd_trend"] = "typo";
    if (which == "overlap")
        p["psd_overlap"] = 256;
    if (which == "single")
        p["psd_window"] = 512;
    DataRef db = makeData(b);
    if (which == "absent_b")
        db.reset();
    if (which == "type_b")
        db = makeData(TableData{{"x"}, {{1}}});
    auto result = CrossSpectrumTask(p).execute({makeData(a), db}, {});
    QCOMPARE(result.state, NodeState::Failed);
    QCOMPARE(result.diagnostic().code, code);
    QVERIFY(result.outputs.isEmpty());
}
void CrossTests::taskMetadata()
{
    auto a = series(537), b = series(537);
    b.yAxis = {"Temperature", "degC"};
    const auto r = task(a, b, {{"psd_window", 32}, {"psd_overlap", 16}});
    QCOMPARE(r.state, NodeState::Succeeded);
    QCOMPARE(r.report["segments"].toInt(), 32);
    QCOMPARE(r.report["unused_tail_samples"].toInt(), 9);
    QCOMPARE(r.report["nfft"].toInt(), 32);
    QCOMPARE(r.report["cross_convention"].toString(), QString("conj(A)*B"));
    QCOMPARE(r.report["column_units"].toArray()[1].toString(), QString("(u) * (degC) * (ka)"));
    TaskRegistry reg;
    QVERIFY(registerCrossSpectrum(reg).ok());
    QVERIFY(reg.validateOutput(CrossSpectrumTask::descriptor(), r).isEmpty());
    for (auto &v : a.y)
        v = 3;
    auto z = task(a, b, {{"psd_window", 32}, {"psd_overlap", 16}});
    QCOMPARE(z.state, NodeState::Succeeded);
    const auto *t = dataAs<TableData>(z.outputs["cross_spectrum"]);
    for (qsizetype i = 0; i < t->rows.size(); ++i)
    {
        QVERIFY(!t->isDefined(i, 2));
        QVERIFY(!t->isDefined(i, 3));
        QCOMPARE(t->rows[i][2], 0.);
    }
}
void CrossTests::pairedExecution()
{
    auto reg = makeScientificRegistry();
    WorkflowModel model(reg);
    const auto root = model.addRoot("input_series", {0, 0});
    QVERIFY(root.ok());
    const auto child = model.addChild(root.nodeId, ChildSlot::Down, "cross_spectrum");
    QVERIFY(child.ok());
    const auto a = QUuid::createUuid(), b = QUuid::createUuid(), c = QUuid::createUuid(),
               g1 = QUuid::createUuid(), g2 = QUuid::createUuid();
    QVector<BatchDataset> inputs{{a, {"same.csv", makeData(series()), {{"column", 1}}}},
                                 {b, {"same.csv", makeData(series(512, .4)), {{"column", 2}}}},
                                 {c, {"unused", makeData(series()), {}}}};
    BatchGrouping groups{{child.nodeId,
                          {{g1, "forward", {a, "series"}, {b, "series"}},
                           {g2, "reverse", {b, "series"}, {a, "series"}}}}};
    const auto plan = prepareBatch(model.freeze().snapshot, root.nodeId, inputs, groups);
    QVERIFY2(plan.validation.ok(), qPrintable(plan.validation.message()));
    QMap<QUuid, NodeResult> results;
    BatchObserver obs;
    obs.completed = [&](const BatchJob &job, const NodeResult &r)
    {
        if (job.node == child.nodeId)
            results[job.item] = r;
    };
    auto run = BatchWorkflowExecutor().run(plan.snapshot, {4}, obs);
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(results.size(), 2);
    const auto direct = task();
    QCOMPARE(encodeData(results[g1].outputs["cross_spectrum"]).bytes,
             encodeData(direct.outputs["cross_spectrum"]).bytes);
    const auto pair = results[g1].report["batch_pair"].toObject();
    QCOMPARE(QUuid(pair["A"].toObject()["dataset_id"].toString()), a);
    QCOMPARE(QUuid(pair["B"].toObject()["dataset_id"].toString()), b);
    const auto *t1 = dataAs<TableData>(results[g1].outputs["cross_spectrum"]);
    const auto *t2 = dataAs<TableData>(results[g2].outputs["cross_spectrum"]);
    for (qsizetype i = 0; i < t1->rows.size(); ++i)
    {
        close(t1->rows[i][4], t2->rows[i][4]);
        close(t1->rows[i][5], -t2->rows[i][5]);
    }
    // One invalid pair does not suppress another group using the same reference.
    auto invalid = series();
    for (auto &x : invalid.x)
        x += 1;
    inputs[2].source.data = makeData(invalid);
    groups[child.nodeId].push_back({QUuid::createUuid(), "bad", {c, "series"}, {b, "series"}});
    const auto second = prepareBatch(model.freeze().snapshot, root.nodeId, inputs, groups);
    QVERIFY(second.validation.ok());
    results.clear();
    auto mixed = BatchWorkflowExecutor().run(second.snapshot, {2}, obs);
    QCOMPARE(mixed.state, RunState::PartialFailed);
    QCOMPARE(results[g1].state, NodeState::Succeeded);
    QCOMPARE(results[g2].state, NodeState::Succeeded);
}
void CrossTests::maskedCodec()
{
    TableData t{{"frequency", "phase"}, {{0, 0}, {1, -45}, {2, 0}}, {{1, 0}, {1, 1}, {1, 1}}};
    const auto e = encodeData(makeData(t));
    QVERIFY(e.ok());
    QCOMPARE(quint8(e.bytes[4]), quint8(2));
    const auto d = decodeData(e.bytes);
    QVERIFY(d.ok());
    const auto *u = dataAs<TableData>(d.data);
    QCOMPARE(u->rows, t.rows);
    QCOMPARE(u->defined, t.defined);
    QCOMPARE(encodeData(d.data).bytes, e.bytes);
    auto bad = t;
    bad.defined[0][1] = 2;
    QVERIFY(!encodeData(makeData(bad)).ok());
    bad = t;
    bad.defined.removeLast();
    QVERIFY(!encodeData(makeData(bad)).ok());
    bad = t;
    bad.rows[0][1] = 12;
    QVERIFY(!encodeData(makeData(bad)).ok());
}
void CrossTests::malformedCodec()
{
    auto e = encodeData(makeData(TableData{{"x", "y"}, {{0, 0}, {1, 0}}, {{1, 0}, {1, 0}}}));
    QVERIFY(e.ok());
    auto b = e.bytes;
    b[b.size() - 1] = char(9);
    QVERIFY(!decodeData(b).ok());
    b = e.bytes;
    b.chop(1);
    QVERIFY(!decodeData(b).ok());
    b = e.bytes;
    b.append('x');
    QVERIFY(!decodeData(b).ok());
    b = e.bytes;
    b[6] = char(1);
    QVERIFY(!decodeData(b).ok());
    b = e.bytes;
    b[4] = char(3);
    QVERIFY(!decodeData(b).ok());
}
void CrossTests::legacyCodec()
{
    auto t = makeData(TableData{{"x", "y"}, {{1, 2}, {3, 4}}});
    auto encoded = encodeData(t);
    QVERIFY(encoded.ok());
    QCOMPARE(quint8(encoded.bytes[4]), quint8(1));
    // Independent byte writer for v1 table; existing payloads remain byte-for-byte stable.
    QByteArray expected;
    QDataStream out(&expected, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::DoublePrecision);
    out << DataFormat::magic << quint16(1) << quint8(4) << quint32(2) << quint32(2);
    for (auto text : {"x", "y"})
    {
        out << quint32(1);
        out.writeRawData(text, 1);
    }
    out << quint32(2) << 1. << 2. << quint32(2) << 3. << 4.;
    QCOMPARE(encoded.bytes, expected);
    QVERIFY(decodeData(expected).ok());
}
void CrossTests::csvAndPreview()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TableData t{{"x", "y"}, {{0, 0}, {1, -45}, {2, 0}}, {{1, 0}, {1, 1}, {1, 1}}};
    const auto file = dir.filePath("masked.csv");
    QVERIFY(exportDataCsv(makeData(t), file).ok());
    const auto text = bytes(file);
    QVERIFY(text.contains("0,\n"));
    QVERIFY(text.contains("1,-45\n"));
    QVERIFY(text.contains("2,0\n"));
    for (int i = 3; i < 25000; ++i)
    {
        t.rows << QVector<double>{double(i), 0};
        t.defined << QVector<quint8>{1, 0};
    }
    const auto reduced = ui::reducePreviewData(NodeResult::success({{"table", makeData(t)}}), {});
    const auto *r = dataAs<TableData>(reduced.outputs["table"]);
    QVERIFY(r->rows.size() < t.rows.size());
    QCOMPARE(r->defined.size(), r->rows.size());
    QVERIFY(!r->isDefined(0, 1));
    QVERIFY(!r->isDefined(r->rows.size() - 1, 1));
}
void CrossTests::curveGaps()
{
    auto r = task();
    auto t = *dataAs<TableData>(r.outputs["cross_spectrum"]);
    r.report["nfft"] = 10;
    t.rows.resize(6);
    t.defined.resize(6);
    for (int i = 0; i < 6; ++i)
    {
        t.rows[i][0] = i;
        t.rows[i][3] = i == 2 ? -179 : 179;
        t.defined[i][3] = 1;
    }
    t.rows[4][3] = 0;
    t.defined[4][3] = 0;
    r.outputs["cross_spectrum"] = makeData(t);
    const auto curves = crossSpectrumCurves(r, "phase_degrees");
    QVERIFY(curves.error.isEmpty());
    QCOMPARE(curves.paths.size(), 4);
    QCOMPARE(curves.paths[0].x, QVector<double>({0, 1}));
    QCOMPARE(curves.paths[3].x, QVector<double>({5}));
    for (auto &mask : t.defined)
        mask[3] = 0;
    for (auto &row : t.rows)
        row[3] = 0;
    r.outputs["cross_spectrum"] = makeData(t);
    const auto empty = crossSpectrumCurves(r, "phase_degrees");
    QVERIFY(empty.error.isEmpty());
    QVERIFY(empty.paths.isEmpty());
    QVERIFY(!crossSpectrumCurves(r, "not_a_metric").error.isEmpty());
}
void CrossTests::databaseRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ResultRepository repo;
    const auto path = dir.filePath("results.sqlite");
    QVERIFY(repo.open(path).isEmpty());
    auto a = series();
    for (auto &y : a.y)
        y = 1;
    const auto result = task(a, series());
    QCOMPARE(result.state, NodeState::Succeeded);
    // Persistence contract test: one self-contained fixture record. The actual
    // pair-to-record application adapter is a later integration stage.
    WorkflowDefinition def;
    const auto id = QUuid::createUuid();
    def.nodes.push_back({id, "cross_spectrum_storage_fixture", 1, {}, std::nullopt});
    def.layout[id] = {0, 0};
    RunResult run;
    run.runId = QUuid::createUuid();
    run.root = id;
    run.state = RunState::Succeeded;
    run.startedAt = run.finishedAt = QDateTime::currentDateTimeUtc();
    run.sourceLabel = "paired payload fixture";
    run.workflowSnapshot = workflowToJson(def);
    run.nodes[id] = result;
    run.executionOrder << id;
    const auto receipt = repo.save(run);
    QVERIFY2(receipt.saved(), qPrintable(receipt.error));
    const auto loaded = repo.load(receipt.recordId);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.run.nodes[id].report, result.report);
    const auto payload = loaded.run.nodes[id].outputs["cross_spectrum"];
    QCOMPARE(encodeData(payload).bytes, encodeData(result.outputs["cross_spectrum"]).bytes);
    OutputReference ref{path, receipt.recordId, run.runId, id, "cross_spectrum", "Table"};
    const auto preview = repo.loadPlotPreview(ref, {});
    QCOMPARE(preview.state, NodeState::Succeeded);
    const auto *t = dataAs<TableData>(preview.outputs["cross_spectrum"]);
    QVERIFY(t);
    QVERIFY(!t->isDefined(0, 2));
    QVERIFY(!t->isDefined(0, 3));
    const auto csv = dir.filePath("from_db.csv");
    const auto forCsv = crossSpectrumCsvData(loaded.run.nodes[id]);
    QVERIFY(forCsv.error.isEmpty());
    QVERIFY(exportDataCsv(forCsv.data, csv).ok());
    QVERIFY(bytes(csv).contains("frequency (1/(ka))"));
    QVERIFY(bytes(csv).contains("phase_degrees (deg)"));
    auto reducedResult = loaded.run.nodes[id];
    reducedResult.report["display_reduced"] = true;
    QVERIFY(!crossSpectrumCsvData(reducedResult).error.isEmpty());
    QVERIFY(!crossSpectrumCurves(reducedResult, "coherence").error.isEmpty());
    QVERIFY(bytes(csv).contains(",,,"));
    auto curves = crossSpectrumCurves(loaded.run.nodes[id], "coherence");
    QVERIFY(curves.error.isEmpty());
    QVERIFY(curves.paths.isEmpty());
}
void CrossTests::streamedMaskedTableAndMerge()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto source = dir.filePath("source.sqlite"),
               destination = dir.filePath("destination.sqlite");
    ResultRepository repo;
    QVERIFY(repo.open(source).isEmpty());
    TableData table{{"frequency", "phase"}, {}};
    for (int i = 0; i < 25001; ++i)
    {
        table.rows << QVector<double>{double(i), i % 2 ? 45. : 0.};
        table.defined << QVector<quint8>{1, quint8(i % 2)};
    }
    WorkflowDefinition def;
    const auto id = QUuid::createUuid();
    def.nodes.push_back({id, "masked_table_fixture", 1, {}, std::nullopt});
    def.layout[id] = {0, 0};
    RunResult run;
    run.runId = QUuid::createUuid();
    run.root = id;
    run.state = RunState::Succeeded;
    run.startedAt = run.finishedAt = QDateTime::currentDateTimeUtc();
    run.sourceLabel = "masked table";
    run.workflowSnapshot = workflowToJson(def);
    run.nodes[id] = NodeResult::success({{"table", makeData(table)}});
    run.executionOrder << id;
    const auto saved = repo.save(run);
    QVERIFY2(saved.saved(), qPrintable(saved.error));
    OutputReference ref{source, saved.recordId, run.runId, id, "table", "Table"};
    const auto preview = repo.loadPlotPreview(ref, {});
    QCOMPARE(preview.state, NodeState::Succeeded);
    const auto *reduced = dataAs<TableData>(preview.outputs["table"]);
    QVERIFY(reduced);
    QCOMPARE(reduced->rows.size(), qsizetype(PlotLimits::maxTableRows));
    for (qsizetype i = 0; i < reduced->rows.size(); ++i)
        QCOMPARE(reduced->isDefined(i, 1), bool(i % 2));
    ResultRepository target;
    QVERIFY(target.open(destination).isEmpty());
    const auto merged = mergeDatabase(destination, source);
    QVERIFY2(merged.ok(), qPrintable(merged.error));
    QCOMPARE(merged.imported, qint64(1));
    const auto loaded = target.load(merged.recordIds[saved.recordId]);
    QVERIFY(loaded.ok());
    const auto *full = dataAs<TableData>(loaded.run.nodes[id].outputs["table"]);
    QVERIFY(full);
    QCOMPARE(full->rows, table.rows);
    QCOMPARE(full->defined, table.defined);
}
void CrossTests::maskedPayloadBudget()
{
    class TablePass final : public Task
    {
      public:
        NodeResult execute(const TaskInput &i, const ExecutionContext &) override
        {
            return NodeResult::success({{"table", i.data}});
        }
    };
    auto registry = makeScientificRegistry();
    TaskDescriptor d;
    d.key = "table_pass";
    d.label = "Table pass";
    d.category = {"Test"};
    d.inputKind = DataKind::Table;
    d.outputs = {{"table", "Table", DataKind::Table, true}};
    d.defaultOutput = "table";
    QVERIFY(
        registry.add(d, [](const QJsonObject &) { return std::make_unique<TablePass>(); }).ok());
    WorkflowModel model(registry);
    const auto root = model.addRoot("input_series", {0, 0});
    const auto joint = model.addChild(root.nodeId, ChildSlot::Down, "cross_spectrum",
                                      {{"psd_window", 8}, {"psd_overlap", 4}, {"psd_nfft", 256}});
    QVERIFY(joint.ok());
    QVERIFY(model.addChild(joint.nodeId, ChildSlot::Down, "table_pass").ok());
    const auto a = QUuid::createUuid(), b = QUuid::createUuid();
    const auto plan = prepareBatch(
        model.freeze().snapshot, root.nodeId,
        {{a, {"A", makeData(series(16)), {}}}, {b, {"B", makeData(series(16, .4)), {}}}},
        {{joint.nodeId, {{QUuid::createUuid(), "pair", {a, "series"}, {b, "series"}}}}});
    QVERIFY(plan.validation.ok());
    // 129*8*8 = 8256 doubles bytes; masks add 1032. Budget lies between them.
    const auto run = BatchWorkflowExecutor().run(plan.snapshot, {1, 8500});
    QVERIFY(run.state != RunState::Succeeded);
    bool found = false;
    for (const auto &j : run.jobs)
        if (j.report["diagnostic"].toObject()["code"] == "BATCH_CACHED_OUTPUT_LIMIT")
            found = true;
    if (!found)
    {
        for (const auto &j : run.jobs)
            if (j.error.contains("budget", Qt::CaseInsensitive))
                found = true;
    }
    QVERIFY(found);
}
void CrossTests::ignoredTailDoesNotRescale()
{
    auto a = values(series()), b = values(series(512, .4));
    const auto reference = crossSpectrum(a, b, .25);
    a.resize(547, 1e300);
    b.resize(547, -1e300);
    const auto result = crossSpectrum(a, b, .25);
    QCOMPARE(result.unusedTail, std::size_t(35));
    QCOMPARE(result.real, reference.real);
    QCOMPARE(result.autoA, reference.autoA);
    QCOMPARE(result.coherenceDefined, reference.coherenceDefined);
}
void CrossTests::catalogueBoundary()
{
    QVERIFY(makeScientificRegistry().find("cross_spectrum"));
    QVERIFY(CrossSpectrumTask::descriptor().catalogVisible);
    QVERIFY(CrossSpectrumTask::descriptor().pairedInput);
}
QTEST_GUILESS_MAIN(CrossTests)
#include "cross_spectrum_tests.moc"
