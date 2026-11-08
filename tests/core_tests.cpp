// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <future>
#include <limits>
#include <stdexcept>
#include "core/workflow_executor.h"
#include "core/workflow_editor.h"
#include "model/workflow_codec.h"
#include "processing/builtin_tasks.h"

using namespace aster;
namespace {
DataRef series(double multiplier = 1)
{
    return makeData(SeriesData{{0, 1, 2}, {multiplier, 3 * multiplier, 5 * multiplier}, {"time", "ka"}, {"value", "u"}});
}
struct Fixture {
    WorkflowModel model{makeBuiltinRegistry()};
    NodeId root;
    Fixture() { root = model.addRoot(MethodKeys::input_series, {0, 0}).nodeId; }
    RunRequest request(DataRef input = series()) const
    { return {model.freeze().snapshot, root, {"fixture", std::move(input), {}}}; }
};
class FunctionTask final : public Task {
public:
    using Function = std::function<NodeResult(const TaskInput&, const ExecutionContext&)>;
    explicit FunctionTask(Function function) : m_function(std::move(function)) {}
    NodeResult execute(const TaskInput& input, const ExecutionContext& context) override { return m_function(input, context); }
private:
    Function m_function;
};
TaskRegistry withProbe(FunctionTask::Function function)
{
    auto registry = makeBuiltinRegistry();
    auto descriptor = AffineTask::descriptor();
    descriptor.key = "probe";
    descriptor.parameters.clear();
    auto status = registry.add(descriptor, [function](const QJsonObject&) { return std::make_unique<FunctionTask>(function); });
    if (!status.ok()) throw std::logic_error(status.message().toStdString());
    return registry;
}
}

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void compoundEditIsAtomic();
    void preparedEditsAreRevisionBound();
    void compoundHistoryRestoresAllState();
    void draftReferencesNeverRebind();
    void compoundBindingValidation();
    void compoundTopologyAndLimits();
    void compoundNoopPreservesRedo();
    void compoundLockRejectsAllEdits();
    void compoundHistoryLimit();
    void largeCompoundEdit();
    void registryCatalogueAndDefaults();
    void rejectBadParameters();
    void rejectBadRegistrations();
    void emptyDraftCannotRun();
    void rootAndChildKinds();
    void adjacencyDoesNotCreateEdges();
    void occupiedParentCellIsUnavailable();
    void threeSlotsAndInvalidDirection();
    void namedPortsAndTypeMismatch();
    void failedEditIsAtomic();
    void previewHasNoSideEffects();
    void deleteUndoRedoPreservesSiblings();
    void newEditClearsRedo();
    void snapshotsAreFrozen();
    void rejectCycles();
    void rejectMalformedDefinitions();
    void rejectVersionMismatch();
    void jsonRoundTrip();
    void rejectMalformedJson();
    void deepSubtreeDeletionIsIterative();
    void typedDataValidation();
    void detrendProducesKnownResults();
    void siblingsHaveIndependentParametersAndInput();
    void namedTrendOutputIsConsumed();
    void batchExpandsInputsButGeneratorRunsOnce();
    void invalidBatchSelection();
    void runContainsOnlySelectedRoot();
    void failureBlocksOnlyDescendants();
    void invalidInputFailsCleanly();
    void cancelBeforeStart();
    void cancelAfterRootPreservesCompletedOutput();
    void eventIdentityAndOrdering();
    void throwingObserverCannotCorruptRun();
    void taskExceptionIsContained();
    void factoryExceptionIsContained();
    void nonStandardExceptionAndAllocationFailureAreContained_data();
    void nonStandardExceptionAndAllocationFailureAreContained();
    void nullFactoryIsContained();
    void malformedTaskOutputIsRejected_data();
    void malformedTaskOutputIsRejected();
    void failedTaskCannotPublishOutput();
    void optionalMissingPortFailsItsConsumer();
    void concurrentRunsAreIsolated();
    void invalidRunRequest();
};

