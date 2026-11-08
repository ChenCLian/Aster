// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include "core/batch_workflow.h"
#include "model/workflow_codec.h"
#include "processing/builtin_tasks.h"

using namespace aster;
namespace {
QUuid identity(int n)
{ return QUuid(QString("00000000-0000-4000-8000-%1").arg(n, 12, 16, QLatin1Char('0'))); }
DataRef data(double value)
{ return makeData(SeriesData{{0, 1, 2}, {value, value + 1, value + 2}, {"age", "ka"}, {"proxy", "u"}}); }
class FunctionTask : public Task {
public:
    using Function = std::function<NodeResult(const TaskInput&, const ExecutionContext&)>;
    explicit FunctionTask(Function function) : f(std::move(function)) {}
    NodeResult execute(const TaskInput& input, const ExecutionContext& context) override { return f(input, context); }
    Function f;
};
TaskRegistry registry(FunctionTask::Function pair = {}, FunctionTask::Function upstream = {})
{
    auto r = makeBuiltinRegistry();
    auto d = AffineTask::descriptor(); d.key = "two_outputs"; d.parameters.clear();
    d.outputs = {{"series", "Series", DataKind::Series}, {"trend", "Trend", DataKind::Series, false}};
    d.outputs.push_back({"table", "Optional table", DataKind::Table, false});
    auto result = r.add(d, [upstream](const QJsonObject&) {
        return std::make_unique<FunctionTask>(upstream ? upstream : FunctionTask::Function(
            [](const TaskInput& input, const ExecutionContext&) {
                const auto* s = dataAs<SeriesData>(input.data);
                if (s->y[0] < 0) return NodeResult::failure("Selected upstream data failed");
                auto trend = *s; for (auto& y : trend.y) y *= 10;
                QMap<QString, DataRef> outputs{{"series", input.data}};
                if (s->y[0] != 9) outputs.insert("trend", makeData(trend));
                return NodeResult::success(outputs);
            }));
    });
    if (!result.ok()) throw std::logic_error(result.message().toStdString());
    d.key = "pair_probe"; d.pairedInput = true;
    d.outputs = {{"series", "Pair output", DataKind::Series}};
    result = r.add(d, [pair](const QJsonObject&) {
        return std::make_unique<FunctionTask>(pair ? pair : FunctionTask::Function(
            [](const TaskInput& input, const ExecutionContext& context) {
                auto s = *dataAs<SeriesData>(input.data); const auto* b = dataAs<SeriesData>(input.second);
                for (qsizetype i = 0; i < s.y.size(); ++i) s.y[i] -= b->y[i];
                if (context.progress) context.progress(50, "pair_probe");
                return NodeResult::success({{"series", makeData(s)}}, {{"inputs", context.sourceMetadata}});
            }));
    });
    if (!result.ok()) throw std::logic_error(result.message().toStdString());
    return r;
}
struct Fixture {
    WorkflowModel model;
    NodeId root, upstream, paired, downstream, ordinary;
    QVector<BatchDataset> datasets;
    BatchGrouping groups;
    Fixture(FunctionTask::Function pair = {}, FunctionTask::Function producer = {}) : model(registry(pair, producer)) {
        root = model.addRoot("input_series", {0, 0}).nodeId;
        upstream = model.addChild(root, ChildSlot::Down, "two_outputs").nodeId;
        paired = model.addChild(upstream, ChildSlot::Down, "pair_probe").nodeId;
        downstream = model.addChild(paired, ChildSlot::Down, "affine", {{"factor", 2}}).nodeId;
        ordinary = model.addChild(upstream, ChildSlot::Right, "affine").nodeId;
        for (int i = 1; i <= 4; ++i) datasets.push_back({identity(i), {"same-name.csv", data(i), {{"test_source", i}}}});
        groups[paired] = {{identity(101), "first", {identity(1), "series"}, {identity(2), "series"}},
                          {identity(102), "second", {identity(3), "series"}, {identity(2), "series"}}};
    }
    BatchPlanResult plan() const { return prepareBatch(model.freeze().snapshot, root, datasets, groups); }
};
struct Capture {
    QMap<QPair<NodeId, QUuid>, NodeResult> outputs;
    QVector<BatchEvent> events;
    std::mutex lock;
    BatchObserver observer() {
        return {[&](const BatchEvent& e) { std::lock_guard<std::mutex> guard(lock); events.push_back(e); },
                [&](const BatchJob& job, const NodeResult& r) { outputs.insert({job.node, job.item}, r); }};
    }
    NodeResult result(NodeId node, QUuid item) const { return outputs.value({node, item}); }
};
}

