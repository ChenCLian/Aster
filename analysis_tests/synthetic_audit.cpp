// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "synthetic_audit.h"
#include "math/synthetic/orbital_proxy.h"
#include "processing/synthetic_orbital_task.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <future>
#include <numeric>
#include <limits>
#include <stdexcept>
using namespace aster;
namespace {
synthetic::Model model(const QJsonObject& j) {
    synthetic::Model m{};for(int i=0;i<5;++i){m.frequencyMean[i]=j["frequency_mean_arcsec_yr"].toArray()[i].toDouble();m.frequencySd[i]=j["frequency_sd_arcsec_yr"].toArray()[i].toDouble();}
    auto s=j["smooth_logit"].toObject();m.g5=j["g5"].toDouble();m.interceptSd=s["intercept_sd"].toDouble();m.slopeSd=s["slope_sd"].toDouble();m.residualSd=s["residual_sd"].toDouble();m.lengthScale=s["length_scale"].toDouble();return m;
}
std::vector<double> vector(const QJsonValue& j){std::vector<double> r;for(auto v:j.toArray())r.push_back(v.toDouble());return r;}
template<std::size_t N> void read(std::array<double,N>& v,const QJsonValue& j){auto a=j.toArray();QCOMPARE(a.size(),qsizetype(N));for(std::size_t i=0;i<N;++i)v[i]=a[qsizetype(i)].toDouble();}
template<class A,class B> void close(const A& actual,const B& expected,double tol=2e-10){
    QCOMPARE(actual.size(),expected.size());
    for(std::size_t i=0;i<std::size_t(actual.size());++i)QVERIFY2(std::abs(actual[i]-expected[i])<=tol*(1+std::abs(expected[i])),qPrintable(QString("index %1: %2 vs %3").arg(i).arg(actual[i],0,'g',17).arg(expected[i],0,'g',17)));
}
double variance(const std::vector<double>& v){double mean=std::accumulate(v.begin(),v.end(),0.)/v.size(),sum=0;for(auto x:v)sum+=(x-mean)*(x-mean);return sum/v.size();}
QJsonObject fixture(){QFile f(QFINDTESTDATA("fixtures/synthetic_reference.json"));if(!f.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(f.readAll()).object();}
}
class SyntheticAudit:public QObject {
    Q_OBJECT
private slots:
    void upstreamReference_data(){QTest::addColumn<QJsonObject>("row");const auto f=fixture();QVERIFY(f["original_functions_verified"].toBool());for(auto v:f["cases"].toArray()){auto r=v.toObject();QTest::newRow(qPrintable(r["name"].toString()))<<r;}}
    void upstreamReference(){
        QFETCH(QJsonObject,row);auto m=model(fixture()["model"].toObject());auto p=row["options"].toObject(),l=row["latent"].toObject(),e=row["expected"].toObject();
        synthetic::Options o{p["rate_min"].toDouble(),p["rate_max"].toDouble(),p["snr"].toDouble(),p["rho"].toDouble()};
        synthetic::Latent latent;read(latent.frequency,l["frequency"]);read(latent.unitRates,l["unit_rates"]);read(latent.beta,l["beta"]);latent.whiteNoise=vector(l["white_noise"]);
        auto depth=vector(row["depth"]);
        const auto r=synthetic::evaluate(m,o,depth,latent);QVERIFY(!r.cancelled);
        close(r.ageKa,vector(e["age_ka"]));close(r.rate,vector(e["rate"]));close(r.clean,vector(e["clean"]));close(r.noise,vector(e["noise"]));close(r.proxy,vector(e["proxy"]));
        close(r.angularFrequency,vector(e["angular_frequency"]));close(r.inverseKnots,vector(e["inverse_knots"]));
        QVERIFY(std::abs(r.rawSd-e["raw_sd"].toDouble())<2e-10);QVERIFY(std::abs(r.rawMean-e["raw_mean"].toDouble())<1e-12);QCOMPARE(r.clippedFraction,e["clipped_fraction"].toDouble());
        if(row["compare_draw"].toBool()){
            const auto drawn=synthetic::draw(m,depth.size(),row["seed"].toInt(),row["index"].toInt());
            close(drawn.frequency,latent.frequency,2e-13);close(drawn.unitRates,latent.unitRates,2e-13);close(drawn.beta,latent.beta,2e-13);close(drawn.whiteNoise,latent.whiteNoise,2e-13);
            const auto fromSeed=synthetic::evaluate(m,o,depth,drawn);close(fromSeed.proxy,r.proxy,2e-9);
        }
        QVERIFY(std::abs(variance(r.clean)-1)<1e-12);QVERIFY(std::abs(variance(r.noise)-1/o.snr)<1e-12);QVERIFY(std::abs(variance(r.proxy)-1)<1e-12);
        for(std::size_t i=0;i<depth.size();++i){QVERIFY(r.rate[i]>=o.rateMin-1e-12&&r.rate[i]<=o.rateMax+1e-12);QVERIFY(std::abs(r.proxy[i]-(r.clean[i]+r.noise[i]-r.rawMean)/r.rawSd)<1e-12);}
    }
    void analyticConstantRate(){
        auto m=model(fixture()["model"].toObject());std::vector<double> x(257);for(std::size_t i=0;i<x.size();++i)x[i]=i/8.;
        auto latent=synthetic::draw(m,x.size(),17,0);latent.unitRates.fill(.5);latent.beta.fill(0);latent.beta[0]=1;
        const auto r=synthetic::evaluate(m,{1,5,2,0},x,latent);
        std::vector<double> sine;for(std::size_t i=0;i<x.size();++i){QVERIFY(std::abs(r.ageKa[i]-60*x[i])<1e-9);QVERIFY(std::abs(r.rate[i]-1/.6)<1e-12);sine.push_back(std::sin(r.angularFrequency[0]*(.06*x[i])));}
        const double mean=std::accumulate(sine.begin(),sine.end(),0.)/sine.size(),sd=std::sqrt(variance(sine));for(double& v:sine)v=(v-mean)/sd;close(r.clean,sine);
    }
    void invalidMath(){
        auto m=model(fixture()["model"].toObject());std::vector<double> x(64);std::iota(x.begin(),x.end(),0.);auto l=synthetic::draw(m,x.size(),1,0);synthetic::Options o{1,5,2,.5};
        auto bad=x;bad[2]=bad[1];QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::evaluate(m,o,bad,l));
        bad=x;bad[2]=std::numeric_limits<double>::quiet_NaN();QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::evaluate(m,o,bad,l));
        for(auto opt:{synthetic::Options{5,1,2,.5},synthetic::Options{1,5,0,.5},synthetic::Options{1,5,2,1},synthetic::Options{1,5,2,std::numeric_limits<double>::quiet_NaN()}})QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::evaluate(m,opt,x,l));
        auto wrong=l;wrong.whiteNoise.pop_back();QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::evaluate(m,o,x,wrong));
        wrong=l;wrong.beta.fill(0);QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::evaluate(m,o,x,wrong));
        wrong=l;wrong.whiteNoise[100]=std::numeric_limits<double>::infinity();QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::evaluate(m,o,x,wrong));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,synthetic::draw(m,100001,0,0));
        QVERIFY(synthetic::evaluate(m,o,x,l,[]{return true;}).cancelled);
    }
    void taskDefaultsAndDeterminism(){
        const auto reg=makeScientificRegistry();const auto normalized=reg.normalize("synthetic_orbital",{});QVERIFY(normalized.validation.ok());
        auto d=SyntheticOrbitalTask::descriptor();for(const auto& p:d.parameters)QCOMPARE(normalized.parameters[p.key],p.defaultValue);
        const auto a=reg.create("synthetic_orbital",{})->execute({},{}),b=reg.create("synthetic_orbital",{})->execute({},{});
        QCOMPARE(a.state,NodeState::Succeeded);QCOMPARE(a.outputs.size(),7);QVERIFY(reg.validateOutput(d,a).isEmpty());
        const auto* s=dataAs<SeriesData>(a.outputs["series"]);QVERIFY(s);QCOMPARE(s->x.size(),2679);QCOMPARE(s->x.front(),80.02);QCOMPARE(s->x.back(),135.48);QCOMPARE(s->y,dataAs<SeriesData>(b.outputs["series"])->y);
        const auto r=reg.create("synthetic_orbital",{{"synthetic_realization",1}})->execute({},{});QVERIFY(s->y!=dataAs<SeriesData>(r.outputs["series"])->y);
        const auto* t=dataAs<TableData>(a.outputs["harmonics"]);QVERIFY(t);QCOMPARE(t->rows.size(),10);QCOMPARE(t->columns.size(),4);
        QVERIFY(a.warnings.contains("warning.synthetic_irregular"));QCOMPARE(a.report["synthetic_source"].toObject()["template_sha256"],fixture()["model"].toObject()["template_sha256"]);
    }
    void invalidTaskAndCancellation(){
        for(const auto& p:{QJsonObject{{"synthetic_rate_min",5},{"synthetic_rate_max",1}},QJsonObject{{"synthetic_count",-1}},QJsonObject{{"synthetic_grid","wrong"}},QJsonObject{{"synthetic_rho",1}},QJsonObject{{"synthetic_seed",-1}},QJsonObject{{"synthetic_snr","bad"}}}){
            const auto r=SyntheticOrbitalTask(p).execute({},{});QCOMPARE(r.state,NodeState::Failed);QVERIFY(!r.diagnostic().isEmpty());QVERIFY(r.outputs.isEmpty());}
        ExecutionContext c;c.cancellation.cancel();QCOMPARE(SyntheticOrbitalTask({}).execute({},c).state,NodeState::Cancelled);
        auto m=model(fixture()["model"].toObject());std::vector<double> x(10000);for(std::size_t i=0;i<x.size();++i)x[i]=i*.01;auto l=synthetic::draw(m,x.size(),2,0);int polls=0;
        QVERIFY(synthetic::evaluate(m,{1,5,2,.5},x,l,[&]{return ++polls>3;}).cancelled);
    }
    void concurrentIndependentRuns(){
        auto fn=[] {return SyntheticOrbitalTask({{"synthetic_grid","uniform"},{"synthetic_count",256}}).execute({},{});};
        auto fa=std::async(std::launch::async,fn),fb=std::async(std::launch::async,fn);auto a=fa.get(),b=fb.get();QCOMPARE(a.state,NodeState::Succeeded);QCOMPARE(b.state,NodeState::Succeeded);
        QCOMPARE(dataAs<SeriesData>(a.outputs["series"])->y,dataAs<SeriesData>(b.outputs["series"])->y);
    }
    void workflowAndDatabase(){
        WorkflowModel model(makeScientificRegistry());const auto root=model.addRoot("synthetic_orbital",{0,0},{{"synthetic_grid","uniform"},{"synthetic_count",512}}).nodeId;
        QVERIFY(!root.isNull());const auto child=model.addChild(root,ChildSlot::Down,"mtm",{{"ar1",false},{"swa",false}}).nodeId;
        const auto requests=prepareRuns(model.freeze().snapshot,{});QVERIFY(requests.validation.ok());QCOMPARE(requests.requests.size(),1);
        const auto run=WorkflowExecutor().run(requests.requests.front());QCOMPARE(run.state,RunState::Succeeded);QCOMPARE(run.nodes[child].state,NodeState::Succeeded);
        QTemporaryDir dir;ResultRepository db;QVERIFY(db.open(dir.filePath("roundtrip.sqlite")).isEmpty());const auto saved=db.save(run);QVERIFY2(saved.saved(),qPrintable(saved.error));const auto loaded=db.load(saved.recordId);QVERIFY(loaded.ok());
        QCOMPARE(loaded.run.nodes[root].report,run.nodes[root].report);QCOMPARE(loaded.run.nodes[root].outputs.size(),7);
        for(const auto& key:run.nodes[root].outputs.keys()){
            const auto before=run.nodes[root].outputs[key];const auto after=loaded.run.nodes[root].outputs[key];QVERIFY(validateData(after).isEmpty());
            if(auto* a=dataAs<SeriesData>(before)){const auto* b=dataAs<SeriesData>(after);QVERIFY(b);QCOMPARE(a->x,b->x);QCOMPARE(a->y,b->y);}else QCOMPARE(dataAs<TableData>(before)->rows,dataAs<TableData>(after)->rows);
        }
    }
};
int runSyntheticAudit(int argc,char** argv){SyntheticAudit test;return QTest::qExec(&test,argc,argv);}
#include "synthetic_audit.moc"