void CoreTests::registryCatalogueAndDefaults()
{
    const auto registry = makeBuiltinRegistry();
    QCOMPARE(registry.descriptors().size(), 5);
    QVERIFY(registry.find(MethodKeys::affine));
    const auto p = registry.normalize("affine", {});
    QVERIFY(p.validation.ok());
    QCOMPARE(p.parameters.value("factor").toDouble(), 1.0);
    QCOMPARE(p.parameters.value("offset").toDouble(), 0.0);
    QCOMPARE(registry.find("linear_detrend")->descriptor.outputs.size(), 2);
    QVERIFY(registry.find("sine_source")->descriptor.category.size() > 1);
}
void CoreTests::rejectBadParameters()
{
    const auto r = makeBuiltinRegistry();
    QVERIFY(!r.normalize("affine", {{"factor", "2"}}).validation.ok());
    QVERIFY(!r.normalize("affine", {{"factor", true}}).validation.ok());
    QVERIFY(!r.normalize("affine", {{"typo", 2}}).validation.ok());
    QVERIFY(!r.normalize("sine_source", {{"count", 3.5}}).validation.ok());
    QVERIFY(!r.normalize("sine_source", {{"count", 1}}).validation.ok());
    QVERIFY(!r.normalize("missing", {}).validation.ok());
    QVERIFY(!r.normalize("affine", {{"factor", QJsonValue(QJsonValue::Null)}}).validation.ok());
}
void CoreTests::rejectBadRegistrations()
{
    auto r = makeBuiltinRegistry();
    auto d = AffineTask::descriptor(); d.key = "affine";
    auto factory = [](const QJsonObject& p) { return std::make_unique<AffineTask>(p); };
    QVERIFY(!r.add(d, factory).ok());
    d.key = "bad"; d.defaultOutput = "missing";
    QVERIFY(!r.add(d, factory).ok());
    d.defaultOutput = "series"; d.parameters[0].defaultValue = "bad";
    QVERIFY(!r.add(d, factory).ok());
    d.parameters.clear(); d.requiresSourceBinding = true;
    QVERIFY(!r.add(d, factory).ok());
    QCOMPARE(r.descriptors().size(), 5);
}
void CoreTests::emptyDraftCannotRun()
{
    WorkflowModel model(makeBuiltinRegistry());
    QVERIFY(validateWorkflow(model.definition(), model.registry()).ok());
    QVERIFY(!model.freeze().validation.ok());
    QVERIFY(!model.undo().ok());
}
void CoreTests::rootAndChildKinds()
{
    Fixture f;
    QVERIFY(!f.model.addRoot("affine", {5, 0}).ok());
    QVERIFY(!f.model.addChild(f.root, ChildSlot::Left, "sine_source").ok());
    QVERIFY(f.model.addRoot("sine_source", {5, 0}).ok());
}
void CoreTests::adjacencyDoesNotCreateEdges()
{
    Fixture f;
    const auto b = f.model.addChild(f.root, ChildSlot::Left, "affine");
    const auto c = f.model.addChild(f.root, ChildSlot::Down, "affine");
    const auto d = f.model.addChild(c.nodeId, ChildSlot::Left, "affine");
    QVERIFY(b.ok() && c.ok() && d.ok());
    QCOMPARE(f.model.definition().layout.value(d.nodeId).column, -1);
    QCOMPARE(f.model.definition().layout.value(d.nodeId).row, 1);
    const auto removedB = f.model.removalPreview(b.nodeId);
    QCOMPARE(removedB.size(), 1);
    QVERIFY(!removedB.contains(d.nodeId));
    QVERIFY(f.model.removalPreview(c.nodeId).contains(d.nodeId));
    QVERIFY(!f.model.addChild(b.nodeId, ChildSlot::Down, "affine").ok());
    QCOMPARE(f.model.definition().nodes.size(), 4);
}
void CoreTests::occupiedParentCellIsUnavailable()
{
    Fixture f;
    const auto b = f.model.addChild(f.root, ChildSlot::Left, "affine");
    QVERIFY(b.ok());
    QVERIFY(!f.model.addChild(b.nodeId, ChildSlot::Right, "affine").ok());
}
void CoreTests::threeSlotsAndInvalidDirection()
{
    Fixture f;
    QVERIFY(f.model.addChild(f.root, ChildSlot::Left, "affine").ok());
    QVERIFY(f.model.addChild(f.root, ChildSlot::Right, "affine").ok());
    QVERIFY(f.model.addChild(f.root, ChildSlot::Down, "affine").ok());
    QVERIFY(!f.model.addChild(f.root, ChildSlot::Down, "affine").ok());
    QVERIFY(!f.model.addChild(f.root, static_cast<ChildSlot>(9), "affine").ok());
    QCOMPARE(f.model.definition().nodes.size(), 4);
}
void CoreTests::namedPortsAndTypeMismatch()
{
    Fixture f;
    const auto summary = f.model.addChild(f.root, ChildSlot::Right, "series_summary");
    QVERIFY(summary.ok());
    QVERIFY(!f.model.addChild(summary.nodeId, ChildSlot::Right, "affine").ok());
    QVERIFY(!f.model.addChild(f.root, ChildSlot::Left, "affine", {}, "missing").ok());
    const auto detrend = f.model.addChild(f.root, ChildSlot::Down, "linear_detrend");
    QVERIFY(f.model.addChild(detrend.nodeId, ChildSlot::Left, "affine", {}, "trend").ok());
}
void CoreTests::failedEditIsAtomic()
{
    Fixture f;
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine");
    const auto before = workflowToJson(f.model.definition());
    QVERIFY(!f.model.setParameters(child.nodeId, {{"factor", "bad"}}).ok());
    QCOMPARE(workflowToJson(f.model.definition()), before);
    QVERIFY(f.model.undo().ok());
    QCOMPARE(f.model.definition().nodes.size(), 1); // Failed edit added no undo entry.
}
void CoreTests::previewHasNoSideEffects()
{
    Fixture f;
    const auto original = workflowToJson(f.model.definition());
    const auto preview = f.model.previewChild(f.root, ChildSlot::Left, "affine");
    QVERIFY(preview.ok()); QCOMPARE(preview.output, QString("series"));
    QCOMPARE(preview.position.column, -1);
    QCOMPARE(workflowToJson(f.model.definition()), original);
    QVERIFY(f.model.addChild(f.root, ChildSlot::Left, "affine").ok());
    QVERIFY(!f.model.previewChild(f.root, ChildSlot::Left, "affine").ok());
    QVERIFY(f.model.undo().ok());
    QCOMPARE(workflowToJson(f.model.definition()), original);
}
void CoreTests::deleteUndoRedoPreservesSiblings()
{
    Fixture f;
    const auto b = f.model.addChild(f.root, ChildSlot::Left, "affine");
    const auto c = f.model.addChild(f.root, ChildSlot::Down, "affine");
    const auto d = f.model.addChild(c.nodeId, ChildSlot::Left, "affine", {{"factor", 7.0}});
    QVERIFY(b.ok() && c.ok() && d.ok());
    const auto before = workflowToJson(f.model.definition());
    const auto removed = f.model.removeSubtree(c.nodeId);
    QCOMPARE(removed.affectedNodes, 2);
    QCOMPARE(f.model.definition().nodes.size(), 2);
    QVERIFY(f.model.definition().layout.contains(b.nodeId));
    QVERIFY(f.model.undo().ok());
    QCOMPARE(workflowToJson(f.model.definition()), before);
    QVERIFY(f.model.redo().ok());
    QCOMPARE(f.model.definition().nodes.size(), 2);
    QVERIFY(f.model.removeSubtree(f.root).ok());
    QVERIFY(f.model.definition().nodes.isEmpty());
}
void CoreTests::newEditClearsRedo()
{
    Fixture f;
    QVERIFY(f.model.addChild(f.root, ChildSlot::Left, "affine").ok());
    QVERIFY(f.model.undo().ok()); QVERIFY(f.model.canRedo());
    QVERIFY(f.model.addChild(f.root, ChildSlot::Right, "series_summary").ok());
    QVERIFY(!f.model.canRedo());
}
void CoreTests::snapshotsAreFrozen()
{
    Fixture f;
    const auto node = f.model.addChild(f.root, ChildSlot::Down, "affine", {{"factor", 2.0}});
    auto request = f.request();
    QVERIFY(f.model.setParameters(node.nodeId, {{"factor", 9.0}}).ok());
    const auto oldRun = WorkflowExecutor().run(request);
    const auto newRun = WorkflowExecutor().run(f.request());
    QCOMPARE(dataAs<SeriesData>(oldRun.nodes[node.nodeId].outputs["series"])->y[0], 2.0);
    QCOMPARE(dataAs<SeriesData>(newRun.nodes[node.nodeId].outputs["series"])->y[0], 9.0);
    QVERIFY(f.model.removeSubtree(f.root).ok());
    QCOMPARE(WorkflowExecutor().run(request).nodes.size(), 2);
}
void CoreTests::rejectCycles()
{
    WorkflowModel model(makeBuiltinRegistry());
    const auto a = QUuid::createUuid(), b = QUuid::createUuid();
    WorkflowDefinition d;
    d.nodes = {{a, "affine", 1, {}, InputBinding{b, "series", ChildSlot::Left}},
               {b, "affine", 1, {}, InputBinding{a, "series", ChildSlot::Right}}};
    d.layout = {{a, {0, 0}}, {b, {1, 0}}};
    const auto validation = validateWorkflow(d, model.registry());
    QVERIFY(!validation.ok());
    QVERIFY(validation.message().contains("Cycle"));
    QVERIFY(!model.replaceDefinition(d).ok());
    QVERIFY(model.definition().nodes.isEmpty());
}
void CoreTests::rejectMalformedDefinitions()
{
    Fixture f;
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine");
    auto d = f.model.definition();
    d.nodes[1].input->parent = QUuid::createUuid();
    QVERIFY(!f.model.replaceDefinition(d).ok());
    d = f.model.definition(); d.nodes[1].input->parent = child.nodeId;
    QVERIFY(!f.model.replaceDefinition(d).ok());
    d = f.model.definition(); d.nodes.push_back(d.nodes[0]);
    QVERIFY(!f.model.replaceDefinition(d).ok());
    d = f.model.definition(); d.layout[child.nodeId] = {77, 88};
    QVERIFY(!f.model.replaceDefinition(d).ok());
    d = f.model.definition(); d.nodes[0].method = "unknown";
    QVERIFY(!f.model.replaceDefinition(d).ok());
    d = f.model.definition(); d.layout.insert(QUuid::createUuid(), {7, 8});
    QVERIFY(!f.model.replaceDefinition(d).ok());
    d = f.model.definition(); d.layout[f.root] = {GridCoordinateLimit + 1, 0};
    QVERIFY(!f.model.replaceDefinition(d).ok());
    QCOMPARE(f.model.definition().nodes.size(), 2);
}
void CoreTests::rejectVersionMismatch()
{
    Fixture f;
    auto d = f.model.definition(); d.nodes[0].methodVersion = 2;
    QVERIFY(!f.model.replaceDefinition(d).ok());
}
void CoreTests::jsonRoundTrip()
{
    Fixture f;
    QVERIFY(f.model.addChild(f.root, ChildSlot::Down, "affine", {{"factor", 2.5}}).ok());
    const auto bytes = QJsonDocument(workflowToJson(f.model.definition())).toJson();
    auto read = workflowFromJson(QJsonDocument::fromJson(bytes).object());
    QVERIFY2(read.validation.ok(), qPrintable(read.validation.message()));
    WorkflowModel restored(makeBuiltinRegistry());
    QVERIFY(restored.replaceDefinition(read.definition).ok());
    QCOMPARE(workflowToJson(restored.definition()), workflowToJson(f.model.definition()));
}
void CoreTests::rejectMalformedJson()
{
    Fixture f;
    const auto original = workflowToJson(f.model.definition());
    auto json = original; json["schema_version"] = 1.5;
    QVERIFY(!workflowFromJson(json).validation.ok());
    json = original; json["schema_version"] = 2;
    QVERIFY(!workflowFromJson(json).validation.ok());
    json = original; auto nodes = json["nodes"].toArray(); auto n = nodes[0].toObject();
    n["input"] = "not an edge"; nodes[0] = n; json["nodes"] = nodes;
    auto result = workflowFromJson(json);
    QVERIFY(!result.validation.ok()); QVERIFY(result.definition.nodes.isEmpty());
    json = original; auto layout = json["layout"].toArray(); auto p = layout[0].toObject();
    p["row"] = 0.5; layout[0] = p; json["layout"] = layout;
    QVERIFY(!workflowFromJson(json).validation.ok());
    json = original; layout = json["layout"].toArray(); layout.push_back(layout[0]); json["layout"] = layout;
    QVERIFY(!workflowFromJson(json).validation.ok());
}
void CoreTests::deepSubtreeDeletionIsIterative()
{
    Fixture f;
    auto d = f.model.definition();
    auto parent = f.root;
    for (int i = 1; i < 1500; ++i) {
        const auto id = QUuid::createUuid();
        d.nodes.push_back({id, "affine", 1, {}, InputBinding{parent, "series", ChildSlot::Down}});
        d.layout.insert(id, {0, i}); parent = id;
    }
    QVERIFY(f.model.replaceDefinition(d).ok());
    QCOMPARE(f.model.removalPreview(f.root).size(), 1500);
    QCOMPARE(f.model.removeSubtree(f.root).affectedNodes, 1500);
    QVERIFY(f.model.undo().ok());
    QCOMPARE(f.model.definition().nodes.size(), 1500);
}
void CoreTests::typedDataValidation()
{
    QVERIFY(validateData(series()).isEmpty());
    QVERIFY(!validateData({}).isEmpty());
    QVERIFY(!validateData(makeData(SeriesData{{1}, {1, 2}, {}, {}})).isEmpty());
    QVERIFY(!validateData(makeData(SeriesData{{1}, {std::numeric_limits<double>::infinity()}, {}, {}})).isEmpty());
    QVERIFY(!validateData(makeData(SpectrumData{{-1}, {1}, {}, {}})).isEmpty());
    QVERIFY(validateData(makeData(GridData{{0, 1}, {0, 1}, {1, 2, 3, 4}, 2, 2, {}, {}, {}})).isEmpty());
    QVERIFY(!validateData(makeData(GridData{{0, 1}, {0, 1}, {1}, 2, 2, {}, {}, {}})).isEmpty());
    QVERIFY(!validateData(makeData(TableData{{"a", "a"}, {{1, 2}}})).isEmpty());
}
void CoreTests::detrendProducesKnownResults()
{
    Fixture f;
    const auto n = f.model.addChild(f.root, ChildSlot::Down, "linear_detrend");
    const auto run = WorkflowExecutor().run(f.request());
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(run.nodes[n.nodeId].report["slope"].toDouble(), 2.0);
    QCOMPARE(run.nodes[n.nodeId].report["intercept"].toDouble(), 1.0);
    QCOMPARE(dataAs<SeriesData>(run.nodes[n.nodeId].outputs["trend"])->y, QVector<double>({1, 3, 5}));
    for (double y : dataAs<SeriesData>(run.nodes[n.nodeId].outputs["detrended"])->y) QVERIFY(std::abs(y) < 1e-12);
}
void CoreTests::siblingsHaveIndependentParametersAndInput()
{
    Fixture f;
    const auto a = f.model.addChild(f.root, ChildSlot::Left, "affine", {{"factor", 2}});
    const auto b = f.model.addChild(f.root, ChildSlot::Right, "affine", {{"factor", 5}, {"offset", 1}});
    auto data = series();
    const auto run = WorkflowExecutor().run(f.request(data));
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(run.nodes[a.nodeId].outputs["series"])->y, QVector<double>({2, 6, 10}));
    QCOMPARE(dataAs<SeriesData>(run.nodes[b.nodeId].outputs["series"])->y, QVector<double>({6, 16, 26}));
    QCOMPARE(dataAs<SeriesData>(data)->y, QVector<double>({1, 3, 5}));
    QCOMPARE(run.nodes[f.root].outputs["series"].get(), data.get());
}
void CoreTests::namedTrendOutputIsConsumed()
{
    Fixture f;
    const auto d = f.model.addChild(f.root, ChildSlot::Down, "linear_detrend");
    const auto n = f.model.addChild(d.nodeId, ChildSlot::Left, "affine", {{"factor", 3}}, "trend");
    const auto run = WorkflowExecutor().run(f.request());
    QCOMPARE(dataAs<SeriesData>(run.nodes[n.nodeId].outputs["series"])->y, QVector<double>({3, 9, 15}));
}
void CoreTests::batchExpandsInputsButGeneratorRunsOnce()
{
    Fixture f;
    const auto g = f.model.addRoot("sine_source", {8, 0});
    QVERIFY(f.model.addChild(g.nodeId, ChildSlot::Down, "series_summary").ok());
    SourceSelection sources{{f.root, {{"a", series(), {}}, {"b", series(2), {}}}}};
    const auto plan = prepareRuns(f.model.freeze().snapshot, sources);
    QVERIFY(plan.validation.ok()); QCOMPARE(plan.requests.size(), 3);
    int generated = 0;
    QSet<RunId> ids;
    for (const auto& request : plan.requests) {
        generated += request.root == g.nodeId;
        const auto run = WorkflowExecutor().run(request);
        QCOMPARE(run.state, RunState::Succeeded); ids.insert(run.runId);
    }
    QCOMPARE(generated, 1); QCOMPARE(ids.size(), 3);
}
void CoreTests::invalidBatchSelection()
{
    Fixture f;
    QVERIFY(!prepareRuns(f.model.freeze().snapshot, {}).validation.ok());
    const auto g = f.model.addRoot("sine_source", {8, 0});
    SourceSelection sources{{f.root, {{"a", series(), {}}}}, {g.nodeId, {{"bad", series(), {}}}}};
    const auto plan = prepareRuns(f.model.freeze().snapshot, sources);
    QVERIFY(!plan.validation.ok()); QVERIFY(plan.requests.isEmpty());
    QVERIFY(!prepareRuns({}, {}).validation.ok());
}
void CoreTests::runContainsOnlySelectedRoot()
{
    Fixture f;
    const auto g = f.model.addRoot("sine_source", {8, 0});
    const auto run = WorkflowExecutor().run(f.request());
    QCOMPARE(run.nodes.size(), 1); QVERIFY(!run.nodes.contains(g.nodeId));
    QCOMPARE(run.workflowSnapshot["nodes"].toArray().size(), 1);
}
void CoreTests::failureBlocksOnlyDescendants()
{
    Fixture f;
    const auto bad = f.model.addChild(f.root, ChildSlot::Left, "linear_detrend");
    const auto blocked = f.model.addChild(bad.nodeId, ChildSlot::Down, "series_summary");
    const auto good = f.model.addChild(f.root, ChildSlot::Right, "affine");
    const auto run = WorkflowExecutor().run(f.request(makeData(SeriesData{{1, 1, 1}, {1, 2, 3}, {}, {}})));
    QCOMPARE(run.state, RunState::PartialFailed);
    QCOMPARE(run.nodes[bad.nodeId].state, NodeState::Failed);
    QCOMPARE(run.nodes[blocked.nodeId].state, NodeState::Blocked);
    QCOMPARE(run.nodes[good.nodeId].state, NodeState::Succeeded);
    QVERIFY(!run.executionOrder.contains(blocked.nodeId));
}
void CoreTests::invalidInputFailsCleanly()
{
    Fixture f;
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine");
    const auto run = WorkflowExecutor().run(f.request({}));
    QCOMPARE(run.state, RunState::Failed);
    QCOMPARE(run.nodes[f.root].state, NodeState::Failed);
    QCOMPARE(run.nodes[child.nodeId].state, NodeState::Blocked);
    QVERIFY(run.executionOrder.isEmpty());
    const auto wrongType = WorkflowExecutor().run(f.request(makeData(TableData{{"a"}, {{1}}})));
    QCOMPARE(wrongType.state, RunState::Failed);
    QCOMPARE(wrongType.nodes[f.root].diagnostic().code, errorCodeName(ErrorCode::InputType));
    QCOMPARE(wrongType.nodes[f.root].diagnostic().context["expected"].toString(), QString("Series"));
    QCOMPARE(wrongType.nodes[f.root].diagnostic().context["actual_type"].toString(), QString("Table"));
}
void CoreTests::cancelBeforeStart()
{
    Fixture f;
    QVERIFY(f.model.addChild(f.root, ChildSlot::Down, "affine").ok());
    CancellationToken token; token.cancel();
    const auto run = WorkflowExecutor().run(f.request(), {}, token);
    QCOMPARE(run.state, RunState::Cancelled); QVERIFY(run.executionOrder.isEmpty());
    for (const auto& result : run.nodes) QCOMPARE(result.state, NodeState::Cancelled);
}
void CoreTests::cancelAfterRootPreservesCompletedOutput()
{
    Fixture f;
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine");
    CancellationToken token;
    const auto run = WorkflowExecutor().run(f.request(), [&](const RunEvent& event) {
        if (event.nodeId == f.root && event.state == NodeState::Succeeded) token.cancel();
    }, token);
    QCOMPARE(run.state, RunState::Cancelled);
    QCOMPARE(run.nodes[f.root].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[child.nodeId].state, NodeState::Cancelled);
    QVERIFY(run.nodes[f.root].outputs.contains("series"));
}
void CoreTests::eventIdentityAndOrdering()
{
    Fixture f;
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine");
    QVector<RunEvent> events;
    const auto run = WorkflowExecutor().run(f.request(), [&](const RunEvent& event) { events.push_back(event); });
    QCOMPARE(events.size(), 6);
    for (const auto& event : events) { QCOMPARE(event.runId, run.runId); QVERIFY(!event.nodeId.isNull()); }
    QCOMPARE(events[2].nodeId, f.root); QCOMPARE(events[2].state, NodeState::Running);
    QCOMPARE(events[3].state, NodeState::Succeeded);
    QCOMPARE(events[4].nodeId, child.nodeId); QCOMPARE(events[4].state, NodeState::Running);
    QVERIFY(run.startedAt <= run.finishedAt);
}
void CoreTests::throwingObserverCannotCorruptRun()
{
    Fixture f;
    const auto run = WorkflowExecutor().run(f.request(), [](const RunEvent&) { throw std::runtime_error("observer"); });
    QCOMPARE(run.state, RunState::Succeeded);
    QCOMPARE(run.observerWarnings.size(), 3);
}
void CoreTests::taskExceptionIsContained()
{
    WorkflowModel m(withProbe([](const TaskInput&, const ExecutionContext&) -> NodeResult { throw std::runtime_error("algorithm"); }));
    const auto root = m.addRoot("input_series", {0, 0});
    const auto child = m.addChild(root.nodeId, ChildSlot::Down, "probe");
    const auto run = WorkflowExecutor().run({m.freeze().snapshot, root.nodeId, {"test", series(), {}}});
    QCOMPARE(run.nodes[child.nodeId].state, NodeState::Failed);
    QCOMPARE(run.nodes[child.nodeId].error, QString("algorithm"));
}
void CoreTests::factoryExceptionIsContained()
{
    auto registry = makeBuiltinRegistry();
    auto d = AffineTask::descriptor(); d.key = "throws";
    QVERIFY(registry.add(d, [](const QJsonObject&) -> std::unique_ptr<Task> { throw std::runtime_error("factory"); }).ok());
    WorkflowModel m(registry);
    const auto root = m.addRoot("input_series", {0, 0});
    const auto child = m.addChild(root.nodeId, ChildSlot::Down, "throws");
    const auto run = WorkflowExecutor().run({m.freeze().snapshot, root.nodeId, {"test", series(), {}}});
    QCOMPARE(run.nodes[child.nodeId].state, NodeState::Failed);
    QCOMPARE(run.nodes[child.nodeId].error, QString("factory"));
}
void CoreTests::malformedTaskOutputIsRejected_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("missing-required") << 0;
    QTest::newRow("wrong-kind") << 1;
    QTest::newRow("undeclared-port") << 2;
    QTest::newRow("bad-shape") << 3;
    QTest::newRow("nonterminal-state") << 4;
    QTest::newRow("succeeded-with-error") << 5;
    QTest::newRow("nonfinite-values") << 6;
    QTest::newRow("null-output") << 7;
    QTest::newRow("succeeded-with-diagnostic") << 8;
    QTest::newRow("executor-only-state") << 9;
}
void CoreTests::malformedTaskOutputIsRejected()
{
    QFETCH(int, mode);
    WorkflowModel m(withProbe([mode](const TaskInput& input, const ExecutionContext&) {
        auto result = NodeResult::success({{"series", input.data}});
        if (mode == 0) result.outputs.clear();
        if (mode == 1) result.outputs["series"] = makeData(TableData{{"a"}, {{1}}});
        if (mode == 2) result.outputs["extra"] = input.data;
        if (mode == 3) result.outputs["series"] = makeData(SeriesData{{1}, {1, 2}, {}, {}});
        if (mode == 4) result.state = NodeState::Running;
        if (mode == 5) result.error = "contradiction";
        if (mode == 6) result.outputs["series"] = makeData(SeriesData{{1}, {std::numeric_limits<double>::quiet_NaN()}, {}, {}});
        if (mode == 7) result.outputs["series"].reset();
        if (mode == 8) result.setDiagnostic(makeError(ErrorCode::NumericRange));
        if (mode == 9) result.state = NodeState::Blocked;
        return result;
    }));
    const auto root = m.addRoot("input_series", {0, 0});
    const auto child = m.addChild(root.nodeId, ChildSlot::Down, "probe");
    const auto run = WorkflowExecutor().run({m.freeze().snapshot, root.nodeId, {"test", series(), {}}});
    QCOMPARE(run.nodes[child.nodeId].state, NodeState::Failed);
    QVERIFY(run.nodes[child.nodeId].outputs.isEmpty());
}
void CoreTests::failedTaskCannotPublishOutput()
{
    WorkflowModel m(withProbe([](const TaskInput& input, const ExecutionContext&) {
        auto result = NodeResult::failure("failed"); result.outputs["series"] = input.data; return result;
    }));
    const auto root = m.addRoot("input_series", {0, 0});
    const auto child = m.addChild(root.nodeId, ChildSlot::Down, "probe");
    const auto run = WorkflowExecutor().run({m.freeze().snapshot, root.nodeId, {"test", series(), {}}});
    QVERIFY(run.nodes[child.nodeId].outputs.isEmpty());
}
void CoreTests::optionalMissingPortFailsItsConsumer()
{
    auto registry = makeBuiltinRegistry();
    auto d = AffineTask::descriptor(); d.key = "optional";
    d.outputs.push_back({"optional_series", "Optional", DataKind::Series, false});
    QVERIFY(registry.add(d, [](const QJsonObject& p) { return std::make_unique<AffineTask>(p); }).ok());
    WorkflowModel m(registry);
    const auto root = m.addRoot("input_series", {0, 0});
    const auto parent = m.addChild(root.nodeId, ChildSlot::Down, "optional");
    const auto child = m.addChild(parent.nodeId, ChildSlot::Down, "affine", {}, "optional_series");
    const auto run = WorkflowExecutor().run({m.freeze().snapshot, root.nodeId, {"test", series(), {}}});
    QCOMPARE(run.nodes[parent.nodeId].state, NodeState::Succeeded);
    QCOMPARE(run.nodes[child.nodeId].state, NodeState::Failed);
    QCOMPARE(run.nodes[child.nodeId].diagnostic().code, errorCodeName(ErrorCode::InputUnavailable));
    QCOMPARE(run.nodes[child.nodeId].diagnostic().context["input_port"].toString(), QString("optional_series"));
    QCOMPARE(run.nodes[child.nodeId].diagnostic().context["parent_id"].toString(), parent.nodeId.toString(QUuid::WithoutBraces));
    QVERIFY(!run.executionOrder.contains(child.nodeId));
}
void CoreTests::nonStandardExceptionAndAllocationFailureAreContained_data()
{
    QTest::addColumn<bool>("allocation");
    QTest::newRow("non-standard") << false;
    QTest::newRow("allocation") << true;
}
void CoreTests::nonStandardExceptionAndAllocationFailureAreContained()
{
    QFETCH(bool, allocation);
    WorkflowModel model(withProbe([allocation](const TaskInput&, const ExecutionContext&) -> NodeResult {
        if (allocation) throw std::bad_alloc(); // Fault injection, not actual memory exhaustion.
        throw 7;
    }));
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto bad = model.addChild(root, ChildSlot::Left, "probe").nodeId;
    const auto blocked = model.addChild(bad, ChildSlot::Down, "affine").nodeId;
    const auto good = model.addChild(root, ChildSlot::Right, "affine").nodeId;
    const auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"test", series(), {}}});
    QCOMPARE(run.state, RunState::PartialFailed);
    QCOMPARE(run.nodes[bad].diagnostic().code, errorCodeName(allocation ? ErrorCode::AllocationFailure : ErrorCode::TaskException));
    QCOMPARE(run.nodes[blocked].state, NodeState::Blocked);
    QCOMPARE(run.nodes[good].state, NodeState::Succeeded);
}
void CoreTests::nullFactoryIsContained()
{
    auto registry = makeBuiltinRegistry();
    auto descriptor = AffineTask::descriptor(); descriptor.key = "null_factory";
    QVERIFY(registry.add(descriptor, [](const QJsonObject&) { return std::unique_ptr<Task>{}; }).ok());
    WorkflowModel model(registry);
    const auto root = model.addRoot("input_series", {0, 0}).nodeId;
    const auto bad = model.addChild(root, ChildSlot::Down, "null_factory").nodeId;
    const auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"test", series(), {}}});
    QCOMPARE(run.nodes[bad].state, NodeState::Failed);
    QCOMPARE(run.nodes[bad].error, QString("Factory returned null"));
    QVERIFY(!run.executionOrder.contains(bad));
}
void CoreTests::concurrentRunsAreIsolated()
{
    Fixture f;
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine", {{"factor", 2.0}});
    const auto snapshot = f.model.freeze().snapshot;
    std::vector<std::future<RunResult>> futures;
    for (int i = 1; i <= 8; ++i) {
        const RunRequest request{snapshot, f.root, {QString::number(i), series(i), {}}};
        futures.push_back(std::async(std::launch::async, [request] { return WorkflowExecutor().run(request); }));
    }
    QSet<RunId> ids;
    for (int i = 0; i < 8; ++i) {
        const auto run = futures[i].get(); ids.insert(run.runId);
        QCOMPARE(run.state, RunState::Succeeded);
        QCOMPARE(dataAs<SeriesData>(run.nodes[child.nodeId].outputs["series"])->y[0], 2.0 * (i + 1));
    }
    QCOMPARE(ids.size(), 8);
}
void CoreTests::invalidRunRequest()
{
    QVERIFY(WorkflowExecutor().run({}).state == RunState::Invalid);
    Fixture f;
    auto request = f.request(); request.root = QUuid::createUuid();
    QCOMPARE(WorkflowExecutor().run(request).state, RunState::Invalid);
    const auto child = f.model.addChild(f.root, ChildSlot::Down, "affine");
    request = f.request(); request.root = child.nodeId;
    QCOMPARE(WorkflowExecutor().run(request).state, RunState::Invalid);
    const auto generated = f.model.addRoot("sine_source", {8, 0});
    request = f.request(); request.root = generated.nodeId;
    QCOMPARE(WorkflowExecutor().run(request).state, RunState::Invalid);
}


