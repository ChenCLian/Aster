#include "ar1_global_audit.h"
#include "math/statistics/ar1_global.h"
#include "processing/services/ar1_reference_cache.h"
#include "processing/ar1_global_task.h"
#include "processing/scientific_registry.h"
#include "processing/lowess_task.h"
#include "application/batch_runner.h"
#include "io/database/data_codec.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QLockFile>
#include <future>
#include <cmath>
using namespace aster;
namespace {
SeriesData series(int n=64){SeriesData d;d.xAxis={"Time","ka"};d.yAxis={"Signal","u"};
    for(int i=0;i<n;++i){d.x<<i;d.y<<std::sin(.39*i)+.3*std::cos(1.21*i)+.1*std::sin(2.13*i);}return d;}
std::vector<double> ys(const SeriesData& d){return {d.y.begin(),d.y.end()};}
numeric::Ar1GlobalOptions small(){numeric::Ar1GlobalOptions o;o.rhoLower=-.05;o.rhoUpper=.05;return o;}
}
class Ar1GlobalAudit : public QObject {
    Q_OBJECT
private slots:
    void productionStatisticParityAndUnits();
    void invalidInputsAndBudget();
    void cacheRoundTripAndCorruption();
    void cancellationAndWaiting();
    void immutableConcurrentEvaluation();
    void workflowPersistenceAndProgress();
};
void Ar1GlobalAudit::productionStatisticParityAndUnits(){
    const auto registry=makeScientificRegistry();auto o=small();const auto d=series(65);
    for(bool lowess:{false,true}){
        o.lowess=lowess;auto expected=makeData(d);
        if(lowess)expected=registry.create("detrend_lowess",{})->execute({expected},{}).outputs["detrended"];
        const auto result=numeric::ar1GlobalStatistic(ys(d),1,o);
        const auto values=dataAs<SeriesData>(expected);QVERIFY(values);
        for(int i=0;i<d.y.size();++i)QVERIFY(std::abs(result.analysedSeries[i]-values->y[i])<1e-12);
        const auto mtm=registry.create("mtm",{{"ar1",false}})->execute({expected},{});
        const auto spectrum=dataAs<SpectrumData>(mtm.outputs["spectrum"]);QVERIFY(spectrum);
        for(int i=0;i<spectrum->power.size();++i)QVERIFY(std::abs(spectrum->power[i]-result.spectrum.power[i])<1e-11);
        auto altered=ys(d);for(auto& v:altered)v=3*v+70;
        const auto scaled=numeric::ar1GlobalStatistic(altered,2,o);
        QVERIFY(std::abs(result.maximumRatio-scaled.maximumRatio)<1e-6);
        QVERIFY(std::abs(result.lag-scaled.lag)<1e-12);
    }
}
void Ar1GlobalAudit::invalidInputsAndBudget(){
    auto o=small();auto y=ys(series());
    QVERIFY_EXCEPTION_THROWN(numeric::ar1GlobalStatistic(std::vector<double>(64,1),1,o),std::invalid_argument);
    y[3]=qQNaN();QVERIFY_EXCEPTION_THROWN(numeric::ar1GlobalStatistic(y,1,o),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::ar1GlobalGrid(2048,o),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::ar1GlobalGrid(63,o),std::invalid_argument);
    o.analysisReplicates=10;QVERIFY_EXCEPTION_THROWN(numeric::ar1GlobalGrid(64,o),std::invalid_argument);
    const auto registry=makeScientificRegistry();
    QVERIFY(registry.find("ar1_global")->descriptor.catalogVisible);
    QCOMPARE(registry.find("ar1_global")->descriptor.defaultOutput,QString("ar1_global_test"));
    QVERIFY(!registry.normalize("ar1_global",{{"ar1_precision","unknown"}}).validation.ok());
    auto task=registry.create("ar1_global",{});
    QCOMPARE(task->execute({makeData(TableData{{"a"},{{1}}})},{}).state,NodeState::Failed);
    auto d=series();d.x[5]=d.x[4];auto failed=task->execute({makeData(d)},{});
    QCOMPARE(failed.diagnostic().code,errorCodeName(ErrorCode::XDuplicate));QVERIFY(failed.outputs.isEmpty());
}
void Ar1GlobalAudit::cacheRoundTripAndCorruption(){
    QTemporaryDir dir;QVERIFY(dir.isValid());Ar1ReferenceCache cache(dir.path());auto o=small();
    auto first=cache.acquire(64,o);QVERIFY(!first.hit);QVERIFY(first.saved);
    auto second=cache.acquire(64,o);QVERIFY(second.hit);QCOMPARE(first.key,second.key);
    QCOMPARE(first.bank->rows.front().analysis,second.bank->rows.front().analysis);
    const auto a=numeric::evaluateAr1Global(ys(series()),1,*first.bank);
    const auto b=numeric::evaluateAr1Global(ys(series()),1,*second.bank);QCOMPARE(a.test.p,b.test.p);
    const auto path=dir.filePath(first.key+".bank");QFile f(path);QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.seek(f.size()-1));f.write("x",1);f.close();
    auto repaired=cache.acquire(64,o);QVERIFY(!repaired.hit);QVERIFY(repaired.saved);
    QCOMPARE(repaired.bank->rows.front().analysis,first.bank->rows.front().analysis);
    ++o.seed;auto other=cache.acquire(64,o);QVERIFY(other.key!=first.key);QVERIFY(!other.hit);
    auto malformed=*first.bank;malformed.rows.pop_back();
    QVERIFY_EXCEPTION_THROWN(numeric::evaluateAr1Global(ys(series()),1,malformed),std::invalid_argument);
}
void Ar1GlobalAudit::cancellationAndWaiting(){
    QTemporaryDir dir;Ar1ReferenceCache cache(dir.path());auto o=small();bool cancelled=false;
    QVERIFY_EXCEPTION_THROWN(cache.acquire(64,o,[&]{return cancelled;},[&](int,const QString&){cancelled=true;}),std::runtime_error);
    QVERIFY(QDir(dir.path()).entryList({"*.bank"},QDir::Files).empty());
    auto reference=cache.acquire(64,o);QLockFile lock(dir.filePath(reference.key+".bank.lock"));QVERIFY(lock.tryLock());
    cancelled=false;bool waiting=false;
    QVERIFY_EXCEPTION_THROWN(cache.acquire(64,o,[&]{return cancelled;},[&](int,const QString& key){waiting=key=="progress.ar1_waiting";cancelled=true;}),std::runtime_error);
    QVERIFY(waiting);
}
void Ar1GlobalAudit::immutableConcurrentEvaluation(){
    auto bank=numeric::buildAr1ReferenceBank(64,small());const auto y=ys(series());
    auto f=std::async(std::launch::async,[&]{return numeric::evaluateAr1Global(y,1,*bank);});
    const auto a=numeric::evaluateAr1Global(y,1,*bank),b=f.get();QCOMPARE(a.test.p,b.test.p);
    QCOMPARE(a.test.includedModels,b.test.includedModels);
}
void Ar1GlobalAudit::workflowPersistenceAndProgress(){
    QTemporaryDir dir;auto registry=makeScientificRegistry();auto descriptor=Ar1GlobalTask::descriptor();descriptor.key="ar1_test_alias";
    QVERIFY(registry.add(descriptor,[&](const QJsonObject& p){return std::make_unique<Ar1GlobalTask>(p,dir.filePath("cache"));}).ok());
    WorkflowModel model(registry);const auto root=model.addRoot("input_series",{0,0}).nodeId;
    const auto child=model.addChild(root,ChildSlot::Down,"ar1_test_alias").nodeId;
    const RunRequest request{model.freeze().snapshot,root,{"test",makeData(series()),{}}};
    int progress=0;const auto run=WorkflowExecutor().run(request,[&](const RunEvent& e){
        if(e.progressPercent>=0){++progress;QVERIFY(e.progressPercent<=100);QCOMPARE(e.state,NodeState::Running);QCOMPARE(e.nodeId,child);}
    });
    QCOMPARE(run.state,RunState::Succeeded);QVERIFY(progress>5);const auto result=run.nodes[child];
    QVERIFY(result.report.contains("reference_key"));QVERIFY(!result.report["reference_cache_hit"].toBool());
    QVERIFY(result.outputs.contains("ar1_global_test"));QVERIFY(!result.outputs.contains("peaks"));
    ResultRepository repository;QVERIFY(repository.open(dir.filePath("test.sqlite")).isEmpty());const auto saved=repository.save(run);QVERIFY(saved.saved());
    auto loaded=repository.load(saved.recordId);QVERIFY(loaded.error.isEmpty());
    QCOMPARE(loaded.run.nodes[child].report,result.report);
    for (auto it=result.outputs.begin();it!=result.outputs.end();++it) {
        const auto actual=encodeData(loaded.run.nodes[child].outputs[it.key()]), expected=encodeData(it.value());
        QVERIFY(actual.ok() && expected.ok());QCOMPARE(actual.bytes,expected.bytes);
    }
    const auto again=WorkflowExecutor().run(request);QVERIFY(again.nodes[child].report["reference_cache_hit"].toBool());
    QCOMPARE(again.nodes[child].report["global_p"],result.report["global_p"]);
    CancellationToken cancel;int transitions=0;
    const auto stopped=WorkflowExecutor().run(request,[&](const RunEvent& e){if(e.nodeId==child&&e.state==NodeState::Running){++transitions;cancel.cancel();}},cancel);
    QCOMPARE(stopped.nodes[child].state,NodeState::Cancelled);QVERIFY(stopped.nodes[child].outputs.empty());QCOMPARE(transitions,1);
}
int runAr1GlobalAudit(int argc,char** argv){Ar1GlobalAudit tests;return QTest::qExec(&tests,argc,argv);}
#include "ar1_global_audit.moc"
