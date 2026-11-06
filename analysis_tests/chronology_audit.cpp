// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "chronology_audit.h"
#include "math/chronology/age_model.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "io/files/series_file_reader.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <cmath>
#include <limits>
using namespace aster;
using namespace aster::chronology;
namespace
{
std::vector<double> v(const QJsonValue &value)
{
    std::vector<double> out;
    for (const auto &x : value.toArray())
        out.push_back(x.toDouble());
    return out;
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
bool write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
SeriesData proxy()
{
    return {{0, .5, 1, 2}, {2, 4, 1, 3}, {"Depth", "m"}, {"Proxy", "u"}};
}
NodeResult task(const char *key, const SeriesData &data, const QJsonObject &p = {})
{
    auto r = makeScientificRegistry();
    auto t = r.create(key, p);
    if (!t)
        return NodeResult::failure("Invalid test parameters");
    auto out = t->execute({makeData(data)}, {});
    if (out.state == NodeState::Succeeded)
    {
        auto error = r.validateOutput(r.find(key)->descriptor, out);
        if (!error.isEmpty())
            return NodeResult::failure(error);
    }
    return out;
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
class ChronologyAudit : public QObject
{
    Q_OBJECT
  private slots:
    void reference_data();
    void reference();
    void analyticIdentities();
    void invalidDataAndNumericRange();
    void cancellationAndBudgets();
    void unitConversions_data();
    void unitConversions();
    void parserParity_data();
    void parserParity();
    void controlFilesAndSnapshots();
    void defaultsTranslationsContracts();
    void workflowDatabaseCsv();
};
void ChronologyAudit::reference_data()
{
    QTest::addColumn<QJsonObject>("data");
    QFile f(QFINDTESTDATA("fixtures/chronology_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    for (auto c : QJsonDocument::fromJson(f.readAll()).object()["cases"].toArray())
        QTest::newRow(qPrintable(c.toObject()["name"].toString())) << c.toObject();
}
void ChronologyAudit::reference()
{
    QFETCH(QJsonObject, data);
    if (data["kind"] == "rate")
    {
        auto r = integrateRate(
            v(data["x"]), v(data["rate"]), data["factor"].toDouble(), data["first_age"].toDouble(),
            data["rule"] == "left" ? RateRule::LeftConstant : RateRule::LinearRate);
        QVERIFY(near(r.age, v(data["age"])));
    }
    else
    {
        auto r =
            mapDepth(v(data["x"]), v(data["depth"]), v(data["controls_age"]), Boundary::Linear);
        QVERIFY(near(r.age, v(data["age"])));
        QVERIFY(near(r.intervalRate, v(data["interval_rate"])));
        QCOMPARE(r.extrapolated, std::size_t(data["extrapolated"].toInt()));
    }
}
void ChronologyAudit::analyticIdentities()
{
    const auto left = integrateRate({10, 11, 13}, {2, 2, 999}, 100, 7, RateRule::LeftConstant);
    QVERIFY(near(left.age, {7, 57, 157}));
    QCOMPARE(left.minimumIncrement, 50.);
    QCOMPARE(left.maximumIncrement, 100.);
    auto linear = integrateRate({0, 1}, {1, 2}, 1, 0, RateRule::LinearRate);
    QVERIFY(std::abs(linear.age.back() - std::log(2.)) < 1e-14);
    auto subdiv = integrateRate({0, .25, .75, 1}, {1, 1.25, 1.75, 2}, 1, 0, RateRule::LinearRate);
    QVERIFY(std::abs(subdiv.age.back() - linear.age.back()) < 1e-14);
    auto reverse = integrateRate({0, 1}, {2, 1}, 1, 0, RateRule::LinearRate);
    QCOMPARE(linear.age, reverse.age);
    auto map = mapDepth({10, 11, 13}, {10, 11, 13}, left.age, Boundary::Reject);
    QCOMPARE(map.age, left.age);
    QVERIFY(near(map.intervalRate, {.02, .02}));
    auto affine = mapDepth({-2, 0, 1, 10}, {0, 5}, {4, 14}, Boundary::Linear);
    QVERIFY(near(affine.age, {0, 4, 6, 24}));
    QCOMPARE(affine.extrapolated, std::size_t(2));
    // Multiplication/division must not overflow merely because of evaluation order.
    auto large = integrateRate({0, 1e300}, {1e300, 1e300}, 1e100, 0, RateRule::LeftConstant);
    QVERIFY(std::abs(large.age.back() / 1e100 - 1) < 1e-14);
    auto small = integrateRate({0, 1e-300}, {1e-300, 1e-300}, 1e-100, 0, RateRule::LeftConstant);
    QVERIFY(std::abs(small.age.back() / 1e-100 - 1) < 1e-14);
    auto contrast = integrateRate({0, 1}, {1e-300, 1e300}, 1, 0, RateRule::LinearRate);
    QVERIFY(std::abs(contrast.age.back() / (600 * std::log(10.) * 1e-300) - 1) < 1e-13);
}
void ChronologyAudit::invalidDataAndNumericRange()
{
    auto rate = [](std::vector<double> d, std::vector<double> r, double start = 0)
    { return integrateRate(d, r, 1, start, RateRule::LeftConstant); };
    QVERIFY(fails(Problem::Shape, [&] { rate({0}, {1}); }));
    QVERIFY(fails(Problem::Shape, [&] { rate({0, 1}, {1}); }));
    for (auto x : std::vector<std::vector<double>>{{0, 0}, {1, 0}})
        QVERIFY(fails(Problem::DepthOrder, [&] { rate(x, {1, 1}); }));
    for (double r : {0., -1., std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN()})
        QVERIFY(fails(Problem::RatePositive, [&] { rate({0, 1}, {1, r}); }));
    QVERIFY(fails(Problem::NumericRange, [&] { rate({0, 1}, {1, 1}, 1e20); }));
    QVERIFY(fails(Problem::NumericRange, [&] { rate({0, 1e300}, {1e-300, 1}); }));
    QVERIFY(fails(Problem::NumericRange, [&] { rate({0, 1e-300}, {1e300, 1}); }));
    QVERIFY(fails(Problem::NumericRange, [&] { rate({-1e308, 1e308}, {1, 1}); }));
    QVERIFY(
        fails(Problem::Shape, [&] { integrateRate({0, 1}, {1, 1}, 0, 0, RateRule::LinearRate); }));
    QVERIFY(fails(Problem::Shape,
                  [&] { integrateRate({0, 1}, {1, 1}, 1, 0, static_cast<RateRule>(42)); }));
    QVERIFY(fails(Problem::AgeOrder, [&] { mapDepth({0, 1}, {0, 1}, {2, 2}, Boundary::Reject); }));
    QVERIFY(fails(Problem::AgeOrder, [&] { mapDepth({0, 1}, {0, 1}, {2, 1}, Boundary::Reject); }));
    QVERIFY(
        fails(Problem::DepthOrder, [&] { mapDepth({0, 1}, {0, 0}, {0, 1}, Boundary::Reject); }));
    QVERIFY(fails(Problem::Shape, [&] { mapDepth({0, 1}, {0}, {0}, Boundary::Linear); }));
    QVERIFY(fails(Problem::OutsideModel,
                  [&] { mapDepth({-.1, 1}, {0, 1}, {0, 1}, Boundary::Reject); }));
    QVERIFY(fails(Problem::NumericRange,
                  [&] { mapDepth({0, 1e-20, 1}, {0, 1}, {1e10, 1e10 + 1}, Boundary::Reject); }));
}
void ChronologyAudit::cancellationAndBudgets()
{
    QVERIFY_EXCEPTION_THROWN(
        integrateRate({0, 1}, {1, 1}, 1, 0, RateRule::LeftConstant, [] { return true; }),
        Cancelled);
    QVERIFY_EXCEPTION_THROWN(
        mapDepth({0, 1}, {0, 1}, {0, 1}, Boundary::Reject, [] { return true; }), Cancelled);
    std::vector<double> d(16384), r(d.size(), 1);
    for (std::size_t i = 0; i < d.size(); ++i)
        d[i] = double(i);
    int calls = 0;
    QVERIFY_EXCEPTION_THROWN(
        integrateRate(d, r, 1, 0, RateRule::LinearRate, [&] { return ++calls >= 36; }), Cancelled);
    calls = 0;
    QVERIFY_EXCEPTION_THROWN(
        mapDepth(d, {0, 20000}, {0, 20000}, Boundary::Reject, [&] { return ++calls >= 23; }),
        Cancelled);
    std::vector<double> big(maxSamples + 1, 1);
    QVERIFY(fails(Problem::Budget, [&] { integrateRate(big, big, 1, 0, RateRule::LeftConstant); }));
    std::vector<double> controls(maxControlPoints + 1, 1);
    QVERIFY(
        fails(Problem::Budget, [&] { mapDepth({0, 1}, controls, controls, Boundary::Linear); }));
    ExecutionContext c;
    c.cancellation.cancel();
    for (const auto *key : {"sedrate_to_age", "apply_age_model"})
    {
        auto t = makeScientificRegistry().create(key, {});
        QCOMPARE(t->execute({makeData(proxy())}, c).state, NodeState::Cancelled);
    }
}
void ChronologyAudit::unitConversions_data()
{
    QTest::addColumn<QString>("depth");
    QTest::addColumn<double>("dz");
    QTest::addColumn<QString>("rate");
    QTest::addColumn<double>("speed");
    QTest::addColumn<QString>("age");
    QTest::addColumn<double>("expected");
    QTest::newRow("m-cm-ka") << QString("m") << 1. << QString("cm/ka") << 2. << QString("ka")
                             << 50.;
    QTest::newRow("cm-m-yr") << QString("cm") << 100. << QString("m/ka") << 2. << QString("yr")
                             << 500.;
    QTest::newRow("mm-mm-Ma") << QString("mm") << 1000. << QString("mm/yr") << 1. << QString("Ma")
                              << .001;
    QTest::newRow("ft-cm-ka") << QString("ft") << 1. << QString("cm/ka") << 30.48 << QString("ka")
                              << 1.;
    QTest::newRow("km-m-ka") << QString("km") << 1. << QString("m/Ma") << 2. << QString("ka")
                             << 500000.;
}
void ChronologyAudit::unitConversions()
{
    QFETCH(QString, depth);
    QFETCH(double, dz);
    QFETCH(QString, rate);
    QFETCH(double, speed);
    QFETCH(QString, age);
    QFETCH(double, expected);
    SeriesData s{{7, 7 + dz}, {speed, speed}, {"Depth", depth}, {"SAR", rate}};
    auto p = QJsonObject{{"chrono_depth_unit", depth},
                         {"chrono_rate_unit", rate},
                         {"chrono_age_unit", age},
                         {"chrono_first_age", 3}};
    auto r = task("sedrate_to_age", s, p);
    QCOMPARE(r.state, NodeState::Succeeded);
    const auto *m = dataAs<SeriesData>(r.outputs["age_model"]);
    QVERIFY(m);
    QVERIFY(std::abs(m->y.back() - (3 + expected)) < 1e-10 + expected * 1e-12);
    QCOMPARE(m->x, s.x);
    QCOMPARE(m->yAxis.unit, age);
    QCOMPARE(m->xAxis.unit, depth);
    s.xAxis.unit = "ka";
    QCOMPARE(task("sedrate_to_age", s, p).diagnostic().code, QString("CHRONOLOGY_UNIT"));
    s.xAxis.unit = "";
    s.yAxis.unit = "";
    r = task("sedrate_to_age", s, p);
    QCOMPARE(r.state, NodeState::Succeeded);
    QVERIFY(r.warnings.contains("warning.chronology_declared_units"));
}
void ChronologyAudit::parserParity_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("delimiter");
    QTest::addColumn<bool>("autoHeader");
    QTest::addColumn<bool>("header");
    QTest::addColumn<bool>("success");
    QTest::newRow("bom-crlf")
        << QByteArray("\xef\xbb\xbf# note\r\n\"depth\",\"age\",name\r\n0,2,a\r\n1,3,b\r\n")
        << QString("auto") << true << false << true;
    QTest::newRow("plain") << QByteArray("0 2\n1 3\n") << QString("whitespace") << true << false
                           << true;
    QTest::newRow("tab") << QByteArray("d\ta\n0\t2\n1\t3\n") << QString("tab") << false << true
                         << true;
    QTest::newRow("numeric-consumed-header")
        << QByteArray("0,2\n1,3\n2,4\n") << QString("comma") << false << true << true;
    QTest::newRow("wrong-header") << QByteArray("d,a\n0,2\n1,3\n") << QString("comma") << false
                                  << false << false;
    QTest::newRow("bad-numeric") << QByteArray("d,a\n0,2\n1,nan\n") << QString("auto") << true
                                 << false << false;
    QTest::newRow("bad-quotes") << QByteArray("d,a\n0,2\n1,\"3\n") << QString("comma") << false
                                << true << false;
}
void ChronologyAudit::parserParity()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, delimiter);
    QFETCH(bool, autoHeader);
    QFETCH(bool, header);
    QFETCH(bool, success);
    QTemporaryDir dir;
    auto path = dir.filePath("控制表.csv");
    QVERIFY(write(path, bytes));
    SeriesFileOptions o;
    o.delimiter = delimiter;
    o.autoHeader = autoHeader;
    o.header = header;
    const auto a = readSeriesFile(path, o), b = readSeriesBytes(bytes, path, o);
    QCOMPARE(a.ok(), success);
    QCOMPARE(a.ok(), b.ok());
    QCOMPARE(a.error, b.error);
    if (success)
    {
        QCOMPARE(encodeData(a.data).bytes, encodeData(b.data).bytes);
        auto report = a.report;
        report.remove("modified_utc");
        QCOMPARE(report, b.report);
    }
    else
        QCOMPARE(a.report, b.report);
    o.maxRows = 2;
    QVERIFY(!readSeriesBytes("0,1\n1,2\n2,3\n", path,
                             SeriesFileOptions{"comma", false, false, {}, {}, 0, 1, false, 2})
                 .ok());
}
void ChronologyAudit::controlFilesAndSnapshots()
{
    QTemporaryDir dir;
    const auto path = dir.filePath("年龄 model.csv");
    const QByteArray bytes = "depth,age,name\n0,10,start\n100,12,middle\n200,16,end\n";
    QVERIFY(write(path, bytes));
    QJsonObject p{{"chrono_model_file", path}, {"chrono_model_depth_unit", "cm"}};
    auto source = proxy();
    auto result = task("apply_age_model", source, p);
    QCOMPARE(result.state, NodeState::Succeeded);
    const auto *out = dataAs<SeriesData>(result.outputs["aged_series"]);
    QVERIFY(out);
    QCOMPARE(out->x, QVector<double>({10, 11, 12, 16}));
    QCOMPARE(out->y, source.y);
    QCOMPARE(
        result.report["control_file"].toObject()["sha256"].toString(),
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    const auto old = encodeData(result.outputs["age_model"]).bytes;
    QVERIFY(write(path, "0,20\n100,22\n200,26\n"));
    auto again = task("apply_age_model", source, p);
    QCOMPARE(again.state, NodeState::Succeeded);
    QVERIFY(result.report["control_file"].toObject()["sha256"] !=
            again.report["control_file"].toObject()["sha256"]);
    QCOMPARE(encodeData(result.outputs["age_model"]).bytes, old);
    source.x[0] = -.5;
    const auto coverage = task("apply_age_model", source, p).diagnostic();
    QCOMPARE(coverage.code, QString("CHRONOLOGY_COVERAGE"));
    QVERIFY(!coverage.context.contains("point")); // No fictitious point zero for range errors.
    p["chrono_boundary"] = "linear";
    again = task("apply_age_model", source, p);
    QCOMPARE(again.state, NodeState::Succeeded);
    QCOMPARE(again.report["extrapolated_samples"].toInt(), 1);
    p["chrono_age_column"] = 1;
    QCOMPARE(task("apply_age_model", source, p).diagnostic().code, QString("CHRONOLOGY_COLUMNS"));
    p.remove("chrono_age_column");
    QVERIFY(write(path, "0,20\n100,18\n200,26\n"));
    QCOMPARE(task("apply_age_model", source, p).diagnostic().code, QString("CHRONOLOGY_AGE_ORDER"));
    QVERIFY(write(path, "0,20\n"));
    QCOMPARE(task("apply_age_model", source, p).state, NodeState::Failed);
    QVERIFY(write(path, QByteArray(4 * 1024 * 1024 + 1, ' ')));
    QCOMPARE(task("apply_age_model", source, p).diagnostic().code, QString("CHRONOLOGY_BUDGET"));
    QVERIFY(QFile::remove(path));
    QCOMPARE(task("apply_age_model", source, p).state, NodeState::Failed);
    QCOMPARE(task("apply_age_model", proxy()).diagnostic().code, QString("CHRONOLOGY_MODEL_FILE"));
    SeriesData rates{{0, 1}, {1, 1}, {"Depth", "m"}, {"Rate", "cm/kyr"}};
    QCOMPARE(task("sedrate_to_age", rates).state, NodeState::Succeeded);
}
void ChronologyAudit::defaultsTranslationsContracts()
{
    const auto registry = makeScientificRegistry();
    for (const auto *lang : {"zh", "en"})
    {
        QFile f(QFINDTESTDATA(qPrintable(QString("../resources/i18n/") + lang + ".json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto words = QJsonDocument::fromJson(f.readAll()).object();
        for (const auto *key : {"sedrate_to_age", "apply_age_model"})
        {
            const auto *entry = registry.find(key);
            QVERIFY(entry);
            auto n = registry.normalize(key, {});
            QVERIFY(n.validation.ok());
            QVERIFY(words.contains("method." + QString(key)));
            for (const auto &p : entry->descriptor.parameters)
            {
                QCOMPARE(n.parameters[p.key], p.defaultValue);
                QVERIFY2(words.contains("parameter." + p.key), qPrintable(p.key));
                QVERIFY2(words.contains("parameterhelp." + p.key), qPrintable(p.key));
                QVERIFY(words.contains("parametergroup." + p.group));
                for (const auto &choice : p.choices)
                    QVERIFY2(words.contains("choice." + p.key + "." + choice),
                             qPrintable(p.key + "." + choice));
            }
            auto t = registry.create(key, {});
            QCOMPARE(t->execute({makeData(TableData{{"a", "b"}, {{1, 2}, {2, 3}}})}, {}).state,
                     NodeState::Failed);
            QCOMPARE(
                t->execute({makeData(GridData{{0, 1}, {0, 1}, {1, 2, 3, 4}, 2, 2, {}, {}, {}})}, {})
                    .state,
                NodeState::Failed);
        }
    }
    auto r = registry.normalize("sedrate_to_age", {});
    QCOMPARE(r.parameters["chrono_rate_rule"].toString(), QString("left"));
    auto a = registry.normalize("apply_age_model", {});
    QCOMPARE(a.parameters["chrono_boundary"].toString(), QString("reject"));
    QVERIFY(
        !registry.normalize("apply_age_model", {{"chrono_boundary", "spline"}}).validation.ok());
}
void ChronologyAudit::workflowDatabaseCsv()
{
    WorkflowModel model(makeScientificRegistry());
    auto root = model.addRoot("input_series", {0, 0}).nodeId;
    auto age = model.addChild(root, ChildSlot::Down, "sedrate_to_age");
    QVERIFY(age.ok());
    SeriesData rates{{0, 1, 2}, {100, 50, 50}, {"Depth", "m"}, {"Rate", "cm/ka"}};
    auto run =
        WorkflowExecutor().run({model.freeze().snapshot, root, {"rates", makeData(rates), {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QTemporaryDir dir;
    auto path = dir.filePath("age.csv");
    QVERIFY(exportDataCsv(run.nodes[age.nodeId].outputs["age_model"], path).ok());
    auto proxyRoot = model.addRoot("input_series", {10, 0}).nodeId;
    auto apply = model.addChild(proxyRoot, ChildSlot::Down, "apply_age_model",
                                {{"chrono_model_file", path}});
    QVERIFY(apply.ok());
    auto next = model.addChild(apply.nodeId, ChildSlot::Down, "linear_detrend");
    QVERIFY(next.ok());
    auto input = makeData(proxy());
    const auto original = encodeData(input).bytes;
    auto applied =
        WorkflowExecutor().run({model.freeze().snapshot, proxyRoot, {"proxy", input, {}}});
    QCOMPARE(applied.state, RunState::Succeeded);
    QCOMPARE(encodeData(input).bytes, original);
    QVERIFY(!applied.nodes.contains(root)); // Independent root does not consume this binding.
    ResultRepository db;
    QVERIFY(db.open(dir.filePath("test.sqlite")).isEmpty());
    auto saved = db.save(applied);
    QVERIFY(saved.saved());
    auto restored = db.load(saved.recordId);
    QVERIFY(restored.ok());
    for (auto id : {apply.nodeId, next.nodeId})
    {
        QCOMPARE(restored.run.nodes[id].report, applied.nodes[id].report);
        for (auto it = applied.nodes[id].outputs.begin(); it != applied.nodes[id].outputs.end();
             ++it)
        {
            QCOMPARE(encodeData(restored.run.nodes[id].outputs[it.key()]).bytes,
                     encodeData(it.value()).bytes);
            QVERIFY(exportDataCsv(it.value(), dir.filePath(it.key() + ".csv")).ok());
        }
    }
    // Stored controls and ages remain available after the source control file disappears.
    QVERIFY(QFile::remove(path));
    restored = db.load(saved.recordId);
    QVERIFY(restored.ok());
    QVERIFY(restored.run.nodes[apply.nodeId].outputs["age_model"]);
    applied = WorkflowExecutor().run({model.freeze().snapshot, proxyRoot, {"proxy", input, {}}});
    QCOMPARE(applied.nodes[apply.nodeId].state, NodeState::Failed);
    QCOMPARE(applied.nodes[next.nodeId].state, NodeState::Blocked);
    QVERIFY(!applied.nodes[apply.nodeId].diagnostic().isEmpty());
}
int runChronologyAudit(int argc, char **argv)
{
    ChronologyAudit t;
    return QTest::qExec(&t, argc, argv);
}
#include "chronology_audit.moc"