namespace {
TaskRegistry editRegistry()
{
    auto r = makeBuiltinRegistry();
    auto d = AffineTask::descriptor(); d.key = "pair_edit_probe"; d.pairedInput = true;
    const auto added = r.add(d, [](const QJsonObject& p) { return std::make_unique<AffineTask>(p); });
    if (!added.ok()) throw std::logic_error(added.message().toStdString());
    return r;
}
struct EditFixture {
    WorkflowEditor editor{editRegistry()};
    NodeId root, pair;
    QUuid a = QUuid::createUuid(), b = QUuid::createUuid(), group = QUuid::createUuid();
    EditFixture() {
        WorkflowModel graph(editRegistry(), false);
        root = graph.addRoot("input_series", {0, 0}).nodeId;
        pair = graph.addChild(root, ChildSlot::Down, "pair_edit_probe").nodeId;
        auto draft = editor.beginEdit(); draft.state.definition = graph.definition();
        draft.state.sources[root] = {{"A", series(), {{"dataset_id", a.toString()}}},
                                     {"B", series(2), {{"dataset_id", b.toString()}}}};
        draft.state.grouping[pair] = {{group, "A/B", {a, "series"}, {b, "series"}}};
        const auto applied = editor.apply(draft);
        if (!applied.ok()) throw std::logic_error(applied.error.toStdString());
    }
    BatchPlanResult plan() const {
        QVector<BatchDataset> data;
        for (const auto& s : editor.state().sources.value(root))
            data.push_back({QUuid(s.provenance["dataset_id"].toString()), s});
        return prepareBatch(editor.model().freeze().snapshot, root, data, editor.state().grouping);
    }
};
}
void CoreTests::compoundEditIsAtomic()
{
    EditFixture f;
    const auto before = workflowToJson(f.editor.state().definition);
    const auto revision = f.editor.revision();
    auto draft = f.editor.beginEdit();
    draft.state.sources.clear(); draft.state.grouping.clear();
    draft.state.definition.nodes[1].input->parent = f.pair; // invalid cycle, with other changes
    QVERIFY(!f.editor.apply(draft).ok());
    QCOMPARE(f.editor.revision(), revision);
    QCOMPARE(workflowToJson(f.editor.state().definition), before);
    QCOMPARE(f.editor.state().sources.value(f.root).size(), 2);
    QCOMPARE(f.editor.state().grouping.value(f.pair).size(), 1);
    QVERIFY(f.editor.undo().ok()); // failure added no checkpoint
    QVERIFY(f.editor.state().definition.nodes.isEmpty());
    QVERIFY(!f.editor.canUndo());
}
void CoreTests::preparedEditsAreRevisionBound()
{
    EditFixture f;
    auto draft = f.editor.beginEdit(); draft.state.grouping[f.pair][0].label = "Prepared";
    const auto prepared = f.editor.prepare(draft);
    QVERIFY(prepared.edit);
    draft.state.grouping[f.pair][0].label = "Changed after preparation";
    QCOMPARE(prepared.edit->state().grouping.value(f.pair)[0].label, QString("Prepared"));
    QVERIFY(f.editor.commit(prepared.edit).ok());
    QVERIFY(!f.editor.commit(prepared.edit).ok());
    QVERIFY(f.editor.undo().ok());
    QVERIFY(!f.editor.commit(prepared.edit).ok()); // undo never restores an old revision
    EditFixture other;
    QVERIFY(!other.editor.commit(f.editor.prepare(f.editor.beginEdit()).edit).ok());
    QVERIFY(!other.editor.prepare(f.editor.beginEdit()).validation.ok());
    QVERIFY(!f.editor.commit({}).ok());
}
void CoreTests::compoundHistoryRestoresAllState()
{
    EditFixture f;
    const auto frozen = f.plan(); QVERIFY(frozen.snapshot);
    const auto manifest = frozen.snapshot->manifest();
    const auto data = f.editor.state().sources.value(f.root)[0].data;
    auto edit = f.editor.beginEdit();
    edit.state.definition.nodes[1].parameters["factor"] = 3;
    edit.state.sources[f.root][0].provenance["note"] = "new import options";
    edit.state.grouping[f.pair][0].label = "Renamed";
    QVERIFY(f.editor.apply(edit).ok());
    QVERIFY(f.editor.undo().ok());
    QCOMPARE(f.editor.state().definition.nodes[1].parameters["factor"].toDouble(), 1.0);
    QVERIFY(!f.editor.state().sources.value(f.root)[0].provenance.contains("note"));
    QCOMPARE(f.editor.state().sources.value(f.root)[0].data.get(), data.get());
    QCOMPARE(f.editor.state().grouping.value(f.pair)[0].label, QString("A/B"));
    QVERIFY(f.editor.redo().ok());
    QCOMPARE(f.editor.state().definition.nodes[1].parameters["factor"].toDouble(), 3.0);
    QCOMPARE(f.editor.state().grouping.value(f.pair)[0].label, QString("Renamed"));
    QCOMPARE(frozen.snapshot->manifest(), manifest);
    QVERIFY(f.editor.undo().ok());
    auto remove = f.editor.beginEdit(); remove.state = {};
    QVERIFY(f.editor.apply(remove).ok());
    QVERIFY(f.editor.state().sources.isEmpty()); QVERIFY(f.editor.state().grouping.isEmpty());
    QVERIFY(f.editor.undo().ok()); QVERIFY(f.plan().snapshot);
    QCOMPARE(f.editor.state().grouping.value(f.pair)[0].id, f.group);
}
void CoreTests::draftReferencesNeverRebind()
{
    EditFixture f;
    auto remove = f.editor.beginEdit(); remove.state.sources[f.root].removeLast();
    QVERIFY(f.editor.apply(remove).ok()); QVERIFY(!f.plan().snapshot);
    QCOMPARE(f.editor.state().grouping.value(f.pair)[0].b.dataset, f.b);
    auto unrelated = f.editor.beginEdit(); unrelated.state.definition.nodes[1].parameters["factor"] = 2;
    QVERIFY(f.editor.apply(unrelated).ok()); // incomplete draft can still be edited
    auto invalid = f.editor.beginEdit(); invalid.state.grouping[f.pair][0].b.dataset = QUuid::createUuid();
    QVERIFY(!f.editor.apply(invalid).ok());
    QVERIFY(f.editor.undo().ok()); QVERIFY(f.editor.undo().ok()); QVERIFY(f.plan().snapshot);
    QCOMPARE(f.editor.state().sources.value(f.root)[1].provenance["dataset_id"].toString(), f.b.toString());
}
void CoreTests::compoundBindingValidation()
{
    EditFixture f;
    auto draft = f.editor.beginEdit();
    draft.state.sources[f.root][1].provenance["dataset_id"] = f.a.toString();
    QVERIFY(!f.editor.prepare(draft).edit);
    draft = f.editor.beginEdit(); draft.state.grouping[f.pair][0].b.port = "missing";
    QVERIFY(!f.editor.prepare(draft).edit);
    draft = f.editor.beginEdit(); draft.state.sources[QUuid::createUuid()] = {};
    QVERIFY(!f.editor.prepare(draft).edit);
    draft = f.editor.beginEdit(); draft.state.grouping[f.root] = {};
    QVERIFY(!f.editor.prepare(draft).edit);
    draft = f.editor.beginEdit(); draft.state.grouping[f.pair][0].id = f.a;
    QVERIFY(!f.editor.prepare(draft).edit);
    draft = f.editor.beginEdit(); draft.state.grouping[f.pair][0].b = draft.state.grouping[f.pair][0].a;
    QVERIFY(!f.editor.prepare(draft).edit);
    draft = f.editor.beginEdit(); draft.state.sources[f.root][0].data.reset();
    QVERIFY(!f.editor.prepare(draft).edit);
    // No graph method replacement may silently erase an incompatible pairing.
    draft = f.editor.beginEdit(); draft.state.definition.nodes[1].method = "affine";
    QVERIFY(!f.editor.prepare(draft).edit);
    // Empty pair settings are a valid draft, not a runnable batch.
    draft = f.editor.beginEdit(); draft.state.grouping.clear();
    QVERIFY(f.editor.apply(draft).ok()); QVERIFY(!f.plan().snapshot);
}

