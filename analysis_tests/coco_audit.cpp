// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "coco_audit.h"
#include "math/chronology/coco_scan.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cmath>
#include <future>
using namespace aster;
using namespace aster::chronology;
namespace
{
std::vector<double> values(QJsonValue v)
{
    std::vector<double> r;
    for (auto x : v.toArray())
        r.push_back(x.toDouble());
    return r;
}
bool near(const std::vector<double> &a, const std::vector<double> &b, double tol = 2e-10)
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
SeriesData proxy(double phase = .3)
{
    SeriesData s;
    s.xAxis = {"Depth", "m"};
    s.yAxis = {"Proxy", "u"};
    CocoOptions o;
    for (int i = 0; i < 1024; ++i)
    {
        s.x << .1 * i;
        double v = 0;
        for (std::size_t j = 0; j < o.periodsKa.size(); ++j)
            v += std::sqrt(o.relativePower[j]) *
                 std::sin(2 * 3.14159265358979323846 * (.1 * i * 100 / 4) / o.periodsKa[j] +
                          phase * (j + 1));
        s.y << v;
    }
    return s;
}
QJsonObject smallParameters()
{
    return {{"coco_rate_min", 3}, {"coco_rate_max", 5}, {"coco_rate_step", .25}};
}
NodeResult task(const SeriesData &s, QJsonObject p = smallParameters())
{
    auto registry = makeScientificRegistry();
    auto t = registry.create("coco_scan", p);
    if (!t)
        return NodeResult::failure("Invalid test parameters");
    return t->execute({makeData(s)}, {});
}
std::vector<double> v(const QVector<double> &x)
{
    return {x.begin(), x.end()};
}
} // namespace
class CocoAudit : public QObject
{
    Q_OBJECT
  private slots:
    void scipyReference_data();
    void scipyReference();
    void knownRatesAndPhase_data();
    void knownRatesAndPhase();
    void invariantsAndGrid();
    void excludedAndInvalid();
    void budgetsAndCancellation();
    void adapterContracts();
    void workflowPersistence();
};
void CocoAudit::scipyReference_data()
{
    QTest::addColumn<QJsonObject>("j");
    QFile f(QFINDTESTDATA("fixtures/coco_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    for (auto x : QJsonDocument::fromJson(f.readAll()).object()["cases"].toArray())
        QTest::newRow(qPrintable(x.toObject()["mode"].toString())) << x.toObject();
}
void CocoAudit::scipyReference()
{
    QFETCH(QJsonObject, j);
    CocoOptions o;
    o.periodsKa = values(j["periods"]);
    o.relativePower = values(j["weights"]);
    o.minimumRate = j["minimum"].toDouble();
    o.maximumRate = j["maximum"].toDouble();
    o.rateStep = j["step"].toDouble();
    o.maximumFrequency = j["fmax"].toDouble();
    o.paddingFactor = j["padding"].toInt();
    o.detrend = j["mode"] == "mean"     ? CocoOptions::Detrend::Mean
                : j["mode"] == "linear" ? CocoOptions::Detrend::Linear
                                        : CocoOptions::Detrend::None;
    auto r = cocoScan(values(j["y"]), j["dx"].toDouble(), o);
    QCOMPARE(r.bestRow, std::size_t(j["best_row"].toInt()));
    auto rows = j["rows"].toArray();
    QCOMPARE(r.rows.size(), std::size_t(rows.size()));
    for (std::size_t i = 0; i < r.rows.size(); ++i)
    {
        auto x = values(rows[int(i)]);
        const auto &a = r.rows[i];
        QVERIFY(near({a.rate, a.correlation, a.rayleigh, a.nyquist, double(a.resolvedTargets),
                      double(a.bins), double(int(a.status))},
                     x));
    }
    QVERIFY(near(r.frequency, values(j["frequency"])));
    QVERIFY(near(r.observedPower, values(j["observed"])));
    QVERIFY(near(r.targetPower, values(j["target"])));
}
void CocoAudit::knownRatesAndPhase_data()
{
    QTest::addColumn<double>("phase");
    for (double p : {0., .3, 1.1, 2.5})
        QTest::newRow(qPrintable(QString::number(p))) << p;
}
void CocoAudit::knownRatesAndPhase()
{
    QFETCH(double, phase);
    auto r = task(proxy(phase));
    QCOMPARE(r.state, NodeState::Succeeded);
    QCOMPARE(r.report["best_rate_cm_ka"].toDouble(), 4.);
    QVERIFY(r.report["best_correlation"].toDouble() > .8);
}
void CocoAudit::invariantsAndGrid()
{
    auto s = proxy();
    auto r = task(s);
    QCOMPARE(r.state, NodeState::Succeeded);
    s.xAxis.unit = "cm";
    for (auto &x : s.x)
        x = x * 100 + 1000;
    for (auto &y : s.y)
        y = -3 * y + 12;
    auto p = smallParameters();
    p["chrono_depth_unit"] = "cm";
    auto b = task(s, p);
    QCOMPARE(b.state, NodeState::Succeeded);
    auto a1 = dataAs<SeriesData>(r.outputs["correlation"]),
         b1 = dataAs<SeriesData>(b.outputs["correlation"]);
    QVERIFY(a1 && b1);
    QVERIFY(near(v(a1->y), v(b1->y)));
    CocoOptions o;
    o.minimumRate = 3;
    o.maximumRate = 5;
    o.rateStep = .3;
    auto z = cocoScan(v(proxy().y), .1, o);
    QCOMPARE(z.rows.size(), std::size_t(7));
    QVERIFY(std::abs(z.rows.back().rate - 4.8) < 1e-14);
    auto run = []
    {
        CocoOptions o;
        o.minimumRate = 3;
        o.maximumRate = 5;
        o.rateStep = .25;
        return cocoScan(v(proxy().y), .1, o);
    };
    auto future = std::async(std::launch::async, run);
    auto one = run(), two = future.get();
    QVERIFY(near(one.targetPower, two.targetPower, 0));
    QCOMPARE(one.bestRow, two.bestRow);
}
void CocoAudit::excludedAndInvalid()
{
    auto s = proxy();
    auto p = smallParameters();
    p["coco_rate_min"] = .01;
    p["coco_rate_max"] = 30;
    p["coco_rate_step"] = .5;
    auto r = task(s, p);
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(r.report["excluded_rates"].toInt() > 0);
    const auto *d = dataAs<TableData>(r.outputs["scan_diagnostics"]);
    QVERIFY(d);
    bool ny = false, ray = false;
    for (const auto &row : d->rows)
    {
        if (row[1] == 1)
            ny = true;
        if (row[1] == 2)
            ray = true;
        if (row[2] == 0)
            QCOMPARE(row[3], 0.);
    }
    QVERIFY(ny && ray);
    auto b = s;
    b.x[2] = b.x[1];
    QCOMPARE(task(b).state, NodeState::Failed);
    b = s;
    b.x[2] += .001;
    QCOMPARE(task(b).state, NodeState::Failed);
    b = s;
    b.xAxis.unit = "ka";
    QCOMPARE(task(b).diagnostic().code, QString("CHRONOLOGY_UNIT"));
    b = s;
    b.y[0] = qQNaN();
    QCOMPARE(task(b).state, NodeState::Failed);
    b = s;
    b.y.fill(1);
    QCOMPARE(task(b).diagnostic().code, QString("COCO_NO_SCORE"));
    p = smallParameters();
    p["coco_periods"] = "405,125,95";
    QCOMPARE(task(s, p).diagnostic().code, QString("COCO_PARAMETERS"));
    p["coco_periods"] = "405,405,95,41,24,22,19";
    QCOMPARE(task(s, p).state, NodeState::Failed);
    p["coco_periods"] = "not-a-period";
    QCOMPARE(task(s, p).state, NodeState::Failed);
    auto registry = makeScientificRegistry();
    auto t = registry.create("coco_scan", smallParameters());
    QCOMPARE(t->execute({makeData(TableData{{"a"}, {{1}}})}, {}).state, NodeState::Failed);
    CocoOptions o;
    o.minimumRate = 1000;
    o.maximumRate = 1001;
    QVERIFY_THROWS_EXCEPTION(std::domain_error, cocoScan(v(s.y), .1, o));
    o = {};
    o.periodsKa[0] = 0;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, cocoScan(v(s.y), .1, o));
}
void CocoAudit::budgetsAndCancellation()
{
    auto oversized = proxy();
    oversized.x.resize(CocoLimits::maxSamples + 1);
    oversized.y.resize(CocoLimits::maxSamples + 1);
    QCOMPARE(task(oversized).diagnostic().code, QString("COCO_BUDGET"));
    auto y = v(proxy().y);
    CocoOptions o;
    QVERIFY_THROWS_EXCEPTION(CocoCancelled, cocoScan(y, .1, o, [] { return true; }));
    int calls = 0;
    QVERIFY_THROWS_EXCEPTION(CocoCancelled, cocoScan(y, .1, o, [&] { return ++calls > 8; }));
    o.rateStep = .0001;
    QVERIFY_THROWS_EXCEPTION(std::length_error, cocoScan(y, .1, o));
    o = {};
    o.paddingFactor = 4;
    std::vector<double> big(CocoLimits::maxSamples, 1);
    QVERIFY_THROWS_EXCEPTION(std::length_error, cocoScan(big, .1, o));
    o = {};
    big.resize(CocoLimits::maxSamples + 1);
    QVERIFY_THROWS_EXCEPTION(std::length_error, cocoScan(big, .1, o));
    o = {};
    o.minimumRate = 1;
    o.maximumRate = 200;
    o.rateStep = .1;
    big.resize(32768);
    QVERIFY_THROWS_EXCEPTION(std::length_error, cocoScan(big, .1, o));
    auto p = smallParameters();
    p["coco_rate_step"] = .00001;
    QCOMPARE(task(proxy(), p).diagnostic().code, QString("COCO_BUDGET"));
}
void CocoAudit::adapterContracts()
{
    auto registry = makeScientificRegistry();
    const auto *entry = registry.find("coco_scan");
    QVERIFY(entry);
    auto normalized = registry.normalize("coco_scan", {});
    QVERIFY(normalized.validation.ok());
    const auto r = task(proxy());
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(registry.validateOutput(entry->descriptor, r).isEmpty());
    QCOMPARE(r.outputs.size(), 5);
    QVERIFY(!r.report.contains("p_value"));
    for (auto lang : {"zh", "en"})
    {
        QFile f(QFINDTESTDATA(qPrintable(QString("../resources/i18n/") + lang + ".json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto words = QJsonDocument::fromJson(f.readAll()).object();
        for (const auto &p : entry->descriptor.parameters)
        {
            QCOMPARE(normalized.parameters[p.key], p.defaultValue);
            QVERIFY2(words.contains("parameter." + p.key), qPrintable(p.key));
            QVERIFY2(words.contains("parameterhelp." + p.key), qPrintable(p.key));
            QVERIFY(words.contains("parametergroup." + p.group));
            for (auto c : p.choices)
                QVERIFY2(words.contains("choice." + p.key + "." + c), qPrintable(p.key + "." + c));
        }
        for (auto c : entry->descriptor.category)
            QVERIFY(words.contains("category." + c));
        for (auto o : entry->descriptor.outputs)
            QVERIFY(words.contains("output.coco_scan." + o.key));
        for (auto w : r.warnings)
            QVERIFY(words.contains(w));
        for (auto data : r.outputs)
            if (const auto *t = dataAs<TableData>(data))
                for (auto c : t->columns)
                    QVERIFY2(words.contains("column." + c), qPrintable(c));
    }
    auto p = smallParameters();
    p["coco_target"] = "waltham_midpoint";
    p["coco_max_frequency"] = .08;
    auto m = task(proxy(), p);
    QCOMPARE(m.state, NodeState::Succeeded);
    QCOMPARE(m.report["periods_ka"].toArray().size(), 7);
    QVERIFY(m.warnings.contains("warning.target_envelope"));
}
void CocoAudit::workflowPersistence()
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("input_series", {0, 0}).nodeId;
    auto child = model.addChild(root, ChildSlot::Down, "coco_scan", smallParameters());
    QVERIFY(child.ok());
    auto source = makeData(proxy());
    const auto original = encodeData(source).bytes;
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"coco proxy", source, {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(encodeData(source).bytes, original);
    QTemporaryDir dir;
    ResultRepository db;
    QVERIFY(db.open(dir.filePath("results.sqlite")).isEmpty());
    auto saved = db.save(run);
    QVERIFY(saved.saved());
    auto restored = db.load(saved.recordId);
    QVERIFY(restored.ok());
    const auto out = run.nodes.value(child.nodeId);
    QCOMPARE(restored.run.nodes.value(child.nodeId).report, out.report);
    for (auto it = out.outputs.cbegin(); it != out.outputs.cend(); ++it)
    {
        QCOMPARE(encodeData(restored.run.nodes.value(child.nodeId).outputs[it.key()]).bytes,
                 encodeData(it.value()).bytes);
        QVERIFY(exportDataCsv(it.value(), dir.filePath(it.key() + ".csv")).ok());
    }
}
int runCocoAudit(int argc, char **argv)
{
    CocoAudit t;
    return QTest::qExec(&t, argc, argv);
}
#include "coco_audit.moc"
