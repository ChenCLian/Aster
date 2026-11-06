// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "cycle_tuning_audit.h"
#include "math/astronomy/milankovitch_periods.h"
#include "math/chronology/cycle_age.h"
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
#include <numeric>
#include <limits>
using namespace aster;
using namespace aster::chronology;
namespace
{
std::vector<double> v(const QJsonValue &x)
{
    std::vector<double> r;
    for (auto y : x.toArray())
        r.push_back(y.toDouble());
    return r;
}
bool near(const std::vector<double> &a, const std::vector<double> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!std::isfinite(a[i]) || std::abs(a[i] - b[i]) > 1e-11 + 5e-12 * std::abs(b[i]))
        {
            qWarning() << i << a[i] << b[i];
            return false;
        }
    return true;
}
bool rangeNear(const astronomy::PeriodRange &r, const QJsonValue &expected)
{
    return near({r.lower, r.upper}, v(expected));
}
QJsonObject reference()
{
    QFile f(QFINDTESTDATA("fixtures/cycle_tuning_reference.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
SeriesData proxy()
{
    return {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
            {0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0},
            {"Depth", "m"},
            {"Proxy", "u"}};
}
NodeResult task(const QString &key, const DataRef &input = {}, const QJsonObject &p = {})
{
    auto registry = makeScientificRegistry();
    auto t = registry.create(key, p);
    if (!t)
        return NodeResult::failure("Invalid test parameters");
    auto r = t->execute({input}, {});
    if (r.state == NodeState::Succeeded)
    {
        auto e = registry.validateOutput(registry.find(key)->descriptor, r);
        if (!e.isEmpty())
            return NodeResult::failure(e);
    }
    return r;
}
template <class F> bool fails(Problem p, F f)
{
    try
    {
        f();
    }
    catch (const Failure &e)
    {
        return e.problem == p;
    }
    return false;
}
} // namespace
class CycleTuningAudit : public QObject
{
    Q_OBJECT
  private slots:
    void theoryReference_data();
    void theoryReference();
    void publishedValuesAndLimits();
    void cyclesReference_data();
    void cyclesReference();
    void analyticAndAcycleConventions();
    void invalidAndCancelled();
    void modelUnitConversion_data();
    void modelUnitConversion();
    void taskContractsAndTranslations();
    void workflowPersistenceAndExports();
};
void CycleTuningAudit::theoryReference_data()
{
    QTest::addColumn<QJsonObject>("data");
    const auto j = reference();
    QVERIFY(!j.isEmpty());
    for (auto x : j["theory"].toArray())
        QTest::newRow(qPrintable(QString::number(x.toObject()["age_ma"].toDouble(), 'g', 12)))
            << x.toObject();
}
void CycleTuningAudit::theoryReference()
{
    QFETCH(QJsonObject, data);
    const auto r = astronomy::milankovitchPeriods(data["age_ma"].toDouble());
    for (int i = 0; i < 10; ++i)
        QVERIFY(rangeNear(r.periodKa[std::size_t(i)], data["periods"].toArray()[i]));
    QVERIFY(rangeNear(r.lunarDistanceKm, data["distance_km"]));
    QVERIFY(rangeNear(r.dayHours, data["day_hours"]));
    QVERIFY(rangeNear(r.meanObliquityDegrees, data["mean_tilt_deg"]));
    QVERIFY(rangeNear(r.axialPrecessionKa, data["axial_ka"]));
    // Original calculator uses 3.14159 in cosine only; its period expression cancels
    // this cosine exactly. Day length differs slightly; do not hide that difference.
    const auto legacy = v(data["legacy_day_hours"]);
    QVERIFY(std::abs(r.dayHours.lower - legacy[0]) < 5e-6);
    QVERIFY(std::abs(r.dayHours.upper - legacy[1]) < 5e-6);
}
void CycleTuningAudit::publishedValuesAndLimits()
{
    const auto r = astronomy::milankovitchPeriods(300);
    QVERIFY(std::abs(r.lunarDistanceKm.midpoint() / 1000 - 376.6) < .05);
    QVERIFY(std::abs(r.dayHours.midpoint() - 22.88) < .005);
    QVERIFY(std::abs(r.meanObliquityDegrees.midpoint() - 22.69) < .005);
    // At 300 Ma, the author webpage initial static P4 text says 17.88,
    // but changing age and recalculating 300 gives 17.80, matching its formula.
    const double centers[] = {35.5, 21.7, 20.60, 17.66, 17.80};
    const double tolerance[] = {.05, .05, .005, .005, .005};
    for (int i = 0; i < 5; ++i)
        QVERIFY(std::abs(r.periodKa[std::size_t(i)].midpoint() - centers[i]) < tolerance[i]);
    const auto zero = astronomy::milankovitchPeriods(0);
    QVERIFY(std::abs(zero.lunarDistanceKm.midpoint() - 384399) < 1e-6);
    for (int i = 0; i <= 4000; ++i)
    {
        const auto m = astronomy::milankovitchPeriods(i);
        for (auto p : m.periodKa)
            QVERIFY(p.lower > 0 && p.upper >= p.lower && std::isfinite(p.upper));
        for (int k = 5; k < 10; ++k)
        {
            QCOMPARE(m.periodKa[std::size_t(k)].lower, zero.periodKa[std::size_t(k)].lower);
            QCOMPARE(m.periodKa[std::size_t(k)].upper, zero.periodKa[std::size_t(k)].upper);
        }
    }
    for (double age : {-1., 4000.001, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()})
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, astronomy::milankovitchPeriods(age));
    for (double age : {1e-300, 1e-15, 1e-9})
    {
        const auto small = astronomy::milankovitchPeriods(age);
        QVERIFY(std::abs(small.periodKa[0].midpoint() - zero.periodKa[0].midpoint()) < 1e-9);
    }
}
void CycleTuningAudit::cyclesReference_data()
{
    QTest::addColumn<QJsonObject>("data");
    const auto j = reference();
    QVERIFY(!j.isEmpty());
    for (auto x : j["cycles"].toArray())
        QTest::newRow(qPrintable(x.toObject()["name"].toString())) << x.toObject();
}
void CycleTuningAudit::cyclesReference()
{
    QFETCH(QJsonObject, data);
    CycleOptions o;
    o.period = data["period"].toDouble();
    o.firstAge = data["first"].toDouble();
    o.troughs = data["troughs"].toBool();
    o.minimumRelief = data["minimum_relief"].toDouble();
    o.minimumDistance = data["minimum_distance"].toDouble();
    o.extendEnds = data["extend"].toBool();
    const auto r = cycleAge(v(data["x"]), v(data["y"]), o);
    QVERIFY(near({r.indices.begin(), r.indices.end()}, v(data["indices"])));
    QVERIFY(near(r.relief, v(data["relief"])));
    QVERIFY(near(r.anchorAge, v(data["anchor_age"])));
    QVERIFY(near(r.modelDepth, v(data["model_depth"])));
    QVERIFY(near(r.modelAge, v(data["model_age"])));
    QVERIFY(near(r.intervalRate, v(data["interval_rates"])));
    QCOMPARE(r.candidateCount, std::size_t(data["candidates"].toInt()));
    QCOMPARE(r.plateauCount, std::size_t(data["plateaus"].toInt()));
}
void CycleTuningAudit::analyticAndAcycleConventions()
{
    const auto s = proxy();
    std::vector<double> x(s.x.begin(), s.x.end()), y(s.y.begin(), s.y.end());
    const auto originalX = x, originalY = y;
    CycleOptions o;
    const auto a = cycleAge(x, y, o);
    QCOMPARE(a.indices, std::vector<std::size_t>({1, 5, 9}));
    QVERIFY(near(a.anchorAge, {41, 82, 123}));
    // Acycle getpks agrees on strictly isolated extrema. We deliberately merge plateaus.
    std::vector<std::size_t> acycle;
    for (std::size_t i = 1; i + 1 < y.size(); ++i)
        if (y[i] >= y[i - 1] && y[i] >= y[i + 1])
            acycle.push_back(i);
    QCOMPARE(a.indices, acycle);
    o.extendEnds = true;
    const auto extended = cycleAge(x, y, o);
    QVERIFY(near(extended.modelAge, {30.75, 41, 82, 123, 133.25}));
    QVERIFY(near(extended.intervalRate, {4. / 41, 4. / 41, 4. / 41, 4. / 41}));
    QCOMPARE(extended.anchorAge, a.anchorAge);
    QCOMPARE(x, originalX);
    QCOMPARE(y, originalY);
    o.troughs = true;
    const auto trough = cycleAge(x, y, o);
    QCOMPARE(trough.indices, std::vector<std::size_t>({3, 7}));
    for (auto &value : y)
        value = -value;
    o.troughs = false;
    const auto mirrored = cycleAge(x, y, o);
    QCOMPARE(mirrored.indices, trough.indices);
    QCOMPARE(mirrored.modelAge, trough.modelAge);
    // Translating/scaling depth only changes physical depth/rate, not the assigned cycle ages.
    for (auto &value : x)
        value = value * 100 + 1000;
    const auto scaled = cycleAge(x, y, o);
    QCOMPARE(scaled.modelAge, mirrored.modelAge);
    for (std::size_t i = 0; i < scaled.intervalRate.size(); ++i)
        QVERIFY(std::abs(scaled.intervalRate[i] / mirrored.intervalRate[i] - 100) < 1e-12);
    auto raw = task("cycle_age_model", makeData(s));
    QCOMPARE(raw.state, NodeState::Succeeded);
    const auto *model = dataAs<SeriesData>(raw.outputs["age_model"]);
    QVERIFY(model);
    QCOMPARE(model->x.size(), 3);
    QCOMPARE(model->y.front(), 41.);
    QVERIFY(raw.warnings.contains("warning.cycle_count_assumption"));
    QVERIFY(!raw.report["filtered"].toBool());
    QVERIFY(
        !raw.outputs.contains("peaks")); // Chronology markers are not spectral-frequency DB peaks.
}
void CycleTuningAudit::invalidAndCancelled()
{
    const std::vector<double> x{0, 1, 2, 3, 4}, y{0, 1, 0, 1, 0};
    CycleOptions o;
    QVERIFY(fails(Problem::Shape, [&] { cycleAge(x, {1, 2}, o); }));
    QVERIFY(fails(Problem::CycleAnchors, [&] { cycleAge({0, 1}, {0, 1}, o); }));
    QVERIFY(fails(Problem::CycleAnchors, [&] { cycleAge(x, {1, 1, 1, 1, 1}, o); }));
    QVERIFY(fails(Problem::CycleAnchors, [&] { cycleAge(x, {0, 1, 2, 1, 0}, o); }));
    QVERIFY(fails(Problem::DepthOrder, [&] { cycleAge({0, 1, 1, 3, 4}, y, o); }));
    QVERIFY(fails(Problem::DepthOrder, [&] { cycleAge({4, 3, 2, 1, 0}, y, o); }));
    QVERIFY(fails(Problem::NumericRange, [&] { cycleAge(x, {0, 1, qQNaN(), 1, 0}, o); }));
    QVERIFY(fails(Problem::NumericRange,
                  [&] { cycleAge(x, {-1e308, 1e308, -1e308, 1e308, -1e308}, o); }));
    o.period = 0;
    QVERIFY(fails(Problem::Shape, [&] { cycleAge(x, y, o); }));
    o.period = 41;
    o.firstAge = 1e308;
    QVERIFY(fails(Problem::NumericRange, [&] { cycleAge(x, y, o); }));
    o.firstAge = 41;
    o.minimumDistance = 3;
    QVERIFY(fails(Problem::CycleAnchors, [&] { cycleAge(x, y, o); }));
    o.minimumDistance = 0;
    o.minimumRelief = 2;
    QVERIFY(fails(Problem::CycleAnchors, [&] { cycleAge(x, y, o); }));
    o.minimumRelief = 0;
    QVERIFY_THROWS_EXCEPTION(Cancelled, cycleAge(x, y, o, [] { return true; }));
    std::vector<double> longX(400001), longY(longX.size());
    std::iota(longX.begin(), longX.end(), 0.);
    for (std::size_t i = 0; i < longX.size(); ++i)
        longY[i] = double(i % 2);
    QVERIFY(fails(Problem::Budget, [&] { cycleAge(longX, longY, o); }));
    int calls = 0;
    QVERIFY_THROWS_EXCEPTION(Cancelled, cycleAge(longX, longY, o, [&] { return ++calls > 430; }));
    std::vector<double> large(maxSamples + 1);
    QVERIFY(fails(Problem::Budget, [&] { cycleAge(large, large, o); }));
    auto invalid = proxy();
    invalid.xAxis.unit = "ka";
    QCOMPARE(task("cycle_age_model", makeData(invalid)).diagnostic().code,
             QString("CHRONOLOGY_UNIT"));
    QCOMPARE(task("cycle_age_model", makeData(TableData{{"a"}, {{1}}})).state, NodeState::Failed);
    QCOMPARE(
        task("cycle_age_model", makeData(GridData{{0, 1}, {0, 1}, {1, 2, 3, 4}, 2, 2, {}, {}, {}}))
            .state,
        NodeState::Failed);
}
void CycleTuningAudit::modelUnitConversion_data()
{
    QTest::addColumn<QString>("unit");
    QTest::addColumn<double>("factor");
    QTest::newRow("yr") << QString("yr") << 1000.;
    QTest::newRow("ka") << QString("ka") << 1.;
    QTest::newRow("Ma") << QString("Ma") << .001;
}
void CycleTuningAudit::modelUnitConversion()
{
    QFETCH(QString, unit);
    QFETCH(double, factor);
    const auto r = task("cycle_age_model", makeData(proxy()),
                        {{"cycle_period_source", "model_midpoint"},
                         {"chrono_age_unit", unit},
                         {"target_component", "p3"},
                         {"target_age_ma", 600}});
    QCOMPARE(r.state, NodeState::Succeeded);
    const double period = astronomy::milankovitchPeriods(600).periodKa[3].midpoint() * factor;
    const auto *s = dataAs<SeriesData>(r.outputs["age_model"]);
    QVERIFY(s);
    QCOMPARE(s->yAxis.unit, unit);
    QVERIFY(std::abs(s->y.front() / period - 1) < 1e-14);
    QVERIFY(std::abs(s->y.back() / period - 3) < 1e-14);
    QVERIFY(r.warnings.contains("warning.cycle_model_midpoint"));
    QCOMPARE(r.report["target_model"].toObject()["component"].toString(), QString("p3"));
}
void CycleTuningAudit::taskContractsAndTranslations()
{
    const auto registry = makeScientificRegistry();
    for (auto lang : {"zh", "en"})
    {
        QFile f(QFINDTESTDATA(qPrintable(QString("../resources/i18n/") + lang + ".json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto words = QJsonDocument::fromJson(f.readAll()).object();
        for (auto key : {"cycle_age_model", "milankovitch_period"})
        {
            const auto *e = registry.find(key);
            QVERIFY(e);
            const auto normalized = registry.normalize(key, {});
            QVERIFY(normalized.validation.ok());
            QVERIFY(words.contains("method." + QString(key)));
            for (const auto &cat : e->descriptor.category)
                QVERIFY(words.contains("category." + cat));
            for (const auto &p : e->descriptor.parameters)
            {
                QCOMPARE(normalized.parameters[p.key], p.defaultValue);
                QVERIFY2(words.contains("parameter." + p.key), qPrintable(p.key));
                QVERIFY(words.contains("parameterhelp." + p.key));
                QVERIFY(words.contains("parametergroup." + p.group));
                for (auto c : p.choices)
                    QVERIFY2(words.contains("choice." + p.key + "." + c),
                             qPrintable(p.key + "." + c));
                for (auto it = p.visibleWhen.constBegin(); it != p.visibleWhen.constEnd(); ++it)
                    QVERIFY(normalized.parameters.contains(it.key()));
            }
            for (auto p : e->descriptor.outputs)
                QVERIFY(words.contains("output." + QString(key) + "." + p.key));
            const auto result =
                task(key, QString(key) == "cycle_age_model" ? makeData(proxy()) : DataRef{});
            QCOMPARE(result.state, NodeState::Succeeded);
            for (auto warn : result.warnings)
                QVERIFY(words.contains(warn));
            for (auto data : result.outputs)
                if (auto t = dataAs<TableData>(data))
                    for (auto col : t->columns)
                        QVERIFY2(words.contains("column." + col), qPrintable(col));
        }
    }
    const auto a = registry.normalize("cycle_age_model", {});
    QCOMPARE(a.parameters["cycle_period"].toDouble(), 41.);
    QCOMPARE(a.parameters["cycle_extend_ends"].toBool(), false);
    QVERIFY(!registry.normalize("milankovitch_period", {{"target_age_ma", 5000}}).validation.ok());
    QVERIFY(!registry.normalize("cycle_age_model", {{"cycle_period", 0}}).validation.ok());
    const auto calculator = registry.find("milankovitch_period");
    QVERIFY(!calculator->descriptor.inputKind.has_value());
    for (auto component :
         {"obliquity", "p1", "p2", "p3", "p4", "e405", "e95", "e124", "e99", "e131"})
    {
        const auto r = task("milankovitch_period", {}, {{"target_component", component}});
        QCOMPARE(r.state, NodeState::Succeeded);
        const auto *t = dataAs<TableData>(r.outputs["target_period"]);
        QVERIFY(t);
        QCOMPARE(t->rows.size(), 1);
        QCOMPARE(t->columns.size(), 4);
        QVERIFY(t->columns[1].startsWith(QString(component) + "_"));
        QVERIFY(t->rows[0][1] <= t->rows[0][2] && t->rows[0][2] <= t->rows[0][3]);
    }
}
void CycleTuningAudit::workflowPersistenceAndExports()
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("input_series", {0, 0}).nodeId;
    auto child =
        model.addChild(root, ChildSlot::Down, "cycle_age_model", {{"cycle_extend_ends", true}});
    QVERIFY(child.ok());
    auto follow = model.addChild(child.nodeId, ChildSlot::Down, "linear_detrend");
    QVERIFY(follow.ok());
    auto source = makeData(proxy());
    const auto original = encodeData(source).bytes;
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"cycles", source, {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(encodeData(source).bytes, original);
    QTemporaryDir dir;
    ResultRepository db;
    QVERIFY(db.open(dir.filePath("results.sqlite")).isEmpty());
    auto saved = db.save(run);
    QVERIFY(saved.saved());
    auto restored = db.load(saved.recordId);
    QVERIFY(restored.ok());
    const auto output = run.nodes.value(child.nodeId);
    QCOMPARE(restored.run.nodes.value(child.nodeId).report, output.report);
    for (auto it = output.outputs.cbegin(); it != output.outputs.cend(); ++it)
    {
        QCOMPARE(encodeData(restored.run.nodes.value(child.nodeId).outputs[it.key()]).bytes,
                 encodeData(it.value()).bytes);
        QVERIFY(exportDataCsv(it.value(), dir.filePath(it.key() + ".csv")).ok());
    }
    auto second = model.addRoot("input_series", {8, 0}).nodeId;
    auto applied = model.addChild(second, ChildSlot::Down, "apply_age_model",
                                  {{"chrono_model_file", dir.filePath("age_model.csv")}});
    QVERIFY(applied.ok());
    auto detrend = model.addChild(applied.nodeId, ChildSlot::Down, "linear_detrend");
    QVERIFY(detrend.ok());
    auto next =
        WorkflowExecutor().run({model.freeze().snapshot, second, {"original proxy", source, {}}});
    QCOMPARE(next.state, RunState::Succeeded);
    auto aged = dataAs<SeriesData>(next.nodes.value(applied.nodeId).outputs["aged_series"]);
    QVERIFY(aged);
    QCOMPARE(aged->y, proxy().y);
    QVERIFY(!next.nodes.contains(root));
    auto calculator = model.addRoot("milankovitch_period", {16, 0}, {{"target_component", "e405"}});
    QVERIFY(calculator.ok());
    auto independent =
        WorkflowExecutor().run({model.freeze().snapshot, calculator.nodeId, {"target", {}, {}}});
    QCOMPARE(independent.state, RunState::Succeeded);
    saved = db.save(independent);
    QVERIFY(saved.saved());
    restored = db.load(saved.recordId);
    QVERIFY(restored.ok());
    const auto expected = independent.nodes.value(calculator.nodeId);
    for (auto it = expected.outputs.cbegin(); it != expected.outputs.cend(); ++it)
    {
        QCOMPARE(encodeData(restored.run.nodes.value(calculator.nodeId).outputs[it.key()]).bytes,
                 encodeData(it.value()).bytes);
        QVERIFY(exportDataCsv(it.value(), dir.filePath(it.key() + ".csv")).ok());
    }
    // A failed count is recorded; descendants block instead of receiving invalid output.
    auto bad = proxy();
    bad.y.fill(1);
    run = WorkflowExecutor().run({model.freeze().snapshot, root, {"constant", makeData(bad), {}}});
    QCOMPARE(run.nodes.value(child.nodeId).state, NodeState::Failed);
    QCOMPARE(run.nodes.value(child.nodeId).diagnostic().code, QString("CYCLE_ANCHORS"));
    QVERIFY(run.nodes.value(child.nodeId).outputs.isEmpty());
    QCOMPARE(run.nodes.value(follow.nodeId).state, NodeState::Blocked);
}
int runCycleTuningAudit(int argc, char **argv)
{
    CycleTuningAudit t;
    return QTest::qExec(&t, argc, argv);
}
#include "cycle_tuning_audit.moc"