class BatchTests : public QObject {
    Q_OBJECT
private slots:
    void groupingRoundTrip();
    void malformedGrouping_data();
    void malformedGrouping();
    void planValidation_data();
    void planValidation();
    void sameBatchPairing_data();
    void sameBatchPairing();
    void namedOutputsAndSameDataset();
    void swappedOrderIsDistinct();
    void frozenPlanAndReordering();
    void unrelatedRootIsExcluded();
    void outOfOrderCompletion();
    void sourceFailureOnlyBlocksAffectedGroups();
    void missingOptionalOutputDoesNotSubstitute();
    void missingSecondOutputDoesNotSubstitute();
    void pairExceptions_data();
    void pairExceptions();
    void cancellation_data();
    void cancellation();
    void throwingObservers();
    void cacheLimitAndRelease();
    void sharedPayloadIsCountedOnce();
    void unusedOutputsDoNotConsumeCache();
    void parallelismIsBounded();
    void lateSharedReferenceDoesNotFillCache();
    void oldExecutorRejectsPair();
    void ordinaryExecutionMatchesLegacy();
    void independentBatchesAreIsolated();
    void generatedSourceRunsOnce();
    void invalidExecutionOptions();
    void aggregateUnassignedCandidatesAreBounded();
};

void BatchTests::aggregateUnassignedCandidatesAreBounded()
{
    WorkflowModel model(registry());
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    QVector<BatchDataset> datasets;
    const auto input = data(1);
    for (int i = 0; i < 1000; ++i)
        datasets.push_back({identity(i + 1), {"input", input, {}}});
    BatchGrouping groups;
    auto parent = root;
    for (int i = 0; i < 51; ++i) {
        const auto producer = model.addChild(parent, ChildSlot::Down, "two_outputs");
        QVERIFY(producer.ok());
        const auto joint = model.addChild(producer.nodeId, ChildSlot::Left, "pair_probe");
        QVERIFY(joint.ok());
        groups[joint.nodeId] = {{identity(2000 + i), "", {datasets[0].id, "series"},
                                                        {datasets[1].id, "series"}}};
        parent = producer.nodeId;
        if (i == 49) {
            const auto plan = prepareBatch(model.freeze().snapshot, root, datasets, groups);
            QVERIFY2(plan.validation.ok(), qPrintable(plan.validation.message()));
            qsizetype total = 0;
            for (const auto& unused : plan.snapshot->unassigned()) total += unused.size();
            QCOMPARE(total, 99900);
        }
    }
    const auto plan = prepareBatch(model.freeze().snapshot, root, datasets, groups);
    QVERIFY(!plan.validation.ok());
    QVERIFY(plan.validation.message().contains("Unassigned output limit"));
    QVERIFY(!plan.snapshot);
}