void CoreTests::compoundTopologyAndLimits()
{
    EditFixture f;
    WorkflowModel graph(editRegistry(), false);
    QVERIFY(graph.replaceDefinition(f.editor.state().definition).ok());
    const auto secondRoot = graph.addRoot("input_series", {4, 0}).nodeId;
    auto moved = f.editor.beginEdit(); moved.state.definition = graph.definition();
    moved.state.definition.nodes[1].input->parent = secondRoot;
    moved.state.definition.layout[f.pair] = {4, 1};
    QVERIFY(!f.editor.prepare(moved).edit); // explicit graph edge alone cannot rebind A/B
    QVERIFY(graph.addChild(f.pair, ChildSlot::Down, "pair_edit_probe").ok());
    auto nested = f.editor.beginEdit(); nested.state.definition = graph.definition();
    QVERIFY(!f.editor.prepare(nested).edit);
    auto many = f.editor.beginEdit();
    auto& sources = many.state.sources[f.root];
    const auto shared = series();
    while (sources.size() <= BatchDatasetLimit)
        sources.push_back({"extra", shared, {{"dataset_id", QUuid::createUuid().toString()}}});
    QVERIFY(!f.editor.prepare(many).edit);
    // The joint-batch limit must not introduce a new cap on ordinary input lists.
    many.state.definition.nodes.removeAt(1); many.state.definition.layout.remove(f.pair);
    many.state.grouping.clear(); QVERIFY(f.editor.prepare(many).edit);
}
void CoreTests::compoundNoopPreservesRedo()
{
    EditFixture f;
    auto draft = f.editor.beginEdit(); draft.state.grouping[f.pair][0].label = "New";
    QVERIFY(f.editor.apply(draft).ok()); QVERIFY(f.editor.undo().ok());
    const auto revision = f.editor.revision();
    const auto result = f.editor.apply(f.editor.beginEdit());
    QVERIFY(result.ok()); QVERIFY(!result.changed()); QCOMPARE(f.editor.revision(), revision);
    QVERIFY(f.editor.canRedo()); QVERIFY(f.editor.redo().ok());
}
void CoreTests::compoundLockRejectsAllEdits()
{
    EditFixture f;
    auto edit = f.editor.beginEdit(); edit.state.grouping[f.pair][0].label = "pending";
    const auto prepared = f.editor.prepare(edit); QVERIFY(prepared.edit);
    f.editor.setLocked(true);
    QVERIFY(!f.editor.prepare(f.editor.beginEdit()).edit);
    QVERIFY(!f.editor.commit(prepared.edit).ok()); QVERIFY(!f.editor.undo().ok()); QVERIFY(!f.editor.redo().ok());
    f.editor.setLocked(false);
    QVERIFY(!f.editor.commit(prepared.edit).ok()); // even after unlock
    QVERIFY(f.editor.apply(f.editor.beginEdit()).ok());
}
void CoreTests::compoundHistoryLimit()
{
    EditFixture f;
    for (int i = 0; i < 110; ++i) {
        auto edit = f.editor.beginEdit(); edit.state.grouping[f.pair][0].label = QString::number(i);
        QVERIFY(f.editor.apply(edit).ok());
    }
    int count = 0;
    while (f.editor.canUndo()) { QVERIFY(f.editor.undo().ok()); ++count; }
    QCOMPARE(count, int(WorkflowEditor::HistoryLimit));
    while (f.editor.canRedo()) QVERIFY(f.editor.redo().ok());
    QCOMPARE(f.editor.state().grouping.value(f.pair)[0].label, QString("109"));
}
void CoreTests::largeCompoundEdit()
{
    EditFixture f;
    auto edit = f.editor.beginEdit();
    auto& sources = edit.state.sources[f.root]; auto& pairs = edit.state.grouping[f.pair];
    sources.clear(); pairs.clear();
    const auto shared = series();
    QUuid first;
    for (int i = 0; i < BatchDatasetLimit; ++i) {
        const auto id = QUuid::createUuid();
        sources.push_back({QString::number(i), shared, {{"dataset_id", id.toString()}}});
        if (i == 0) first = id;
        else pairs.push_back({QUuid::createUuid(), QString::number(i), {first, "series"}, {id, "series"}});
    }
    QElapsedTimer timer; timer.start();
    QVERIFY(f.editor.apply(edit).ok());
    for (int i = 0; i < 10; ++i) {
        auto next = f.editor.beginEdit(); next.state.definition.nodes[1].parameters["factor"] = i + 2;
        QVERIFY(f.editor.apply(next).ok());
    }
    const auto milliseconds = timer.elapsed();
    qInfo() << "Compound editor: 10000 datasets, 9999 pairs, initial + 10 edits (ms):" << milliseconds;
    QVERIFY(milliseconds < 10000); // regression guard, not a user-machine promise
    QCOMPARE(f.editor.state().sources.value(f.root).back().data.get(), shared.get());
    QVERIFY(f.editor.undo().ok()); QVERIFY(f.editor.redo().ok());
}

QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
