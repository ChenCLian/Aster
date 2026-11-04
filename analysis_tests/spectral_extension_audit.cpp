#include "spectral_extension_audit.h"
#include "math/spectrum/lomb_scargle.h"
#include "math/filtering/butterworth.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include "io/database/data_codec.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace aster;
namespace
{
constexpr double pi = 3.14159265358979323846;
std::vector<double> vector(const QJsonValue &value)
{
    std::vector<double> v;
    for (const auto &x : value.toArray())
        v.push_back(x.toDouble());
    return v;
}
bool near(const std::vector<double> &a, const std::vector<double> &b, double absolute = 1e-10,
          double relative = 5e-9)
{
    if (a.size() != b.size())
    {
        qWarning() << "Size" << a.size() << b.size();
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!std::isfinite(a[i]) || std::abs(a[i] - b[i]) > absolute + relative * std::abs(b[i]))
        {
            qWarning() << "Mismatch" << i << QString::number(a[i], 'g', 17)
                       << QString::number(b[i], 'g', 17) << "diff" << a[i] - b[i];
            return false;
        }
    return true;
}
SeriesData sample(int n = 256, bool irregular = false)
{
    SeriesData s;
    s.xAxis = {"Age", "ka"};
    s.yAxis = {"Proxy", "u"};
    double t = -50;
    for (int i = 0; i < n; ++i)
    {
        t += irregular ? 1 + .2 * std::sin(.7 * i) : 1.;
        s.x << t;
        s.y << 3 + std::sin(2 * pi * t / 16) + .2 * std::cos(2 * pi * t / 7);
    }
    return s;
}
NodeResult task(const char *key, const SeriesData &s, const QJsonObject &p = {})
{
    auto registry = makeScientificRegistry();
    auto t = registry.create(key, p);
    if (!t)
        return NodeResult::failure("Invalid test parameters");
    auto result = t->execute({makeData(s)}, {});
    if (result.state == NodeState::Succeeded)
    {
        auto error = registry.validateOutput(registry.find(key)->descriptor, result);
        if (!error.isEmpty())
            return NodeResult::failure(error);
    }
    return result;
}
} // namespace
class SpectralExtensionAudit : public QObject
{
    Q_OBJECT
  private slots:
    void reference_data();
    void reference();
    void lombInvariantsAndRanks();
    void lombValidationAndCancellation();
    void butterworthIdentities();
    void butterworthValidationAndCancellation();
    void defaultsTranslationsAndContracts();
    void workflowDatabaseCsv();
};
void SpectralExtensionAudit::reference_data()
{
    QTest::addColumn<QJsonObject>("data");
    QFile f(QFINDTESTDATA("fixtures/spectral_extension_reference.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    int i = 0;
    for (auto c : QJsonDocument::fromJson(f.readAll()).object()["cases"].toArray())
        QTest::newRow(qPrintable(c.toObject()["kind"].toString() + QString::number(i++)))
            << c.toObject();
}
void SpectralExtensionAudit::reference()
{
    QFETCH(QJsonObject, data);
    const auto y = vector(data["y"]);
    if (data["kind"] == "lomb")
    {
        numeric::LombOptions o;
        o.minimumFrequency = data["minimum"].toDouble();
        o.maximumFrequency = data["maximum"].toDouble();
        o.samplesPerPeak = data["samples_per_peak"].toInt();
        const auto r = numeric::lombScargle(vector(data["x"]), y, o);
        QVERIFY(near(r.frequency, vector(data["frequency"]), 1e-12, 1e-12));
        QVERIFY(near(r.power, vector(data["power"])));
    }
    else
    {
        const auto r = filtering::butterworth(
            y, data["dt"].toDouble(), data["cutoff"].toDouble(), data["order"].toInt(),
            data["kind"] == "highpass" ? filtering::PassType::High : filtering::PassType::Low);
        const bool edge = data["edge_stress"].toBool();
        QVERIFY(
            near(r.filtered, vector(data["filtered"]), edge ? 2e-7 : 1e-10, edge ? 2e-7 : 5e-9));
        QVERIFY(near(r.frequency, vector(data["frequency"]), 1e-12, 1e-12));
        QVERIFY(near(r.gain, vector(data["gain"]), edge ? 2e-7 : 1e-10, edge ? 2e-7 : 5e-9));
        QCOMPARE(r.padding, std::size_t(3 * (data["order"].toInt() + 1)));
    }
}
void SpectralExtensionAudit::lombInvariantsAndRanks()
{
    using namespace numeric;
    std::vector<double> x(64), y(64);
    for (int i = 0; i < 64; ++i)
    {
        x[i] = i;
        y[i] = 8 + std::sin(2 * pi * i / 8);
    }
    LombOptions o;
    o.minimumFrequency = .125;
    o.maximumFrequency = .4;
    auto r = lombScargle(x, y, o);
    QVERIFY(std::abs(r.power.front() - 1) < 1e-12);
    auto shifted = x, scaled = y;
    for (int i = 0; i < 64; ++i)
    {
        shifted[i] = 1024 + 2 * x[i];
        scaled[i] = -3 * y[i] + 20;
    }
    auto o2 = o;
    o2.minimumFrequency /= 2;
    o2.maximumFrequency /= 2;
    auto s = lombScargle(shifted, scaled, o2);
    QVERIFY(near(r.power, s.power, 2e-12, 2e-12));
    for (std::size_t i = 0; i < r.frequency.size(); ++i)
        QVERIFY(std::abs(r.frequency[i] / 2 - s.frequency[i]) < 1e-14);
    for (int i = 0; i < 64; ++i)
    {
        y[i] = (i % 2) ? 1 : -1;
        scaled[i] = 1e15 + y[i];
    }
    o.minimumFrequency = .5;
    o.maximumFrequency = 1;
    o.samplesPerPeak = 2;
    r = lombScargle(x, y, o);
    s = lombScargle(x, scaled, o);
    QVERIFY(near(r.power, s.power, 1e-12, 1e-12));
    QVERIFY(r.rankOneBins >= 1);
    QVERIFY(r.rankZeroBins >= 1);
    QVERIFY(std::abs(r.power.front() - 1) < 1e-12);
    QVERIFY(r.power.back() < 1e-10);
    // A large absolute X origin is removed before evaluating phases.
    for (auto &t : x)
        t += 1e12;
    s = lombScargle(x, y, o);
    QVERIFY(near(r.power, s.power, 1e-12, 1e-12));
    for (auto &v : scaled)
        v = 0;
    for (int i = 0; i < 64; ++i)
        scaled[i] = y[i] * 1.7e308;
    s = lombScargle(x, scaled, o);
    QVERIFY(near(r.power, s.power, 1e-12, 1e-12));
    auto source = sample(64, true);
    auto result = task("lomb_scargle", source);
    QCOMPARE(result.state, NodeState::Succeeded);
    const auto *output = dataAs<SpectrumData>(result.outputs["periodogram"]);
    QVERIFY(output);
    QCOMPARE(output->frequencyAxis.unit, QString("1/ka"));
    QCOMPARE(output->powerAxis.unit, QString("1"));
    QVERIFY(!result.report["resampled"].toBool());
    QVERIFY(!result.report["is_psd"].toBool());
}
void SpectralExtensionAudit::lombValidationAndCancellation()
{
    using namespace numeric;
    auto data = sample(128);
    std::vector<double> x(data.x.begin(), data.x.end()), y(data.y.begin(), data.y.end());
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, lombScargle({1, 2}, {2, 3}));
    auto bad = x;
    bad[10] = bad[9];
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, lombScargle(bad, y));
    auto nan = y;
    nan[5] = std::numeric_limits<double>::quiet_NaN();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, lombScargle(x, nan));
    QVERIFY_THROWS_EXCEPTION(std::domain_error, lombScargle(x, std::vector<double>(128, 2)));
    LombOptions o;
    o.minimumFrequency = .1;
    o.maximumFrequency = .01;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, lombScargle(x, y, o));
    o = {};
    o.minimumFrequency = 1e-12;
    o.maximumFrequency = .1;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, lombScargle(x, y, o));
    o = {};
    o.maximumFrequency = 1000;
    QVERIFY_THROWS_EXCEPTION(std::length_error, lombScargle(x, y, o));
    auto large = sample(5000);
    QVERIFY_THROWS_EXCEPTION(std::length_error,
                             lombScargle(std::vector<double>(large.x.begin(), large.x.end()),
                                         std::vector<double>(large.y.begin(), large.y.end())));
    QVERIFY_THROWS_EXCEPTION(LombCancelled, lombScargle(x, y, {}, [] { return true; }));
    int calls = 0;
    QVERIFY_THROWS_EXCEPTION(LombCancelled, lombScargle(x, y, {}, [&] { return ++calls > 30; }));
    std::vector<int> progress;
    lombScargle(x, y, {}, {}, [&](int n) { progress.push_back(n); });
    QVERIFY(progress.size() <= 101);
    QCOMPARE(progress.back(), 100);
    QVERIFY(std::is_sorted(progress.begin(), progress.end()));
}
void SpectralExtensionAudit::butterworthIdentities()
{
    using namespace filtering;
    for (int order = 1; order <= 10; ++order)
        for (auto type : {PassType::Low, PassType::High})
        {
            std::vector<double> y(128, 1.7e308);
            auto r = butterworth(y, 1, .1, order, type);
            for (auto value : r.filtered)
                QCOMPARE(value, type == PassType::Low ? 1.7e308 : 0.);
            auto it = std::find(r.frequency.begin(), r.frequency.end(), .1);
            QVERIFY(it != r.frequency.end());
            QVERIFY(std::abs(r.gain[std::distance(r.frequency.begin(), it)] - .5) < 1e-10);
            QCOMPARE(r.gain.front(), type == PassType::Low ? 1. : 0.);
            QCOMPARE(r.gain.back(), type == PassType::Low ? 0. : 1.);
        }
    std::vector<double> y(4096);
    for (int i = 0; i < 4096; ++i)
        y[i] = 4 + std::sin(2 * pi * i / 128) + .7 * std::sin(2 * pi * i / 8);
    for (auto type : {PassType::Low, PassType::High})
    {
        const double cutoff = 1. / 32;
        auto r = butterworth(y, 1, cutoff, 4, type);
        for (double f : {1. / 128, 1. / 8})
        {
            double sine = 0, cosine = 0;
            for (int i = 1024; i < 3072; ++i)
            {
                sine += r.filtered[i] * std::sin(2 * pi * f * i);
                cosine += r.filtered[i] * std::cos(2 * pi * f * i);
            }
            sine /= 1024;
            cosine /= 1024;
            const auto ratio = type == PassType::Low ? std::tan(pi * f) / std::tan(pi * cutoff)
                                                     : std::tan(pi * cutoff) / std::tan(pi * f);
            const auto gain = 1 / (1 + std::pow(ratio, 8));
            QVERIFY(std::abs(sine - (f == 1. / 128 ? 1 : .7) * gain) < 1e-9);
            QVERIFY(std::abs(cosine) < 1e-9);
        }
    }
}
void SpectralExtensionAudit::butterworthValidationAndCancellation()
{
    using namespace filtering;
    std::vector<double> y(128);
    for (int i = 0; i < 128; ++i)
        y[i] = std::sin(.2 * i);
    for (int order : {1, 4, 10})
    {
        const auto pad = 3 * (order + 1);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, butterworth(std::vector<double>(pad, 1), 1,
                                                                    .05, order, PassType::Low));
        QCOMPARE(butterworth(std::vector<double>(pad + 1, 1), 1, .05, order, PassType::Low)
                     .filtered.size(),
                 std::size_t(pad + 1));
    }
    for (double f : {0., .5, -.1, 1e-9})
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, butterworth(y, 1, f, 4, PassType::Low));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, butterworth(y, 1, .1, 11, PassType::Low));
    auto bad = y;
    bad[2] = std::numeric_limits<double>::infinity();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, butterworth(bad, 1, .1, 4, PassType::High));
    QVERIFY_THROWS_EXCEPTION(std::length_error,
                             butterworth(std::vector<double>(ButterworthLimits::maxSamples + 1, 1),
                                         1, .1, 4, PassType::Low));
    QVERIFY_THROWS_EXCEPTION(ButterworthCancelled,
                             butterworth(y, 1, .1, 4, PassType::Low, [] { return true; }));
    int calls = 0;
    QVERIFY_THROWS_EXCEPTION(ButterworthCancelled,
                             butterworth(y, 1, .1, 4, PassType::Low, [&] { return ++calls > 5; }));
    for (int i = 0; i < 128; ++i)
        y[i] = (i < 64 ? -1 : 1) * 1.7e308;
    QVERIFY_THROWS_EXCEPTION(std::overflow_error, butterworth(y, 1, .1, 4, PassType::Low));
}
void SpectralExtensionAudit::defaultsTranslationsAndContracts()
{
    const auto registry = makeScientificRegistry();
    for (const auto *lang : {"zh", "en"})
    {
        QFile file(QFINDTESTDATA(qPrintable(QString("../resources/i18n/") + lang + ".json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto words = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto *key : {"lomb_scargle", "butterworth_lowpass", "butterworth_highpass"})
        {
            const auto *entry = registry.find(key);
            QVERIFY(entry);
            QVERIFY(words.contains("method." + QString(key)));
            auto normalized = registry.normalize(key, {});
            QVERIFY(normalized.validation.ok());
            for (const auto &p : entry->descriptor.parameters)
            {
                QCOMPARE(normalized.parameters[p.key], p.defaultValue);
                QVERIFY(words.contains("parameter." + p.key));
                QVERIFY(words.contains("parameterhelp." + p.key));
                QVERIFY(words.contains("parametergroup." + p.group));
            }
            auto t = registry.create(key, {});
            auto r = t->execute(
                {makeData(GridData{{0, 1}, {0, 1}, {1, 2, 3, 4}, 2, 2, {}, {}, {}})}, {});
            QCOMPARE(r.state, NodeState::Failed);
            auto source = sample();
            source.x[10] = source.x[9];
            QCOMPARE(task(key, source).state, NodeState::Failed);
            source = sample(256, true);
            QCOMPARE(task(key, source).state,
                     QString(key) == "lomb_scargle" ? NodeState::Succeeded : NodeState::Failed);
            source = sample(4);
            r = task(key, source);
            QCOMPARE(r.state, NodeState::Failed);
            QVERIFY(!r.diagnostic().isEmpty());
        }
    }
    QCOMPARE(task("butterworth_lowpass", sample(), {{"butter_cutoff", .5}}).state,
             NodeState::Failed);
    auto y = sample();
    y.y.fill(2);
    QCOMPARE(task("lomb_scargle", y).state, NodeState::Failed);
}
void SpectralExtensionAudit::workflowDatabaseCsv()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto lomb = model.addChild(root, ChildSlot::Left, "lomb_scargle");
    const auto low = model.addChild(root, ChildSlot::Down, "butterworth_lowpass");
    const auto high = model.addChild(root, ChildSlot::Right, "butterworth_highpass");
    QVERIFY(lomb.ok() && low.ok() && high.ok());
    const auto envelope = model.addChild(low.nodeId, ChildSlot::Down, "hilbert_envelope");
    QVERIFY(envelope.ok());
    const auto source = makeData(sample());
    const auto before = encodeData(source).bytes;
    auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"test", source, {}}});
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(encodeData(source).bytes, before);
    QTemporaryDir dir;
    ResultRepository db;
    QVERIFY(db.open(dir.filePath("test.sqlite")).isEmpty());
    const auto saved = db.save(run);
    QVERIFY(saved.saved());
    const auto loaded = db.load(saved.recordId);
    QVERIFY(loaded.ok());
    for (const auto &node : {lomb.nodeId, low.nodeId, high.nodeId, envelope.nodeId})
        for (auto it = run.nodes[node].outputs.begin(); it != run.nodes[node].outputs.end(); ++it)
        {
            QCOMPARE(encodeData(it.value()).bytes,
                     encodeData(loaded.run.nodes[node].outputs[it.key()]).bytes);
            auto path = dir.filePath(node.toString() + it.key() + ".csv");
            QVERIFY(exportDataCsv(it.value(), path).ok());
            QVERIFY(QFileInfo(path).size() > 100);
        }
    QVERIFY(model.setParameters(low.nodeId, {{"butter_cutoff", 1}}).ok());
    run = WorkflowExecutor().run({model.freeze().snapshot, root, {"invalid", source, {}}});
    QCOMPARE(run.nodes[low.nodeId].state, NodeState::Failed);
    QCOMPARE(run.nodes[envelope.nodeId].state, NodeState::Blocked);
    QCOMPARE(run.nodes[lomb.nodeId].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[high.nodeId].state, NodeState::Succeeded);
}
int runSpectralExtensionAudit(int argc, char **argv)
{
    SpectralExtensionAudit t;
    return QTest::qExec(&t, argc, argv);
}
#include "spectral_extension_audit.moc"