void BatchTests::groupingRoundTrip()
{
    Fixture f;
    auto read = batchGroupingFromJson(batchGroupingToJson(f.groups));
    QVERIFY(read.validation.ok());
    QCOMPARE(batchGroupingToJson(read.grouping), batchGroupingToJson(f.groups));
    QCOMPARE(read.grouping[f.paired][0].a.dataset, identity(1));
    QCOMPARE(read.grouping[f.paired][0].b.dataset, identity(2));
    QVERIFY(batchGroupingFromJson(batchGroupingToJson({})).validation.ok());
}
void BatchTests::malformedGrouping_data()
{
    QTest::addColumn<QString>("caseName");
    for (const auto& name : {"version", "extra_top", "array", "duplicate_node", "duplicate_id", "null_id",
                            "bad_ref", "missing_b", "self_pair", "duplicate_pair", "extra_ref", "extra_pair", "pair_limit"})
        QTest::newRow(name) << QString(name);
}
void BatchTests::malformedGrouping()
{
    QFETCH(QString, caseName); Fixture f;
    auto json = batchGroupingToJson(f.groups); auto nodes = json["nodes"].toArray();
    auto node = nodes[0].toObject(); auto pairs = node["pairs"].toArray(); auto p = pairs[0].toObject();
    if (caseName == "version") json["schema_version"] = 1.5;
    else if (caseName == "extra_top") json["future"] = true;
    else if (caseName == "array") json["nodes"] = "bad";
    else if (caseName == "duplicate_node") { nodes.append(node); json["nodes"] = nodes; }
    else {
        if (caseName == "duplicate_id") { auto p2 = pairs[1].toObject(); p2["id"] = p["id"]; pairs[1] = p2; }
        else if (caseName == "null_id") p["id"] = "";
        else if (caseName == "bad_ref") p["a"] = 1;
        else if (caseName == "missing_b") p.remove("b");
        else if (caseName == "self_pair") p["b"] = p["a"];
        else if (caseName == "duplicate_pair") { auto p2 = p; p2["id"] = identity(200).toString(); pairs[1] = p2; }
        else if (caseName == "extra_ref") { auto ref = p["a"].toObject(); ref["node"] = f.root.toString(); p["a"] = ref; }
        else if (caseName == "extra_pair") p["node"] = f.root.toString();
        else if (caseName == "pair_limit") { pairs = {}; for (int i = 0; i <= BatchPairLimit; ++i) pairs.append(p); }
        pairs[0] = p; node["pairs"] = pairs; nodes[0] = node; json["nodes"] = nodes;
    }
    const auto read = batchGroupingFromJson(json);
    QVERIFY(!read.validation.ok()); QVERIFY(read.grouping.isEmpty());
}
void BatchTests::planValidation_data()
{
    QTest::addColumn<QString>("caseName");
    for (const auto& name : {"empty_data", "null_id", "duplicate_id", "missing_group", "empty_group", "foreign_dataset",
                            "missing_port", "wrong_kind", "foreign_node", "ordinary_node", "duplicate_group_id",
                            "group_source_collision", "duplicate_pair", "self_pair", "null_group_id", "non_root",
                            "nested_pair", "dataset_limit", "job_limit"}) QTest::newRow(name) << QString(name);
}
void BatchTests::planValidation()
{
    QFETCH(QString, caseName); Fixture f; NodeId root = f.root;
    if (caseName == "empty_data") f.datasets.clear();
    else if (caseName == "null_id") f.datasets[0].id = {};
    else if (caseName == "duplicate_id") f.datasets[1].id = f.datasets[0].id;
    else if (caseName == "missing_group") f.groups.clear();
    else if (caseName == "empty_group") f.groups[f.paired].clear();
    else if (caseName == "foreign_dataset") f.groups[f.paired][0].a.dataset = identity(999);
    else if (caseName == "missing_port") f.groups[f.paired][0].a.port = "absent";
    else if (caseName == "wrong_kind") f.groups[f.paired][0].a.port = "table";
    else if (caseName == "foreign_node") f.groups[identity(555)] = f.groups.take(f.paired);
    else if (caseName == "ordinary_node") f.groups[f.ordinary] = f.groups.take(f.paired);
    else if (caseName == "duplicate_group_id") f.groups[f.paired][1].id = f.groups[f.paired][0].id;
    else if (caseName == "group_source_collision") f.groups[f.paired][0].id = f.datasets[0].id;
    else if (caseName == "duplicate_pair") { auto p = f.groups[f.paired][0]; p.id = identity(202); f.groups[f.paired][1] = p; }
    else if (caseName == "self_pair") f.groups[f.paired][0].b = f.groups[f.paired][0].a;
    else if (caseName == "null_group_id") f.groups[f.paired][0].id = {};
    else if (caseName == "non_root") root = f.upstream;
    else if (caseName == "nested_pair") {
        const auto n = f.model.addChild(f.downstream, ChildSlot::Down, "pair_probe"); QVERIFY(n.ok());
        f.groups[n.nodeId] = {{identity(200), "nested", {identity(101), "series"}, {identity(102), "series"}}};
    }
    else if (caseName == "dataset_limit") { f.datasets.clear(); for (int i = 1; i <= BatchDatasetLimit + 1; ++i) f.datasets.push_back({identity(i), {"x", data(1), {}}}); }
    else if (caseName == "job_limit") {
        f.datasets.clear(); for (int i = 1; i <= 10000; ++i) f.datasets.push_back({identity(i + 1000), {"x", data(1), {}}});
        f.groups[f.paired] = {{identity(500), "p", {identity(1001), "series"}, {identity(1002), "series"}}};
        auto n = f.ordinary; for (int i = 0; i < 11; ++i) { const auto edit = f.model.addChild(n, ChildSlot::Right, "affine"); QVERIFY(edit.ok()); n = edit.nodeId; }
    }
    const auto result = prepareBatch(f.model.freeze().snapshot, root, f.datasets, f.groups);
    QVERIFY2(!result.validation.ok(), qPrintable(caseName)); QVERIFY(!result.snapshot);
    QVERIFY(!prepareBatch({}, f.root, f.datasets, f.groups).snapshot);
}
void BatchTests::sameBatchPairing_data()
{
    QTest::addColumn<int>("workers");
    QTest::newRow("one-worker") << 1; QTest::newRow("two-workers") << 2; QTest::newRow("eight-workers") << 8;
}
void BatchTests::sameBatchPairing()
{
    QFETCH(int, workers); Fixture f; const auto p = f.plan(); QVERIFY2(p.validation.ok(), qPrintable(p.validation.message()));
    QCOMPARE(p.snapshot->jobs().size(), 16);
    QVERIFY(p.snapshot->unassigned()[f.paired].contains(BatchSeriesRef{identity(4), "series"}));
    Capture c; const auto report = BatchWorkflowExecutor().run(p.snapshot, {workers}, c.observer());
    QCOMPARE(report.state, RunState::Succeeded); QCOMPARE(report.jobs.size(), p.snapshot->jobs().size());
    QCOMPARE(c.outputs.size(), 16);
    QCOMPARE(dataAs<SeriesData>(c.result(f.paired, identity(101)).outputs["series"])->y[0], -1.0);
    QCOMPARE(dataAs<SeriesData>(c.result(f.paired, identity(102)).outputs["series"])->y[0], 1.0);
    QCOMPARE(dataAs<SeriesData>(c.result(f.downstream, identity(101)).outputs["series"])->y[0], -2.0);
    QCOMPARE(c.result(f.ordinary, identity(4)).state, NodeState::Succeeded);
    for (const auto& job : report.jobs) { QCOMPARE(job.state, NodeState::Succeeded); QVERIFY(job.executed); }
    for (const auto& e : c.events) { QCOMPARE(e.batchId, report.batchId); QCOMPARE(e.item, p.snapshot->jobs()[e.job].item); }
    const auto info = c.result(f.paired, identity(101)).report["inputs"].toObject()["batch_pair"].toObject();
    QCOMPARE(info["A"].toObject()["dataset_id"].toString(), identity(1).toString(QUuid::WithoutBraces));
    QCOMPARE(info["B"].toObject()["source"].toObject()["test_source"].toInt(), 2);
}
void BatchTests::namedOutputsAndSameDataset()
{
    Fixture f; f.groups[f.paired] = {{identity(101), "two outputs", {identity(1), "trend"}, {identity(1), "series"}}};
    const auto p = f.plan(); QVERIFY(p.validation.ok()); Capture c;
    QCOMPARE(BatchWorkflowExecutor().run(p.snapshot, {}, c.observer()).state, RunState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(c.result(f.paired, identity(101)).outputs["series"])->y[0], 9.0);
}
void BatchTests::swappedOrderIsDistinct()
{
    Fixture f; auto reversed = f.groups[f.paired][0]; reversed.id = identity(103); std::swap(reversed.a, reversed.b);
    f.groups[f.paired].append(reversed); auto p = f.plan(); QVERIFY(p.validation.ok()); Capture c;
    QCOMPARE(BatchWorkflowExecutor().run(p.snapshot, {2}, c.observer()).state, RunState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(c.result(f.paired, identity(103)).outputs["series"])->y[0], 1.0);
}
void BatchTests::frozenPlanAndReordering()
{
    Fixture f; auto old = f.plan(); QVERIFY(old.validation.ok());
    std::reverse(f.datasets.begin(), f.datasets.end()); auto reordered = f.plan(); QVERIFY(reordered.validation.ok());
    f.datasets[0].source.label = "renamed"; f.datasets[0].source.data = data(100);
    f.groups[f.paired][0].b.dataset = identity(3);
    QVERIFY(f.model.setParameters(f.downstream, {{"factor", 99}}).ok());
    Capture a, b; BatchWorkflowExecutor executor;
    QCOMPARE(executor.run(old.snapshot, {2}, a.observer()).state, RunState::Succeeded);
    QCOMPARE(executor.run(reordered.snapshot, {2}, b.observer()).state, RunState::Succeeded);
    for (const auto& pair : {identity(101), identity(102)})
        QCOMPARE(dataAs<SeriesData>(a.result(f.downstream, pair).outputs["series"])->y,
                 dataAs<SeriesData>(b.result(f.downstream, pair).outputs["series"])->y);
    QCOMPARE(dataAs<SeriesData>(a.result(f.downstream, identity(101)).outputs["series"])->y[0], -2.0);
    const auto manifest = old.snapshot->manifest();
    QCOMPARE(manifest["datasets"].toArray()[0].toObject()["id"].toString(), identity(1).toString(QUuid::WithoutBraces));
    QCOMPARE(manifest["grouping"].toObject(), batchGroupingToJson(old.snapshot->grouping()));
}
void BatchTests::unrelatedRootIsExcluded()
{
    Fixture f; auto extra = f.model.addRoot("sine_source", {8, 0}); QVERIFY(extra.ok());
    auto p = f.plan(); QVERIFY(p.validation.ok()); QCOMPARE(p.snapshot->jobs().size(), 16);
    const auto snapshot = workflowFromJson(p.snapshot->manifest()["workflow"].toObject());
    QVERIFY(snapshot.validation.ok()); QCOMPARE(snapshot.definition.nodes.size(), 5);
    for (const auto& n : snapshot.definition.nodes) QVERIFY(n.id != extra.nodeId);
    f.groups[extra.nodeId] = f.groups[f.paired]; QVERIFY(!f.plan().snapshot);
}
void BatchTests::outOfOrderCompletion()
{
    std::mutex mutex; std::condition_variable changed; bool fastFinished = false;
    Fixture f({}, [&](const TaskInput& input, const ExecutionContext&) {
        const auto value = dataAs<SeriesData>(input.data)->y[0];
        if (value == 1) { std::unique_lock<std::mutex> lock(mutex); if (!changed.wait_for(lock, std::chrono::seconds(3), [&]{return fastFinished;})) return NodeResult::failure("Test synchronization timeout"); }
        return NodeResult::success({{"series", input.data}});
    });
    Capture c; auto observer = c.observer(); auto completed = observer.completed;
    QVector<QUuid> order;
    observer.completed = [&](const BatchJob& job, const NodeResult& r) {
        completed(job, r);
        if (job.node == f.upstream) {
            order.push_back(job.item);
            if (job.item == identity(2)) { {std::lock_guard<std::mutex> lock(mutex); fastFinished = true;} changed.notify_all(); }
        }
    };
    const auto p = f.plan(); QVERIFY(p.validation.ok());
    QCOMPARE(BatchWorkflowExecutor().run(p.snapshot, {2}, observer).state, RunState::Succeeded);
    QVERIFY(order.indexOf(identity(2)) < order.indexOf(identity(1)));
    QCOMPARE(dataAs<SeriesData>(c.result(f.paired, identity(101)).outputs["series"])->y[0], -1.0);
}
void BatchTests::sourceFailureOnlyBlocksAffectedGroups()
{
    Fixture f; f.datasets[0].source.data = data(-1); auto p = f.plan(); QVERIFY(p.validation.ok()); Capture c;
    const auto report = BatchWorkflowExecutor().run(p.snapshot, {2}, c.observer());
    QCOMPARE(report.state, RunState::PartialFailed);
    QCOMPARE(c.result(f.upstream, identity(1)).state, NodeState::Failed);
    QCOMPARE(c.result(f.paired, identity(101)).state, NodeState::Blocked);
    QCOMPARE(c.result(f.downstream, identity(101)).state, NodeState::Blocked);
    QCOMPARE(c.result(f.paired, identity(102)).state, NodeState::Succeeded);
    QCOMPARE(c.result(f.ordinary, identity(4)).state, NodeState::Succeeded);
    QVERIFY(c.result(f.paired, identity(101)).outputs.isEmpty());
}
void BatchTests::missingOptionalOutputDoesNotSubstitute()
{
    Fixture f; f.datasets[0].source.data = data(9); f.groups[f.paired][0].a.port = "trend";
    Capture c; const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {2}, c.observer());
    QCOMPARE(report.state, RunState::PartialFailed);
    const auto result = c.result(f.paired, identity(101)); QCOMPARE(result.state, NodeState::Failed);
    QCOMPARE(result.diagnostic().code, QString("INPUT_PORT_UNAVAILABLE"));
    QCOMPARE(result.diagnostic().context["role"].toString(), QString("A"));
    QCOMPARE(c.result(f.paired, identity(102)).state, NodeState::Succeeded);
}
void BatchTests::missingSecondOutputDoesNotSubstitute()
{
    Fixture f; f.datasets[1].source.data = data(9); f.groups[f.paired][0].b.port = "trend";
    Capture c; const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {2}, c.observer());
    QCOMPARE(report.state, RunState::PartialFailed);
    const auto result = c.result(f.paired, identity(101));
    QCOMPARE(result.diagnostic().code, QString("INPUT_PORT_UNAVAILABLE"));
    QCOMPARE(result.diagnostic().context["role"].toString(), QString("B"));
    QCOMPARE(c.result(f.paired, identity(102)).state, NodeState::Succeeded);
}
void BatchTests::pairExceptions_data()
{
    QTest::addColumn<QString>("fault");
    for (const auto& s : {"std_exception", "unknown_exception", "allocation", "pending", "wrong_output", "failed_output", "null_output"}) QTest::newRow(s) << QString(s);
}
void BatchTests::pairExceptions()
{
    QFETCH(QString, fault);
    Fixture f([&](const TaskInput& input, const ExecutionContext&) -> NodeResult {
        if (fault == "std_exception") throw std::runtime_error("probe");
        if (fault == "unknown_exception") throw 1;
        if (fault == "allocation") throw std::bad_alloc();
        auto result = NodeResult::success({{"series", input.data}});
        if (fault == "pending") result.state = NodeState::Pending;
        if (fault == "wrong_output") result.outputs["series"] = makeData(TableData{{"x"}, {{1}}});
        if (fault == "null_output") result.outputs["series"] = {};
        if (fault == "failed_output") result.state = NodeState::Failed;
        return result;
    });
    Capture c; const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {2}, c.observer());
    QCOMPARE(report.state, RunState::PartialFailed);
    QCOMPARE(c.result(f.paired, identity(101)).state, NodeState::Failed);
    QVERIFY(c.result(f.paired, identity(101)).outputs.isEmpty());
    QCOMPARE(c.result(f.downstream, identity(101)).state, NodeState::Blocked);
    QVERIFY(!c.result(f.paired, identity(101)).diagnostic().isEmpty());
}
void BatchTests::cancellation_data()
{
    QTest::addColumn<QString>("when");
    for (const auto& s : {"before", "running", "between", "progress"}) QTest::newRow(s) << QString(s);
}
void BatchTests::cancellation()
{
    QFETCH(QString, when); Fixture f; CancellationToken token; Capture c;
    auto observer = c.observer(); auto event = observer.event; auto completed = observer.completed;
    observer.event = [&](const BatchEvent& e) {
        event(e);
        if ((when == "running" && e.state == NodeState::Running) || (when == "progress" && e.progress >= 0)) token.cancel();
    };
    observer.completed = [&](const BatchJob& job, const NodeResult& result) {
        completed(job, result); if (when == "between" && job.node == f.upstream) token.cancel();
    };
    if (when == "before") token.cancel();
    const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {1}, observer, token);
    QCOMPARE(report.state, RunState::Cancelled); QCOMPARE(c.outputs.size(), 16);
    for (const auto& r : report.jobs) QVERIFY(r.state != NodeState::Running && r.state != NodeState::Pending);
    if (when == "before" || when == "running") for (const auto& r : report.jobs) QVERIFY(!r.executed);
}
void BatchTests::throwingObservers()
{
    Fixture f; BatchObserver observer;
    observer.event = [](const BatchEvent&) { throw std::runtime_error("event observer"); };
    observer.completed = [](const BatchJob&, const NodeResult&) { throw 1; };
    const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {4}, observer);
    QCOMPARE(report.state, RunState::Succeeded); QVERIFY(report.observerWarnings.size() >= 16);
}
void BatchTests::cacheLimitAndRelease()
{
    Fixture f; Capture c;
    auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {1, 1}, c.observer());
    QCOMPARE(report.state, RunState::Failed); QVERIFY(report.peakCachedPayloadBytes <= 1);
    QCOMPARE(c.result(f.root, identity(1)).diagnostic().code, QString("BATCH_CACHED_OUTPUT_LIMIT"));
    std::weak_ptr<const DataObject> weak;
    Fixture normal;
    BatchObserver observer; observer.completed = [&](const BatchJob& job, const NodeResult& r) {
        if (job.node == normal.upstream && job.item == identity(1)) weak = r.outputs["trend"];
    };
    report = BatchWorkflowExecutor().run(normal.plan().snapshot, {1, 4096}, observer);
    QCOMPARE(report.state, RunState::Succeeded); QVERIFY(report.peakCachedPayloadBytes <= 4096); QVERIFY(weak.expired());
}
void BatchTests::sharedPayloadIsCountedOnce()
{
    WorkflowModel model(makeBuiltinRegistry()); const auto root = model.addRoot("input_series", {0,0}).nodeId;
    const auto a = model.addChild(root, ChildSlot::Down, "affine").nodeId;
    QVERIFY(model.addChild(a, ChildSlot::Down, "affine").ok());
    const auto shared = data(1);
    QVector<BatchDataset> ds{{identity(1), {"a", shared, {}}}, {identity(2), {"b", shared, {}}}};
    const auto plan = prepareBatch(model.freeze().snapshot, root, ds, {}); QVERIFY(plan.validation.ok());
    const auto report = BatchWorkflowExecutor().run(plan.snapshot, {1, 48});
    QCOMPARE(report.state, RunState::Succeeded); QCOMPARE(report.peakCachedPayloadBytes, quint64(48));
}
void BatchTests::unusedOutputsDoNotConsumeCache()
{
    Fixture f({}, [](const TaskInput& input, const ExecutionContext&) {
        SeriesData unused; unused.x.resize(10000); unused.y.resize(10000);
        return NodeResult::success({{"series", input.data}, {"trend", makeData(unused)}});
    });
    const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {1, 4096});
    QCOMPARE(report.state, RunState::Succeeded); QVERIFY(report.peakCachedPayloadBytes <= 4096);
}
void BatchTests::parallelismIsBounded()
{
    std::atomic_int active{0}, peak{0};
    Fixture f({}, [&](const TaskInput& input, const ExecutionContext&) {
        const int current = ++active; int previous = peak.load();
        while (current > previous && !peak.compare_exchange_weak(previous, current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        --active; return NodeResult::success({{"series", input.data}});
    });
    for (int i = 5; i <= 40; ++i) f.datasets.push_back({identity(i), {"another", data(i), {}}});
    const auto report = BatchWorkflowExecutor().run(f.plan().snapshot, {2});
    QCOMPARE(report.state, RunState::Succeeded); QVERIFY(peak.load() <= 2); QCOMPARE(active.load(), 0);
}
void BatchTests::lateSharedReferenceDoesNotFillCache()
{
    Fixture f;
    for (int i = 5; i <= 40; ++i) f.datasets.push_back({identity(i), {"another", data(i), {}}});
    const auto reference = f.datasets.takeAt(1); f.datasets.push_back(reference);
    f.groups[f.paired].clear();
    for (int i = 1; i <= 40; ++i) if (i != 2)
        f.groups[f.paired].push_back({identity(100 + i), "group", {identity(i), "series"}, {identity(2), "series"}});
    const auto p = f.plan(); QVERIFY(p.validation.ok());
    const auto report = BatchWorkflowExecutor().run(p.snapshot, {1, 512});
    QCOMPARE(report.state, RunState::Succeeded); QVERIFY(report.peakCachedPayloadBytes <= 512);
}
void BatchTests::oldExecutorRejectsPair()
{
    Fixture f; RunRequest request{f.model.freeze().snapshot, f.root, f.datasets[0].source};
    const auto report = WorkflowExecutor().run(request); QCOMPARE(report.state, RunState::Invalid);
    QVERIFY(report.executionOrder.isEmpty()); QVERIFY(report.error.contains("same-batch"));
    auto r = makeBuiltinRegistry(); auto descriptor = SineSourceTask::descriptor(); descriptor.key = "invalid_pair_source"; descriptor.pairedInput = true;
    QVERIFY(!r.add(descriptor, [](const QJsonObject& p) {return std::make_unique<SineSourceTask>(p);}).ok());
}
void BatchTests::ordinaryExecutionMatchesLegacy()
{
    WorkflowModel model(makeBuiltinRegistry()); auto root = model.addRoot("input_series", {0,0}).nodeId;
    const auto a = model.addChild(root, ChildSlot::Down, "affine", {{"factor", 3}, {"offset", 2}}).nodeId;
    const auto b = model.addChild(a, ChildSlot::Down, "linear_detrend").nodeId;
    QVERIFY(model.addChild(b, ChildSlot::Right, "series_summary", {}, "trend").ok());
    QVector<BatchDataset> datasets{{identity(1), {"input", data(2), {}}}};
    const auto frozen = model.freeze().snapshot;
    const auto old = WorkflowExecutor().run({frozen, root, datasets[0].source});
    Capture c; auto plan = prepareBatch(frozen, root, datasets, {}); QVERIFY(plan.validation.ok());
    auto report = BatchWorkflowExecutor().run(plan.snapshot, {2}, c.observer());
    QCOMPARE(report.state, old.state);
    for (auto it = old.nodes.cbegin(); it != old.nodes.cend(); ++it) {
        auto current = c.result(it.key(), identity(1)); QCOMPARE(current.state, it->state); QCOMPARE(current.outputs.keys(), it->outputs.keys());
        for (auto o = it->outputs.cbegin(); o != it->outputs.cend(); ++o) {
            if (auto x = dataAs<SeriesData>(o.value())) { auto y = dataAs<SeriesData>(current.outputs[o.key()]); QVERIFY(y); QCOMPARE(y->x, x->x); QCOMPARE(y->y, x->y); }
            else if (auto x = dataAs<TableData>(o.value())) QCOMPARE(dataAs<TableData>(current.outputs[o.key()])->rows, x->rows);
        }
    }
}
void BatchTests::independentBatchesAreIsolated()
{
    Fixture f; auto plan = f.plan(); Capture a, b;
    auto first = std::async(std::launch::async, [&]{return BatchWorkflowExecutor().run(plan.snapshot, {2}, a.observer());});
    const auto second = BatchWorkflowExecutor().run(plan.snapshot, {2}, b.observer());
    const auto result = first.get(); QCOMPARE(result.state, RunState::Succeeded); QCOMPARE(second.state, RunState::Succeeded);
    QVERIFY(result.batchId != second.batchId);
    QCOMPARE(dataAs<SeriesData>(a.result(f.paired, identity(101)).outputs["series"])->y,
             dataAs<SeriesData>(b.result(f.paired, identity(101)).outputs["series"])->y);
}
void BatchTests::generatedSourceRunsOnce()
{
    WorkflowModel model(registry()); auto root = model.addRoot("sine_source", {0,0}).nodeId;
    auto up = model.addChild(root, ChildSlot::Down, "two_outputs").nodeId;
    auto pair = model.addChild(up, ChildSlot::Down, "pair_probe").nodeId;
    QVector<BatchDataset> ds{{identity(1), {"generated", {}, {}}}};
    BatchGrouping groups{{pair, {{identity(101), "ports", {identity(1), "series"}, {identity(1), "trend"}}}}};
    auto p = prepareBatch(model.freeze().snapshot, root, ds, groups); QVERIFY(p.validation.ok()); QCOMPARE(p.snapshot->jobs().size(), 3);
    QCOMPARE(BatchWorkflowExecutor().run(p.snapshot).state, RunState::Succeeded);
    ds.push_back({identity(2), {"extra", {}, {}}}); QVERIFY(!prepareBatch(model.freeze().snapshot, root, ds, groups).snapshot);
}
void BatchTests::invalidExecutionOptions()
{
    Fixture f; auto p = f.plan(); BatchWorkflowExecutor e;
    QCOMPARE(e.run({}).state, RunState::Invalid);
    QCOMPARE(e.run(p.snapshot, {0}).state, RunState::Invalid);
    QCOMPARE(e.run(p.snapshot, {257}).state, RunState::Invalid);
    QCOMPARE(e.run(p.snapshot, {1, 0}).state, RunState::Invalid);
}
QTEST_GUILESS_MAIN(BatchTests)
#include "batch_tests.moc"
