// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QJsonArray>
#include <QPushButton>
#include <QSet>
#include <cmath>
#include <random>
#include "core/workflow_editor.h"
#include "processing/scientific_registry.h"
#include "processing/builtin_tasks.h"
#include "processing/cross_spectrum_task.h"
#include "io/database/data_codec.h"
#include "model/workflow_codec.h"
#include "ui/workflow/workflow_document.h"
#include "ui/services/workflow_preview_controller.h"
#include "ui/panels/pairing_panel.h"
#include "../tests/support/joint_input_helpers.h"
#include "ui/workflow/workflow_canvas.h"
#include "ui/common/popup_placement.h"
#include "ui/settings/theme_manager.h"
#include <QLineEdit>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QSplitter>
#include <QDir>
#include "ui/common/presentation.h"
#include "application/batch_runner.h"
#include "application/joint_batch_runner.h"
using namespace aster;
using namespace aster::ui;
namespace {
class PairProbe : public Task {
public:
    NodeResult execute(const TaskInput& input,const ExecutionContext&) override {
        const auto* a=dataAs<SeriesData>(input.data); const auto* b=dataAs<SeriesData>(input.second);
        if(!a || !b || a->x!=b->x) return NodeResult::failure("Mismatched input");
        auto result=*a; for(qsizetype i=0;i<result.y.size();++i) result.y[i]-=b->y[i];
        return NodeResult::success({{"series",makeData(result)}});
    }
};
TaskRegistry registry() {
    auto r=makeScientificRegistry(); auto d=AffineTask::descriptor(); d.key="linked_probe";
    d.parameters.clear(); d.pairedInput=true;
    const auto status=r.add(d,[](const QJsonObject&){return std::make_unique<PairProbe>();});
    if(!status.ok()) qFatal("probe registration failed");
    return r;
}
SourceItem data(double offset) {
    SeriesData s; s.xAxis={"age","ka"};s.yAxis={"proxy","u"};
    for(int i=0;i<512;++i){s.x.push_back(i);s.y.push_back(offset+std::sin(i*.2));}
    return {"same name",makeData(s),{}};
}
struct Fixture {
    WorkflowDocument doc{registry()}; NodeId rootA,rootB,a,b,sibling,joint,down; QUuid a0,a1,b0,b1,pair;
    Fixture() {
        rootA=doc.addRoot("input_series").nodeId;
        a=doc.addChild(rootA,ChildSlot::Down,"affine",{{"factor",2}}).nodeId;
        joint=doc.addChild(a,ChildSlot::Down,"linked_probe").nodeId;
        down=doc.addChild(joint,ChildSlot::Down,"affine",{{"factor",3}}).nodeId;
        // Referenced branch is created after the consumer.
        sibling=doc.addChild(rootA,ChildSlot::Right,"affine",{{"factor",5}}).nodeId;
        rootB=doc.addRoot("input_series").nodeId;
        b=doc.addChild(rootB,ChildSlot::Down,"affine",{{"factor",3}}).nodeId;
        if(!doc.setSources(rootA,{data(1),data(2)}).isEmpty() || !doc.setSources(rootB,{data(10),data(20)}).isEmpty()) qFatal("fixture sources failed");
        a0=doc.datasets(rootA)[0].id;a1=doc.datasets(rootA)[1].id;b0=doc.datasets(rootB)[0].id;b1=doc.datasets(rootB)[1].id;pair=QUuid::createUuid();
    }
    BatchPair linked(bool cross=true) const {return {pair,"A-B",{a0,"series"},{cross?b0:a0,"series",cross?b:sibling}};}
    BatchSources sources() const {return {{rootA,doc.datasets(rootA)},{rootB,doc.datasets(rootB)}};}
};
struct Capture {
    QMap<QPair<NodeId,QUuid>,NodeResult> outputs; QMap<QPair<NodeId,QUuid>,int> calls;
    BatchObserver observer(){return {{},[this](const BatchJob& job,const NodeResult& result){outputs[{job.node,job.item}]=result;++calls[{job.node,job.item}];},{}};}
    double first(NodeId node,QUuid item) const {
        const auto* s=dataAs<SeriesData>(outputs.value({node,item}).outputs.value("series"));return s?s->y.front():NAN;
    }
};
}
class LinkedInputTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){setLanguage("zh");applyTheme(*qApp);}
    void largeCrossRootIndexSurvivesGrowth() {
        Fixture f;
        auto draft = f.doc.beginEdit();
        QHash<NodeId,NodeId> expectedRoot{{f.rootA,f.rootA},{f.a,f.rootA},
            {f.sibling,f.rootA},{f.joint,f.rootA},{f.down,f.rootA},
            {f.rootB,f.rootB},{f.b,f.rootB}};
        NodeId lastA, lastB;
        for (int branch = 0; branch < 2; ++branch) {
            auto parent = branch ? f.b : f.sibling;
            auto position = draft.state.definition.layout.value(parent);
            for (int i = 0; i < 512; ++i) {
                const auto id = QUuid::createUuid();
                draft.state.definition.nodes.push_back({id,"affine",
                    f.doc.model().registry().find("affine")->descriptor.version,
                    {{"factor",1}},InputBinding{parent,"series",ChildSlot::Down}});
                ++position.row; draft.state.definition.layout[id] = position;
                expectedRoot[id] = branch ? f.rootB : f.rootA;
                parent = id;
            }
            (branch ? lastB : lastA) = parent;
        }
        const auto prepared = f.doc.prepareEdit(std::move(draft));
        QVERIFY2(prepared.validation.ok(),qPrintable(prepared.validation.message()));
        QVERIFY(f.doc.commitEdit(prepared.edit).ok());
        QVERIFY(f.doc.setPairs(f.joint,{{f.pair,"deep roots",
            {f.a0,"series",lastA},{f.b0,"series",lastB}}}).isEmpty());
        const auto origins = linkedInputOrigins(f.doc.model(),f.joint);
        QVERIFY(origins.contains(lastA)); QVERIFY(origins.contains(lastB));
        const auto plan = f.doc.plan(); QVERIFY(plan.validation.ok());
        QCOMPARE(plan.batches.size(),1);
        for (const auto& job : plan.batches.front()->jobs())
            QCOMPARE(job.sourceRoot,expectedRoot.value(job.node));
        Capture all;
        QCOMPARE(BatchWorkflowExecutor().run(plan.batches.front(),{4},all.observer()).state,RunState::Succeeded);
        QCOMPARE(all.first(f.joint,f.pair),-25.0);
        const auto preview = f.doc.pairPreview(f.down,f.pair); QVERIFY(preview.validation.ok());
        Capture selected;
        QCOMPARE(BatchWorkflowExecutor().run(preview.snapshot,{2},selected.observer()).state,RunState::Succeeded);
        QCOMPARE(selected.first(f.down,f.pair),-75.0);
        QVERIFY(f.doc.undo().ok()); QVERIFY(f.doc.redo().ok());
        QVERIFY(f.doc.plan().validation.ok());
    }
    void randomizedLinkedExecution_data() {
        QTest::addColumn<int>("seed");
        for (int seed = 0; seed < 12; ++seed) QTest::newRow(qPrintable(QString::number(seed))) << seed;
    }
    void randomizedLinkedExecution() {
        QFETCH(int, seed);
        Fixture f;
        struct Origin { NodeId node; QUuid dataset; double factor, offset; };
        const QVector<Origin> origins{{f.rootA,f.a0,1,1},{f.rootA,f.a1,1,2},
            {f.a,f.a0,2,1},{f.a,f.a1,2,2},{f.sibling,f.a0,5,1},{f.sibling,f.a1,5,2},
            {f.rootB,f.b0,1,10},{f.rootB,f.b1,1,20},{f.b,f.b0,3,10},{f.b,f.b1,3,20}};
        std::mt19937 random(1729 + seed);
        QVector<BatchPair> pairs;
        QVector<QPair<int,int>> choices;
        QSet<QPair<int,int>> used;
        while (pairs.size() < 24) {
            const int a = random() % origins.size(), b = random() % origins.size();
            if (a == b || used.contains({a,b})) continue;
            used.insert({a,b}); choices.push_back({a,b});
            const auto ref = [&](int i) { return BatchSeriesRef{origins[i].dataset,"series",origins[i].node}; };
            pairs.push_back({QUuid::createUuid(),"duplicate label",ref(a),ref(b)});
        }
        QVERIFY(f.doc.setPairs(f.joint,pairs).isEmpty());
        // Source order changes after grouping must not change A/B identities.
        auto reordered = f.doc.sources().value(f.rootB);
        std::reverse(reordered.begin(),reordered.end());
        QVERIFY(f.doc.setSources(f.rootB,reordered).isEmpty());
        const auto plan = f.doc.plan(); QVERIFY(plan.validation.ok());
        QCOMPARE(plan.batches.size(),1);
        for (int workers : {1,4,8}) {
            Capture capture;
            const auto run = BatchWorkflowExecutor().run(plan.batches[0],{workers},capture.observer());
            QCOMPARE(run.state,RunState::Succeeded);
            for (auto calls : capture.calls) QCOMPARE(calls,1);
            for (int p = 0; p < pairs.size(); ++p) {
                const auto a = origins[choices[p].first], b = origins[choices[p].second];
                const auto output = capture.outputs.value({f.joint,pairs[p].id}).outputs.value("series");
                const auto* series = dataAs<SeriesData>(output); QVERIFY(series);
                QCOMPARE(series->y.size(),512);
                for (int i = 0; i < series->y.size(); ++i) {
                    const double expected = a.factor*(a.offset+std::sin(i*.2)) - b.factor*(b.offset+std::sin(i*.2));
                    QVERIFY(std::abs(series->y[i]-expected) < 1e-11);
                }
            }
        }
        const int selected = seed % pairs.size();
        const auto preview = f.doc.pairPreview(f.down,pairs[selected].id); QVERIFY(preview.validation.ok());
        Capture capture;
        QCOMPARE(BatchWorkflowExecutor().run(preview.snapshot,{2},capture.observer()).state,RunState::Succeeded);
        const auto a = origins[choices[selected].first], b = origins[choices[selected].second];
        QVERIFY(std::abs(capture.first(f.down,pairs[selected].id) - 3*(a.factor*a.offset-b.factor*b.offset)) < 1e-11);
    }
    void codecRoundTripAndLegacy() {
        Fixture f; BatchGrouping old{{f.joint,{{f.pair,"old",{f.a0,"series"},{f.a1,"series"}}}}};
        const auto json=batchGroupingToJson(old); QCOMPARE(json["schema_version"].toInt(),1);
        QCOMPARE(batchGroupingToJson(batchGroupingFromJson(json).grouping),json);
        BatchGrouping linked{{f.joint,{f.linked()}}};const auto v2=batchGroupingToJson(linked);QCOMPARE(v2["schema_version"].toInt(),2);
        const auto parsed=batchGroupingFromJson(v2);QVERIFY(parsed.validation.ok());QCOMPARE(parsed.grouping[f.joint][0].b.node,f.b);
        QCOMPARE(batchGroupingToJson(parsed.grouping),v2);
        auto invalid=v2;invalid["schema_version"]=1;QVERIFY(!batchGroupingFromJson(invalid).validation.ok());
        auto nodes=v2["nodes"].toArray();auto node=nodes[0].toObject();auto pairs=node["pairs"].toArray();auto p=pairs[0].toObject();auto b=p["b"].toObject();
        b["node"]="not-a-node";p["b"]=b;pairs[0]=p;node["pairs"]=pairs;nodes[0]=node;invalid=v2;invalid["nodes"]=nodes;
        QVERIFY(!batchGroupingFromJson(invalid).validation.ok());
    }
    void crossBranchAndRoot_data(){QTest::addColumn<bool>("cross");QTest::addColumn<int>("threads");for(bool c:{false,true})for(int n:{1,4})QTest::newRow(qPrintable(QString("%1-%2").arg(c).arg(n)))<<c<<n;}
    void crossBranchAndRoot(){
        QFETCH(bool,cross);QFETCH(int,threads);Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked(cross)}).isEmpty());
        const auto plan=f.doc.plan();QVERIFY2(plan.validation.ok(),qPrintable(plan.validation.message()));QCOMPARE(plan.batches.size(),1);
        QCOMPARE(plan.requests.size(),cross?0:2);const auto batch=plan.batches[0];QVERIFY(batch->linkedInputs());
        Capture c;const auto run=BatchWorkflowExecutor().run(batch,{threads},c.observer());QCOMPARE(run.state,RunState::Succeeded);
        QCOMPARE(c.first(f.joint,f.pair),cross?-28.:-3.);QCOMPARE(c.first(f.down,f.pair),cross?-84.:-9.);
        for(auto count:c.calls)QCOMPARE(count,1);
        for(const auto& job:batch->jobs())for(const auto& d:job.inputs)QVERIFY(d.producer<job.index);
        const auto manifest=batch->manifest(f.pair);QCOMPARE(manifest["schema_version"].toInt(),2);
        QCOMPARE(manifest["datasets"].toArray().size(),cross?2:1);
        const auto parsed=workflowFromJson(manifest["workflow"].toObject());QVERIFY2(parsed.validation.ok(),qPrintable(parsed.validation.message()));
        QVERIFY(validateWorkflow(parsed.definition,f.doc.model().registry()).ok());
        if(cross){const auto member=batch->manifest(f.b0);QCOMPARE(QUuid(member["root"].toString()),f.rootB);QCOMPARE(member["roots"].toArray().size(),1);}
    }
    void reciprocalRootLinksWithoutJointCycle(){
        Fixture f;const auto other=f.doc.addChild(f.b,ChildSlot::Down,"linked_probe").nodeId;QVERIFY(!other.isNull());
        QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());const auto pair=QUuid::createUuid();
        QVERIFY(f.doc.setPairs(other,{{pair,"B-A",{f.b0,"series"},{f.a0,"series",f.a}}}).isEmpty());
        const auto plan=f.doc.plan();QVERIFY(plan.validation.ok());QCOMPARE(plan.requests.size(),0);QCOMPARE(plan.batches.size(),1);
        Capture c;QCOMPARE(BatchWorkflowExecutor().run(plan.batches[0],{1},c.observer()).state,RunState::Succeeded);
        QCOMPARE(c.first(f.joint,f.pair),-28.);QCOMPARE(c.first(other,pair),28.);
        for(auto count:c.calls)QCOMPARE(count,1);
        auto invalid=f.linked();invalid.b={pair,"series",other};QVERIFY(!f.doc.setPairs(f.joint,{invalid}).isEmpty());
    }
    void sharedProducerAndDifferentPorts(){
        Fixture f;const auto trend=f.doc.addChild(f.rootB,ChildSlot::Right,"linear_detrend").nodeId;QVERIFY(!trend.isNull());
        auto pair=f.linked();pair.a={f.b0,"trend",trend};pair.b={f.b0,"detrended",trend};
        QVERIFY(f.doc.setPairs(f.joint,{pair}).isEmpty());const auto plan=f.doc.plan();QVERIFY(plan.validation.ok());
        Capture c;QCOMPARE(BatchWorkflowExecutor().run(plan.batches[0],{4},c.observer()).state,RunState::Succeeded);QCOMPARE(c.calls.value({trend,f.b0}),1);
        QVERIFY(std::isfinite(c.first(f.joint,f.pair)));
    }
    void invalidReferences_data(){QTest::addColumn<int>("which");for(int n=0;n<7;++n)QTest::newRow(qPrintable(QString::number(n)))<<n;}
    void invalidReferences(){
        QFETCH(int,which);Fixture f;auto pair=f.linked();switch(which){
        case 0:pair.b.node=QUuid::createUuid();break;case 1:pair.b.node=f.joint;break;case 2:pair.b.node=f.down;break;
        case 3:pair.b.port="missing";break;case 4:pair.b.dataset=QUuid::createUuid();break;
        case 5:pair.b={f.a0,"series",f.a};break;case 6:pair.b.node=f.doc.addChild(f.b,ChildSlot::Down,"series_summary").nodeId;pair.b.port="summary";break;}
        const auto revision=f.doc.revision();QVERIFY(!f.doc.setPairs(f.joint,{pair}).isEmpty());QCOMPARE(f.doc.revision(),revision);
        const auto direct=prepareLinkedBatch(f.doc.model().freeze().snapshot,f.sources(),{{f.joint,{pair}}});QVERIFY(!direct.validation.ok());
    }
    void rejectOutsideScopeAndAliases(){
        Fixture f;const auto frozen=f.doc.model().freeze().snapshot;
        QVERIFY(!prepareBatch(frozen,f.rootA,f.doc.datasets(f.rootA),{{f.joint,{f.linked()}}}).validation.ok());
        auto one=f.linked();auto alias=one;alias.id=QUuid::createUuid();alias.a.node=f.a;
        QVERIFY(!f.doc.setPairs(f.joint,{one,alias}).isEmpty());
        QVERIFY(!prepareLinkedBatch(frozen,f.sources(),{{f.joint,{one,alias}}}).validation.ok());
        auto sources=f.sources();sources[f.rootB][0].id=f.a0;
        QVERIFY(!prepareLinkedBatch(frozen,sources,{{f.joint,{one}}}).validation.ok());
    }
    void candidatesExcludeJointPaths(){
        Fixture f;const auto candidates=linkedInputOrigins(f.doc.model(),f.joint);
        QVERIFY(candidates.contains(f.rootA));QVERIFY(candidates.contains(f.a));QVERIFY(candidates.contains(f.b));QVERIFY(candidates.contains(f.sibling));
        QVERIFY(!candidates.contains(f.joint));QVERIFY(!candidates.contains(f.down));
        QVERIFY(linkedInputOrigins(f.doc.model(),f.a).isEmpty());
    }
    void aggregateUnassignedCandidatesAreBounded(){
        WorkflowDocument doc(registry());const auto root=doc.addRoot("input_series").nodeId;
        auto previous=root;QVector<NodeId> joints;
        for(int i=0;i<60;++i){const auto branch=doc.addChild(previous,ChildSlot::Down,"affine").nodeId;
            QVERIFY(!branch.isNull());const auto joint=doc.addChild(branch,ChildSlot::Left,"linked_probe").nodeId;
            QVERIFY(!joint.isNull());joints.push_back(joint);previous=branch;
        }
        QVector<BatchDataset> sources;const auto source=data(1);
        for(int i=0;i<1000;++i)sources.push_back({QUuid::createUuid(),source});
        BatchGrouping groups;for(auto joint:joints)groups[joint]={{QUuid::createUuid(),"",{sources[0].id,"series"},{sources[0].id,"series",root}}};
        const auto plan=prepareLinkedBatch(doc.model().freeze().snapshot,{{root,sources}},groups);
        QVERIFY(!plan.validation.ok());QVERIFY(plan.validation.message().contains("unassigned output limit"));
    }
    void deletionUndoAndNoRetarget(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());const auto before=batchGroupingToJson(f.doc.grouping());
        const auto frozen=f.doc.plan().batches[0];QVERIFY(f.doc.removeSubtree(f.b).ok());
        QCOMPARE(batchGroupingToJson(f.doc.grouping()),before);QVERIFY(!f.doc.plan().validation.ok());QVERIFY(!f.doc.pairPreview(f.down,f.pair).validation.ok());
        const auto replacement=f.doc.addChild(f.rootB,ChildSlot::Down,"affine",{{"factor",3}}).nodeId;QVERIFY(replacement!=f.b);
        QVERIFY(!f.doc.plan().validation.ok());QVERIFY(f.doc.undo().ok());QVERIFY(f.doc.undo().ok());QVERIFY(f.doc.plan().validation.ok());
        Capture c;QCOMPARE(BatchWorkflowExecutor().run(frozen,{1},c.observer()).state,RunState::Succeeded);QCOMPARE(c.first(f.joint,f.pair),-28.);
    }
    void removedDatasetRemainsUnresolved(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());const auto before=batchGroupingToJson(f.doc.grouping());
        QVERIFY(f.doc.setSources(f.rootB,{f.doc.sources().value(f.rootB)[1]}).isEmpty());QCOMPARE(batchGroupingToJson(f.doc.grouping()),before);
        QVERIFY(!f.doc.plan().validation.ok());QVERIFY(f.doc.undo().ok());QVERIFY(f.doc.plan().validation.ok());
        QVERIFY(f.doc.removeSubtree(f.rootB).ok());QCOMPARE(batchGroupingToJson(f.doc.grouping()),before);QVERIFY(!f.doc.plan().validation.ok());
        QVERIFY(f.doc.undo().ok());QVERIFY(f.doc.plan().validation.ok());
    }
    void rewriteCannotBreakReferencedPort(){
        Fixture f;const auto bchild=f.doc.addChild(f.b,ChildSlot::Down,"affine").nodeId;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());
        NodeRewriteRequest r{NodeRewriteKind::Replace,f.b,"linear_detrend","series",{{bchild,"trend"}}, {}};
        const auto before=workflowToJson(f.doc.model().definition());QVERIFY(!f.doc.prepareRewrite(r).ok());QCOMPARE(workflowToJson(f.doc.model().definition()),before);
        r={NodeRewriteKind::InsertBefore,f.b,"affine","series",{{f.b,"series"}},{{"factor",2}}};const auto prepared=f.doc.prepareRewrite(r);QVERIFY(prepared.ok());QVERIFY(f.doc.commitEdit(prepared.edit).ok());
        Capture c;QCOMPARE(BatchWorkflowExecutor().run(f.doc.plan().batches[0],{1},c.observer()).state,RunState::Succeeded);QCOMPARE(c.first(f.joint,f.pair),-58.);
        QVERIFY(f.doc.undo().ok());QCOMPARE(f.doc.grouping()[f.joint][0].b.node,f.b);
    }
    void previewOnlyDependencyClosure(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());
        const auto preview=f.doc.pairPreview(f.down,f.pair);QVERIFY2(preview.validation.ok(),qPrintable(preview.validation.message()));
        QSet<NodeId> nodes;QSet<QUuid> items;for(const auto& job:preview.snapshot->jobs()){nodes.insert(job.node);items.insert(job.item);}
        QVERIFY(nodes.contains(f.rootB));QVERIFY(nodes.contains(f.b));QVERIFY(!nodes.contains(f.sibling));QVERIFY(!items.contains(f.a1));QVERIFY(!items.contains(f.b1));
        QCOMPARE(preview.snapshot->jobs().size(),6);Capture c;QCOMPARE(BatchWorkflowExecutor().run(preview.snapshot,{1},c.observer()).state,RunState::Succeeded);QCOMPARE(c.first(f.down,f.pair),-84.);
    }
    void previewIgnoresUnusedPlacementSource(){
        Fixture f;auto pair=f.linked();pair.a={f.b0,"series",f.rootB};pair.b={f.b0,"series",f.b};
        QVERIFY(f.doc.setPairs(f.joint,{pair}).isEmpty());QVERIFY(f.doc.setSources(f.rootA,{}).isEmpty());
        const auto preview=f.doc.pairPreview(f.joint,f.pair);QVERIFY2(preview.validation.ok(),qPrintable(preview.validation.message()));
        QCOMPARE(preview.snapshot->jobs().size(),3);Capture c;QCOMPARE(BatchWorkflowExecutor().run(preview.snapshot,{1},c.observer()).state,RunState::Succeeded);
        QCOMPARE(c.first(f.joint,f.pair),-20.);const auto manifest=preview.snapshot->manifest(f.pair);
        const auto parsed=workflowFromJson(manifest["workflow"].toObject());QVERIFY(parsed.validation.ok());
        QVERIFY(validateWorkflow(parsed.definition,f.doc.model().registry()).ok());
    }
    void generatedOriginAndSharedReferenceBudget(){
        Fixture f;const auto generated=f.doc.addRoot("sine_source",{{"count",512},{"step",1}}).nodeId;
        auto pair=f.linked();pair.b={generated,"series",generated};QVERIFY(f.doc.setPairs(f.joint,{pair}).isEmpty());
        Capture c;const auto plan=f.doc.plan();QVERIFY(plan.validation.ok());QCOMPARE(plan.requests.size(),2);
        QCOMPARE(BatchWorkflowExecutor().run(plan.batches[0],{2},c.observer()).state,RunState::Succeeded);QCOMPARE(c.first(f.joint,f.pair),2.);
        QVector<SourceItem> inputs;for(int i=0;i<256;++i)inputs.push_back(data(i+1));QVERIFY(f.doc.setSources(f.rootA,inputs).isEmpty());
        QVector<BatchPair> pairs;for(const auto& dataset:f.doc.datasets(f.rootA))pairs.push_back({QUuid::createUuid(),"",{dataset.id,"series"},{generated,"series",generated}});
        QVERIFY(f.doc.setPairs(f.joint,pairs).isEmpty());const auto large=f.doc.plan();QVERIFY(large.validation.ok());
        QElapsedTimer elapsed;elapsed.start();const auto run=BatchWorkflowExecutor().run(large.batches[0],{4,256*1024});
        QCOMPARE(run.state,RunState::Succeeded);QVERIFY(run.peakCachedPayloadBytes<=256*1024);
        qInfo()<<"256 cross-root pairs, cache bytes:"<<run.peakCachedPayloadBytes<<"elapsed ms:"<<elapsed.elapsed();
    }
    void previewRefreshAndIdentityLabels(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());WorkflowPreviewController p(&f.doc);p.selectNode(f.joint);p.selectPair(f.pair);p.setActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(p.state(),WorkflowPreviewController::State::Ready,10000);
        QVERIFY(p.result()->labelB.contains(f.b.toString(QUuid::WithoutBraces).left(8)));
        QCOMPARE(dataAs<SeriesData>(p.result()->second.outputs.value("series"))->y.front(),30.);
        QVERIFY(f.doc.setParameters(f.b,{{"factor",4}}).ok());QTRY_COMPARE_WITH_TIMEOUT(p.state(),WorkflowPreviewController::State::Ready,10000);
        QCOMPARE(dataAs<SeriesData>(p.result()->output.outputs.value("series"))->y.front(),-38.);
        QVERIFY(f.doc.undo().ok());QTRY_COMPARE_WITH_TIMEOUT(p.state(),WorkflowPreviewController::State::Ready,10000);
        QCOMPARE(dataAs<SeriesData>(p.result()->output.outputs.value("series"))->y.front(),-28.);
    }
    void pairedEditorRetainsNodeIdentity(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());PairingPanel panel(&f.doc);panel.resize(1050,700);panel.selectNode(f.joint);panel.show();
        QVERIFY(panel.findChild<QPushButton*>("managePairs")->isEnabled());const auto before=batchGroupingToJson(f.doc.grouping());
        panel.findChild<QPushButton*>("managePairs")->click();auto* dialog=panel.findChild<PairingDialog*>();QVERIFY(dialog);
        const auto ref=dialog->findChild<QTableWidget*>("pairTable")->item(0,2)->data(Qt::UserRole).toMap();QCOMPARE(ref.value("node").toUuid(),f.b);
        QTest::keyClick(dialog,Qt::Key_Escape);QCOMPARE(batchGroupingToJson(f.doc.grouping()),before);
    }
    void crossNodeChooserCommitAndUndo(){
        Fixture f;PairingPanel panel(&f.doc);panel.resize(1050,700);panel.selectNode(f.joint);panel.show();
        panel.findChild<QPushButton*>("managePairs")->click();auto* dialog=panel.findChild<PairingDialog*>();QVERIFY(dialog);
        dialog->findChild<QPushButton*>("addPair")->click();
        QVERIFY(aster::test::selectMember(aster::test::openMember(dialog,0,1),f.a,0));
        QVERIFY(aster::test::selectMember(aster::test::openMember(dialog,0,2),f.b,0));
        QVERIFY(f.doc.grouping().value(f.joint).isEmpty());QVERIFY(dialog->findChild<QPushButton*>("pairingConfirm")->isEnabled());
        dialog->findChild<QPushButton*>("pairingConfirm")->click();auto pairs=f.doc.grouping().value(f.joint);QCOMPARE(pairs.size(),1);
        QVERIFY(pairs[0].a.node.isNull());QCOMPARE(pairs[0].b.node,f.b);QCOMPARE(pairs[0].b.dataset,f.b0);
        const auto frozen=batchGroupingToJson(f.doc.grouping());QVERIFY(f.doc.undo().ok());QVERIFY(f.doc.grouping().value(f.joint).isEmpty());QVERIFY(f.doc.redo().ok());QCOMPARE(batchGroupingToJson(f.doc.grouping()),frozen);
        Capture c;QCOMPARE(BatchWorkflowExecutor().run(f.doc.plan().batches[0],{2},c.observer()).state,RunState::Succeeded);QCOMPARE(c.first(f.joint,pairs[0].id),-28.);
    }
    void repairRemovedOrigin(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());QVERIFY(f.doc.removeSubtree(f.b).ok());
        PairingPanel panel(&f.doc);panel.resize(1050,700);panel.selectNode(f.joint);panel.show();panel.findChild<QPushButton*>("managePairs")->click();
        auto* dialog=panel.findChild<PairingDialog*>();QVERIFY(dialog);QVERIFY(!dialog->findChild<QPushButton*>("pairingConfirm")->isEnabled());
        QVERIFY(dialog->findChild<QTableWidget*>("pairTable")->item(0,2)->text().contains(text("ui.joint_unavailable")));
        QVERIFY(aster::test::selectMember(aster::test::openMember(dialog,0,2),f.sibling,0));
        dialog->findChild<QPushButton*>("pairingConfirm")->click();QCOMPARE(f.doc.grouping()[f.joint][0].id,f.pair);QCOMPARE(f.doc.grouping()[f.joint][0].b.node,f.sibling);
        QVERIFY(f.doc.undo().ok());QVERIFY(!f.doc.plan().validation.ok());QVERIFY(f.doc.undo().ok());QVERIFY(f.doc.plan().validation.ok());
    }
    void cancelledChooserAndImmediateReopen(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());PairingPanel panel(&f.doc);panel.resize(1050,700);panel.selectNode(f.joint);panel.show();
        auto* manage=panel.findChild<QPushButton*>("managePairs");manage->click();auto* first=panel.findChild<PairingDialog*>();QVERIFY(first);
        const auto before=batchGroupingToJson(f.doc.grouping());auto* picker=aster::test::openMember(first,0,2);QVERIFY(picker);picker->reject();
        first->findChild<QPushButton*>("choosePairB")->click();int visible=0;for(auto* next:first->findChildren<JointInputDialog*>())if(next->isVisible())++visible;QCOMPARE(visible,1);
        first->reject();manage->click();visible=0;PairingDialog* reopened=nullptr;for(auto* next:panel.findChildren<PairingDialog*>())if(next->isVisible()){++visible;reopened=next;}
        QCOMPARE(visible,1);QVERIFY(reopened!=first);QCOMPARE(batchGroupingToJson(f.doc.grouping()),before);reopened->reject();
    }
    void chooserFiltersAndRejectsStaleSelection(){
        Fixture f;QWidget owner;owner.resize(1050,700);owner.show();QPointer<JointInputDialog> picker=new JointInputDialog(&f.doc,f.joint,{},&owner);picker->openWithinOwner();
        auto* tree=picker->findChild<QTreeWidget*>("jointSourceTree");QTreeWidgetItem* b=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it){const auto id=(*it)->data(0,Qt::UserRole).toUuid();QVERIFY(id!=f.joint&&id!=f.down);if(id==f.b)b=*it;}
        QVERIFY(b);tree->setCurrentItem(b);auto* table=picker->findChild<QTableView*>("jointMemberTable");QCOMPARE(table->model()->rowCount(),2);
        QVERIFY(table->model()->index(0,0).data().toString()!=table->model()->index(1,0).data().toString());
        const auto secondName=table->model()->index(1,0).data().toString();
        table->setCurrentIndex(table->model()->index(0,0));QVERIFY(picker->findChild<QPushButton*>("jointInputConfirm")->isEnabled());
        picker->findChild<QLineEdit*>("jointDataSearch")->setText("no matching file");QCOMPARE(table->model()->rowCount(),0);QVERIFY(!picker->findChild<QPushButton*>("jointInputConfirm")->isEnabled());
        // Search follows visible labels; internal UUIDs must not create matches.
        picker->findChild<QLineEdit*>("jointDataSearch")->setText(f.b1.toString());QCOMPARE(table->model()->rowCount(),0);
        QVERIFY(!picker->findChild<QPushButton*>("jointInputConfirm")->isEnabled());
        picker->findChild<QLineEdit*>("jointDataSearch")->setText(secondName);QCOMPARE(table->model()->rowCount(),1);
        QVERIFY(!picker->findChild<QPushButton*>("jointInputConfirm")->isEnabled());
        QVERIFY(f.doc.setParameters(f.b,{{"factor",4}}).ok());QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!picker);
    }
    void pairDialogInvalidationAndLock(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());QWidget owner;owner.resize(1050,700);owner.show();
        QPointer<PairingDialog> dialog=new PairingDialog(&f.doc,f.joint,&owner);dialog->openWithinOwner();
        QPointer<JointInputDialog> picker=aster::test::openMember(dialog,0,2);QVERIFY(picker);
        QVERIFY(f.doc.setParameters(f.a,{{"factor",4}}).ok());QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!dialog);QVERIFY(!picker);
        dialog=new PairingDialog(&f.doc,f.joint,&owner);dialog->openWithinOwner();f.doc.setEditingLocked(true);const auto before=f.doc.revision();
        dialog->findChild<QPushButton*>("pairingConfirm")->click();QCOMPARE(f.doc.revision(),before);QVERIFY(!dialog->isVisible());f.doc.setEditingLocked(false);
        auto* transient=new WorkflowDocument(registry());const auto root=transient->addRoot("sine_source").nodeId;const auto joint=transient->addChild(root,ChildSlot::Down,"linked_probe").nodeId;
        QPointer<PairingDialog> destroyed=new PairingDialog(transient,joint,&owner);destroyed->openWithinOwner();delete transient;QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(!destroyed);
    }
    void canvasShowsOnlySelectedPair(){
        Fixture f;auto other=f.linked();other.id=QUuid::createUuid();other.b={f.a1,"series",f.sibling};
        QVERIFY(f.doc.setPairs(f.joint,{f.linked(),other}).isEmpty());WorkflowCanvas canvas(&f.doc);canvas.resize(1100,700);canvas.show();connect(&f.doc,&WorkflowDocument::changed,&canvas,&WorkflowCanvas::rebuild);
        const auto origins=[&]{QSet<NodeId> ids;for(auto* item:canvas.scene()->items())if(item->data(0).toInt()==5&&!item->data(1).toUuid().isNull())ids.insert(item->data(1).toUuid());return ids;};
        canvas.setSelectedNode(f.joint);QVERIFY(origins().contains(f.a));QVERIFY(origins().contains(f.b));QCOMPARE(origins().size(),2);
        canvas.setPreviewPair(other.id);QVERIFY(origins().contains(f.sibling));QVERIFY(!origins().contains(f.b));
        canvas.setSelectedNode(f.down);QVERIFY(origins().contains(f.sibling));canvas.setSelectedNode(f.a);QVERIFY(origins().isEmpty());
        canvas.setSelectedNode(f.joint);canvas.setPreviewPair(f.pair);QVERIFY(f.doc.removeSubtree(f.b).ok());QCOMPARE(origins().size(),1);
        int missing=0;for(auto* item:canvas.scene()->items())if(item->data(0).toInt()==6&&!item->data(2).toBool())++missing;QCOMPARE(missing,1);
        QVERIFY(f.doc.undo().ok());QCOMPARE(origins().size(),2);
        canvas.fitWorkflow();const auto pictures=qEnvironmentVariable("ASTER_LINKED_SCREENSHOT_DIR");if(!pictures.isEmpty())QVERIFY(canvas.grab().save(pictures+"/reference-canvas.png"));
    }
    void dialogAppearance_data(){QTest::addColumn<QString>("language");QTest::addColumn<QString>("theme");for(auto l:{"zh","en"})for(auto t:{"light","dark"})QTest::newRow(qPrintable(QString(l)+"-"+t))<<QString(l)<<QString(t);}
    void dialogAppearance(){
        QFETCH(QString,language);QFETCH(QString,theme);setLanguage(language);ThemeManager::instance().apply(*qApp,theme);
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());QWidget owner;owner.resize(1100,760);owner.show();
        QPointer<PairingDialog> dialog=new PairingDialog(&f.doc,f.joint,&owner);dialog->openWithinOwner();QCoreApplication::processEvents();
        const auto pictures=qEnvironmentVariable("ASTER_LINKED_SCREENSHOT_DIR");const auto suffix=language+"-"+theme;
        if(!pictures.isEmpty())QVERIFY(dialog->grab().save(pictures+"/pairs-"+suffix+".png"));
        QPointer<JointInputDialog> picker=aster::test::openMember(dialog,0,2);QVERIFY(picker);
        if(!pictures.isEmpty())QVERIFY(picker->grab().save(pictures+"/sources-"+suffix+".png"));
        auto* candidates=picker->findChild<QTableView*>("jointMemberTable");
        QVERIFY(candidates->viewport()->height()>=candidates->verticalHeader()->defaultSectionSize());
        QVERIFY(dialog->geometry().contains(picker->frameGeometry()));picker->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);dialog->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        owner.resize(620,460);picker=new JointInputDialog(&f.doc,f.joint,f.linked().b,&owner);picker->openWithinOwner();QCoreApplication::processEvents();
        QCOMPARE(picker->findChild<QSplitter*>("jointInputSplitter")->orientation(),Qt::Vertical);
        const auto bounds=popupBounds(&owner,QRect(owner.mapToGlobal(QPoint()),owner.size()));QVERIFY(bounds.contains(picker->frameGeometry()));
        if(!pictures.isEmpty())QVERIFY(picker->grab().save(pictures+"/sources-small-"+suffix+".png"));
        picker->reject();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);setLanguage("zh");ThemeManager::instance().apply(*qApp,"light");
    }
    void realWelchMatchesDirectInputs(){
        Fixture f;QVERIFY(f.doc.removeSubtree(f.joint).ok());
        const QJsonObject params{{"psd_window",64},{"psd_overlap",32}};
        f.joint=f.doc.addChild(f.a,ChildSlot::Down,"cross_spectrum",params).nodeId;
        QVERIFY(!f.joint.isNull());QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());
        const auto plan=f.doc.plan();QVERIFY(plan.validation.ok());Capture c;
        QCOMPARE(BatchWorkflowExecutor().run(plan.batches[0],{4},c.observer()).state,RunState::Succeeded);
        auto a=*dataAs<SeriesData>(data(1).data),b=*dataAs<SeriesData>(data(10).data);
        for(auto& y:a.y)y*=2;
        for(auto& y:b.y)y*=3;
        const auto direct=CrossSpectrumTask(params).execute({makeData(a),makeData(b)},{});
        QCOMPARE(direct.state,NodeState::Succeeded);
        const auto expected=encodeData(direct.outputs.value("cross_spectrum")).bytes;
        QCOMPARE(encodeData(c.outputs.value({f.joint,f.pair}).outputs.value("cross_spectrum")).bytes,expected);
        WorkflowPreviewController preview(&f.doc);preview.selectNode(f.joint);preview.selectPair(f.pair);preview.setActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(preview.state(),WorkflowPreviewController::State::Ready,10000);
        QCOMPARE(encodeData(preview.result()->output.outputs.value("cross_spectrum")).bytes,expected);
        QTemporaryDir tmp;auto saved=BatchRunner().run(plan,tmp.filePath("welch.sqlite"),4);QVERIFY(saved.error.isEmpty());
        ResultRepository repo;QVERIFY(repo.open(tmp.filePath("welch.sqlite")).isEmpty());bool found=false;
        for(const auto& item:saved.items){const auto record=repo.load(item.persistence.recordId);QVERIFY(record.ok());
            if(record.run.provenance.contains("batch_pair")){found=true;
                QCOMPARE(encodeData(record.run.nodes.value(f.joint).outputs.value("cross_spectrum")).bytes,expected);
            }
        }
        QVERIFY(found);
    }
    void persistenceAndRootRouting(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());QTemporaryDir tmp;
        QMap<NodeId,NodeId> routes;const auto plan=f.doc.plan();QVERIFY(plan.validation.ok());
        auto result=BatchRunner().run(plan,tmp.filePath("linked.sqlite"),4,[&](const RunEvent& e){routes[e.nodeId]=e.root;});
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.items.size(),5);
        QCOMPARE(routes.value(f.b),f.rootB);QCOMPARE(routes.value(f.joint),f.rootA);
        ResultRepository repo;QVERIFY(repo.open(tmp.filePath("linked.sqlite")).isEmpty());int pairs=0;
        for(const auto& item:result.items){QVERIFY(item.persistence.saved());const auto stored=repo.load(item.persistence.recordId);QVERIFY(stored.ok());
            if(stored.run.provenance.contains("batch_pair")) {++pairs;const auto links=stored.run.provenance["input_records"].toArray();QCOMPARE(links.size(),2);
                QCOMPARE(links[1].toObject()["node_id"].toString(),f.b.toString(QUuid::WithoutBraces));
                QCOMPARE(stored.run.provenance["batch_execution"].toObject()["datasets"].toArray().size(),2);
                QCOMPARE(dataAs<SeriesData>(stored.run.nodes[f.down].outputs["series"])->y.front(),-84.);
            }
        }
        QCOMPARE(pairs,1);
    }
    void cancelledAndFailureIsolation(){
        Fixture f;QVERIFY(f.doc.setPairs(f.joint,{f.linked()}).isEmpty());auto sources=f.doc.sources().value(f.rootB);sources[0].data={};
        // Compiler preserves invalid bound values for execution-time validation;
        // a failed referenced input blocks its consumers but not independent data.
        auto bindings=f.sources();bindings[f.rootB][0].source.data={};
        const auto plan=prepareLinkedBatch(f.doc.model().freeze().snapshot,bindings,{{f.joint,{f.linked()}}});QVERIFY(plan.validation.ok());
        Capture c;const auto result=BatchWorkflowExecutor().run(plan.snapshot,{2},c.observer());QCOMPARE(result.state,RunState::PartialFailed);
        QCOMPARE(c.outputs.value({f.joint,f.pair}).state,NodeState::Blocked);QCOMPARE(c.outputs.value({f.b,f.b1}).state,NodeState::Succeeded);
        CancellationToken token;token.cancel();Capture cancelled;
        QCOMPARE(BatchWorkflowExecutor().run(plan.snapshot,{2},cancelled.observer(),token).state,RunState::Cancelled);
        for(const auto& summary:cancelled.outputs)QCOMPARE(summary.state,NodeState::Cancelled);
    }
};
QTEST_MAIN(LinkedInputTests)
#include "linked_input_tests.moc"
