// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "transfer_audit.h"
#include "application/batch_runner.h"
#include "processing/scientific_registry.h"
#include "io/database/data_codec.h"
#include "io/database/data_format.h"
#include <QtTest>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <limits>

using namespace aster;
namespace {
DataRef testSeries()
{
    SeriesData data;
    for (int i = 0; i < 64; ++i) {
        data.x.push_back(i);
        data.y.push_back(std::sin(i * .4) + i * .01);
    }
    return makeData(std::move(data));
}
class ProbeTask final : public Task {
public:
    using Function = std::function<NodeResult(const TaskInput&)>;
    explicit ProbeTask(Function function) : m_function(std::move(function)) {}
    NodeResult execute(const TaskInput& input, const ExecutionContext&) override { return m_function(input); }
private:
    Function m_function;
};
TaskDescriptor probeDescriptor(const QString& key, DataKind input, DataKind output)
{
    TaskDescriptor descriptor;
    descriptor.key = key; descriptor.label = key; descriptor.category = {"Test"};
    descriptor.inputKind = input; descriptor.outputs = {{"data", "Data", output, true}};
    descriptor.defaultOutput = "data";
    return descriptor;
}
}
class TransferAudit : public QObject {
    Q_OBJECT
private slots:
    void twoFileRootsKeepTheirBindings_data();
    void twoFileRootsKeepTheirBindings();
    void scientificPortsRejectWrongKinds();
    void optionalSpectrumPortIsNotSubstituted();
    void malformedOutputsStopAtProducer_data();
    void malformedOutputsStopAtProducer();
    void scientificConcurrencyMatchesSerial();
    void corruptTableCountsAndWidthsAreRejected();
    void validShapeInvalidDomainIsIsolated();
    void cancelWholeForestBeforeStarting();
};
void TransferAudit::validShapeInvalidDomainIsIsolated()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series", {0,0}).nodeId;
    const auto lowess = model.addChild(root, ChildSlot::Left, "detrend_lowess").nodeId;
    const auto blocked = model.addChild(lowess, ChildSlot::Down, "mtm").nodeId;
    const auto good = model.addChild(root, ChildSlot::Right, "affine").nodeId;
    auto source = *dataAs<SeriesData>(testSeries());
    source.x[10] = source.x[9]; // Legal Series shape; illegal LOWESS domain.
    const auto input = makeData(source);
    QVERIFY(validateData(input).isEmpty());
    const auto run = WorkflowExecutor().run({model.freeze().snapshot, root, {"duplicate-x",input,{}}});
    QCOMPARE(run.state, RunState::PartialFailed);
    QCOMPARE(run.nodes[lowess].state, NodeState::Failed);
    QVERIFY(!run.nodes[lowess].diagnostic().isEmpty());
    QVERIFY(run.nodes[lowess].outputs.isEmpty());
    QCOMPARE(run.nodes[blocked].state, NodeState::Blocked);
    QCOMPARE(run.nodes[good].state, NodeState::Succeeded);
    ResultRepository repo; QVERIFY(repo.open(":memory:").isEmpty());
    const auto saved = repo.save(run); QVERIFY(saved.saved());
    const auto loaded = repo.load(saved.recordId); QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.nodes[lowess].diagnostic().toJson(), run.nodes[lowess].diagnostic().toJson());
    QCOMPARE(dataAs<SeriesData>(loaded.run.nodes[root].outputs["series"])->x, source.x);
}
void TransferAudit::cancelWholeForestBeforeStarting()
{
    WorkflowModel model(makeScientificRegistry());
    SourceSelection selection;
    for (int i=0; i<2; ++i) {
        const auto root = model.addRoot("input_series", {i*5,0}).nodeId;
        QVERIFY(model.addChild(root,ChildSlot::Down,"detrend_lowess").ok());
        for (int j=0; j<3; ++j) selection[root].push_back({QString::number(j),testSeries(),{}});
    }
    const auto plan = prepareRuns(model.freeze().snapshot, selection);
    CancellationToken token; token.cancel();
    QTemporaryDir dir;
    const auto path = dir.filePath("cancelled.sqlite");
    const auto batch = BatchRunner().run(plan, path, 2, {}, {}, token);
    QVERIFY(batch.error.isEmpty()); QCOMPARE(batch.items.size(),6);
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    for (const auto& item : batch.items) {
        QCOMPARE(item.computation, RunState::Cancelled); QVERIFY(item.persistence.saved());
        const auto loaded = repo.load(item.persistence.recordId); QVERIFY(loaded.ok());
        QVERIFY(loaded.run.executionOrder.isEmpty()); QCOMPARE(loaded.run.nodes.size(),2);
        for (const auto& result : loaded.run.nodes) {
            QCOMPARE(result.state,NodeState::Cancelled); QVERIFY(result.outputs.isEmpty());
        }
    }
}
void TransferAudit::twoFileRootsKeepTheirBindings_data()
{
    QTest::addColumn<bool>("invalidFirstRoot");
    QTest::newRow("same-files-different-columns") << false;
    QTest::newRow("failure-does-not-affect-other-root") << true;
}
void TransferAudit::twoFileRootsKeepTheirBindings()
{
    QFETCH(bool, invalidFirstRoot);
    QTemporaryDir dir;
    QStringList paths;
    for (int i = 0; i < 2; ++i) {
        auto path = dir.filePath(QString::number(i) + ".csv");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QString("x,a,b\n0,%1,%2\n1,%3,%4\n2,%5,%6\n")
            .arg(i+1).arg(i+101).arg(i+2).arg(i+102).arg(i+3).arg(i+103).toUtf8();
        QCOMPARE(file.write(bytes), bytes.size()); file.close(); paths << path;
    }
    WorkflowModel model(makeScientificRegistry());
    const auto a = model.addRoot("file_source", {0,0}).nodeId;
    const auto b = model.addRoot("file_source", {5,0}).nodeId;
    const auto ac = model.addChild(a, ChildSlot::Down, "affine", {{"factor",2}}).nodeId;
    const auto bc = model.addChild(b, ChildSlot::Down, "affine", {{"factor",3}}).nodeId;
    SourceSelection sources;
    for (const auto& path : paths) {
        sources[a].push_back({path, {}, {{"path",path}, {"file_options",QJsonObject{
            {"auto_header",false},{"header",true},{"x_column",0},{"y_column",invalidFirstRoot ? 10 : 1}}}}});
        sources[b].push_back({path, {}, {{"path",path}, {"file_options",QJsonObject{
            {"auto_header",false},{"header",true},{"x_column",0},{"y_column",2}}}}});
    }
    const auto plan = prepareRuns(model.freeze().snapshot, sources);
    QVERIFY2(plan.validation.ok(),qPrintable(plan.validation.message()));
    QCOMPARE(plan.requests.size(),4);
    // Later edits cannot change a prepared request's input options or parameters.
    sources[a][0].provenance["path"] = "not-the-selected-file";
    QVERIFY(model.setParameters(bc,{{"factor",99}}).ok());
    QVector<RunEvent> events;
    const auto db = dir.filePath("runs.sqlite");
    const auto batch = BatchRunner().run(plan,db,3,[&](const RunEvent& event){events.push_back(event);});
    QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error));
    QCOMPARE(batch.items.size(),4);
    ResultRepository repo; QVERIFY(repo.open(db).isEmpty());
    QSet<RunId> ids; QSet<qint64> records;
    for (const auto& item : batch.items) {
        QVERIFY2(item.persistence.saved(),qPrintable(item.persistence.error));
        ids.insert(item.runId); records.insert(item.persistence.recordId);
        const auto loaded = repo.load(item.persistence.recordId); QVERIFY(loaded.ok());
        const auto& run = loaded.run;
        QCOMPARE(run.root,item.root); QCOMPARE(run.sourceLabel,item.sourceLabel);
        QCOMPARE(run.nodes.size(),2); QVERIFY(run.nodes.contains(item.root));
        const bool first = item.root == a;
        QVERIFY(run.nodes.contains(first ? ac : bc)); QVERIFY(!run.nodes.contains(first ? b : a));
        if (first && invalidFirstRoot) {
            QCOMPARE(run.nodes[a].state,NodeState::Failed);
            QCOMPARE(run.nodes[ac].state,NodeState::Blocked);
            QVERIFY(!run.nodes[a].diagnostic().isEmpty());
        } else {
            QCOMPARE(run.state,RunState::Succeeded);
            const auto* data = dataAs<SeriesData>(run.nodes[first ? ac : bc].outputs["series"]);
            QVERIFY(data);
            const int index = paths.indexOf(item.sourceLabel); QVERIFY(index >= 0);
            QCOMPARE(data->y[0],first ? 2.*(index+1) : 3.*(index+101));
        }
        bool sawTerminal = false;
        for (const auto& event : events) if (event.runId == item.runId) {
            QCOMPARE(event.root,item.root); QCOMPARE(event.sourceLabel,item.sourceLabel);
            QVERIFY(run.nodes.contains(event.nodeId));
            if (event.state == run.nodes[event.nodeId].state) sawTerminal = true;
        }
        QVERIFY(sawTerminal);
    }
    QCOMPARE(ids.size(),4); QCOMPARE(records.size(),4);
}
void TransferAudit::scientificPortsRejectWrongKinds()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series",{0,0}).nodeId;
    const auto mtm = model.addChild(root,ChildSlot::Left,"mtm").nodeId;
    const auto cwt = model.addChild(root,ChildSlot::Right,"cwt").nodeId;
    QVERIFY(!mtm.isNull() && !cwt.isNull());
    QVERIFY(!model.addChild(mtm,ChildSlot::Down,"detrend_lowess",{},"spectrum").ok());
    QVERIFY(!model.addChild(cwt,ChildSlot::Down,"detrend_lowess",{},"power").ok());
    QCOMPARE(model.definition().nodes.size(),3);
    const auto consumer = model.addChild(cwt,ChildSlot::Down,"detrend_lowess",{},"coi");
    QVERIFY2(consumer.ok(),qPrintable(consumer.error));
    // Same-shaped data need not have useful scientific meaning. No algorithm blacklist.
    const auto run = WorkflowExecutor().run({model.freeze().snapshot,root,{"signal",testSeries(),{}}});
    QCOMPARE(run.nodes[consumer.nodeId].state,NodeState::Succeeded);
    QVERIFY(dataAs<GridData>(run.nodes[cwt].outputs["power"]));
    QVERIFY(dataAs<SeriesData>(run.nodes[cwt].outputs["coi"]));
}
void TransferAudit::optionalSpectrumPortIsNotSubstituted()
{
    auto registry = makeScientificRegistry();
    QVERIFY(registry.add(probeDescriptor("spectrum_copy",DataKind::Spectrum,DataKind::Spectrum),
        [](const QJsonObject&){return std::make_unique<ProbeTask>([](const TaskInput& in){
            return NodeResult::success({{"data",in.data}});
        });}).ok());
    WorkflowModel model(registry);
    const auto root = model.addRoot("input_series",{0,0}).nodeId;
    const auto mtm = model.addChild(root,ChildSlot::Down,"mtm",{{"ar1",false},{"swa",false}}).nodeId;
    const auto missing = model.addChild(mtm,ChildSlot::Left,"spectrum_copy",{},"ar1_95").nodeId;
    const auto blocked = model.addChild(missing,ChildSlot::Down,"spectrum_copy").nodeId;
    const auto good = model.addChild(mtm,ChildSlot::Right,"spectrum_copy",{},"spectrum").nodeId;
    QVERIFY(!missing.isNull() && !blocked.isNull() && !good.isNull());
    QVector<RunEvent> events;
    const auto run = WorkflowExecutor().run({model.freeze().snapshot,root,{"optional",testSeries(),{}}},
        [&](const RunEvent& e){events.push_back(e);});
    QCOMPARE(run.nodes[mtm].state,NodeState::Succeeded);
    QCOMPARE(run.nodes[missing].diagnostic().code,errorCodeName(ErrorCode::InputUnavailable));
    QCOMPARE(run.nodes[missing].diagnostic().context["input_port"].toString(),QString("ar1_95"));
    QCOMPARE(run.nodes[blocked].state,NodeState::Blocked);
    QCOMPARE(run.nodes[good].state,NodeState::Succeeded);
    QVERIFY(!run.executionOrder.contains(missing));
    ResultRepository repo; QVERIFY(repo.open(":memory:").isEmpty());
    const auto saved = repo.save(run); QVERIFY2(saved.saved(),qPrintable(saved.error));
    const auto loaded = repo.load(saved.recordId); QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.nodes[missing].diagnostic().toJson(),run.nodes[missing].diagnostic().toJson());
    QCOMPARE(loaded.run.nodes[blocked].diagnostic().context["cause_node_id"].toString(),missing.toString(QUuid::WithoutBraces));
    for (const auto& event : events) if (event.nodeId == missing && event.state == NodeState::Failed)
        QCOMPARE(event.diagnostic.toJson(),loaded.run.nodes[missing].diagnostic().toJson());
}
void TransferAudit::malformedOutputsStopAtProducer_data()
{
    QTest::addColumn<int>("mode");
    const char* names[] = {"series-shape","series-nan","spectrum-negative-frequency","spectrum-negative-power",
        "grid-shape","grid-overflow","grid-infinity","table-duplicate-columns","table-row-width","table-nan"};
    for (int i=0;i<10;++i) QTest::newRow(names[i]) << i;
}
void TransferAudit::malformedOutputsStopAtProducer()
{
    QFETCH(int,mode);
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const QVector<DataRef> malformed{
        makeData(SeriesData{{0,1},{1},{},{}}),makeData(SeriesData{{0},{nan},{},{}}),
        makeData(SpectrumData{{-1},{1},{},{}}),makeData(SpectrumData{{1},{-1},{},{}}),
        makeData(GridData{{0,1},{0},{1},1,2,{},{},{}}),
        makeData(GridData{{0},{0},{1},std::numeric_limits<qsizetype>::max(),2,{},{},{}}),
        makeData(GridData{{0},{0},{std::numeric_limits<double>::infinity()},1,1,{},{},{}}),
        makeData(TableData{{"a","a"},{{1,2}}}),makeData(TableData{{"a","b"},{{1}}}),
        makeData(TableData{{"a"},{{nan}}})};
    const auto bad = malformed[mode]; const auto kind = dataKind(*bad);
    auto registry = makeScientificRegistry();
    QVERIFY(registry.add(probeDescriptor("bad_output",DataKind::Series,kind),[bad](const QJsonObject&){
        return std::make_unique<ProbeTask>([bad](const TaskInput&){return NodeResult::success({{"data",bad}});});
    }).ok());
    int calls=0;
    QVERIFY(registry.add(probeDescriptor("consumer",kind,kind),[&calls](const QJsonObject&){
        return std::make_unique<ProbeTask>([&calls](const TaskInput& in){++calls;return NodeResult::success({{"data",in.data}});});
    }).ok());
    WorkflowModel model(registry);
    const auto root = model.addRoot("input_series",{0,0}).nodeId;
    const auto producer = model.addChild(root,ChildSlot::Left,"bad_output").nodeId;
    const auto consumer = model.addChild(producer,ChildSlot::Down,"consumer").nodeId;
    const auto good = model.addChild(root,ChildSlot::Right,"affine").nodeId;
    QVERIFY(!producer.isNull() && !consumer.isNull());
    const auto run = WorkflowExecutor().run({model.freeze().snapshot,root,{"malformed",testSeries(),{}}});
    QCOMPARE(run.nodes[producer].diagnostic().code,errorCodeName(ErrorCode::OutputInvalid));
    QVERIFY(run.nodes[producer].outputs.isEmpty());
    QCOMPARE(run.nodes[consumer].state,NodeState::Blocked); QCOMPARE(calls,0);
    QCOMPARE(run.nodes[good].state,NodeState::Succeeded);
    ResultRepository repo; QVERIFY(repo.open(":memory:").isEmpty());
    const auto saved = repo.save(run); QVERIFY2(saved.saved(),qPrintable(saved.error));
    const auto loaded = repo.load(saved.recordId); QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.nodes[producer].diagnostic().toJson(),run.nodes[producer].diagnostic().toJson());
}
void TransferAudit::scientificConcurrencyMatchesSerial()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root = model.addRoot("input_series",{0,0}).nodeId;
    const auto trend = model.addChild(root,ChildSlot::Down,"detrend_lowess").nodeId;
    QVERIFY(model.addChild(trend,ChildSlot::Left,"mtm",{{"ar1",true}}).ok());
    QVERIFY(model.addChild(trend,ChildSlot::Right,"cwt").ok());
    const auto input = testSeries(); const auto inputBytes = encodeData(input).bytes;
    const auto baseline = WorkflowExecutor().run({model.freeze().snapshot,root,{"serial",input,{}}});
    QCOMPARE(baseline.state,RunState::Succeeded);
    const auto plan = prepareRuns(model.freeze().snapshot,{{root,{{"a",input,{}},{"b",input,{}},{"c",input,{}},{"d",input,{}}}}});
    QTemporaryDir dir; const auto path = dir.filePath("concurrent.sqlite");
    const auto batch = BatchRunner().run(plan,path,3);
    QVERIFY2(batch.error.isEmpty(),qPrintable(batch.error)); QCOMPARE(batch.items.size(),4);
    ResultRepository repo; QVERIFY(repo.open(path).isEmpty());
    for (const auto& item : batch.items) {
        QVERIFY(item.persistence.saved()); QCOMPARE(item.computation,RunState::Succeeded);
        const auto loaded = repo.load(item.persistence.recordId); QVERIFY(loaded.ok());
        for (auto node=baseline.nodes.cbegin();node!=baseline.nodes.cend();++node) {
            const auto outputs = loaded.run.nodes[node.key()].outputs;
            QCOMPARE(outputs.keys(),node->outputs.keys());
            for (auto port=node->outputs.cbegin();port!=node->outputs.cend();++port)
                QCOMPARE(encodeData(outputs[port.key()]).bytes,encodeData(port.value()).bytes);
        }
    }
    QCOMPARE(encodeData(input).bytes,inputBytes);
}
void TransferAudit::corruptTableCountsAndWidthsAreRejected()
{
    auto tableBytes = [](quint32 rows,quint32 width) {
        QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
        stream << DataFormat::magic << DataFormat::version << quint8(4) << quint32(1) << rows;
        stream << quint32(1); stream.writeRawData("a",1);
        stream << width; for (quint32 i=0;i<width;++i) stream << double(i);
        return bytes;
    };
    // A tiny damaged blob must not cause allocation of 20 million row objects.
    QVERIFY(!decodeData(tableBytes(DataFormat::elementLimit,0)).ok());
    QVERIFY(!decodeData(tableBytes(1,2)).ok());
    QVERIFY(!decodeData(tableBytes(1,0)).ok());
    QVERIFY(decodeData(tableBytes(1,1)).ok());
    const auto valid = encodeData(makeData(TableData{{"a"},{{1},{2}}})); QVERIFY(valid.ok());
    for (qsizetype size=0;size<valid.bytes.size();++size) QVERIFY(!decodeData(valid.bytes.left(size)).ok());
    QVERIFY(decodeData(encodeData(makeData(TableData{{"a"},{}})).bytes).ok());
}
int runTransferAudit(int argc,char** argv)
{
    TransferAudit tests; return QTest::qExec(&tests,argc,argv);
}
#include "transfer_audit.moc"
