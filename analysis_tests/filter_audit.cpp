#include "filter_audit.h"
#include "math/filtering/bandpass.h"
#include "processing/scientific_registry.h"
#include "core/workflow_model.h"
#include "core/workflow_executor.h"
#include "io/database/result_repository.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <cmath>
#include <future>
using namespace aster;
using namespace aster::filtering;
namespace {
constexpr double pi=3.14159265358979323846;
SeriesData signal(int n=256) {
    SeriesData d;d.xAxis={"Age","ka"};d.yAxis={"Proxy","u"};
    for(int i=0;i<n;++i){d.x<<i;d.y<<2+(1+.25*std::cos(2*pi*2*i/n))*std::cos(2*pi*32*i/n);}
    return d;
}
QVector<double> array(const QJsonValue& v){QVector<double> out;for(auto x:v.toArray())out<<x.toDouble();return out;}
}
class FilterAudit:public QObject {
    Q_OBJECT
private slots:
    void independentDftReference_data();
    void independentDftReference();
    void gainAndPhaseInvariants();
    void hilbertEvenOddDcNyquist();
    void constantOverflowAndCancellation();
    void invalidInputsAndBudgets();
    void workflowPersistenceAndUnits();
    void insolationMapMatchesEveryLatitude();
    void mapValidationAndPersistence();
    void concurrentAndLarge();
};
void FilterAudit::independentDftReference_data(){
    QTest::addColumn<QJsonObject>("reference");
    QFile f(QFINDTESTDATA("fixtures/filter_reference.json"));QVERIFY(f.open(QIODevice::ReadOnly));
    const auto root=QJsonDocument::fromJson(f.readAll()).object();QCOMPARE(root["cases"].toArray().size(),12);
    for(const auto v:root["cases"].toArray()) {
        const auto c=v.toObject();const auto name=QString("%1-N%2-q%3").arg(c["method"].toString()).arg(c["n"].toInt()).arg(c["q"].toInt());
        QTest::newRow(qPrintable(name))<<c;
    }
}
void FilterAudit::independentDftReference(){
    QFETCH(QJsonObject,reference);const auto c=reference;const auto y=array(c["input"]);
    const auto f=bandpass({y.begin(),y.end()},c["spacing"].toDouble(),c["method"].toString()=="gaussian"?Shape::Gaussian:Shape::Taner,
                           c["low"].toDouble(),c["high"].toDouble(),c["q"].toDouble());
    QCOMPARE(f.fftLength,std::size_t(c["nfft"].toInt()));
    const auto a=hilbert(f.filtered);const auto expected=array(c["filtered"]),env=array(c["envelope"]),quad=array(c["quadrature"]);
    double maxError=0;
    for(int i=0;i<expected.size();++i) {
        maxError=std::max(maxError,std::abs(f.filtered[i]-expected[i]));
        QVERIFY(std::abs(f.filtered[i]-expected[i])<5e-11);
        QVERIFY(std::abs(a.envelope[i]-env[i])<5e-11);
        QVERIFY(std::abs(a.quadrature[i]-quad[i])<5e-11);
    }
    qInfo()<<"Maximum filter / direct-DFT error:"<<maxError;
}
void FilterAudit::gainAndPhaseInvariants(){
    for(auto shape:{Shape::Gaussian,Shape::Taner}) {
        QCOMPARE(responseGain(shape,.125,.0625,.1875),1.);
        QVERIFY(std::abs(responseGain(shape,.0625,.0625,.1875)-std::sqrt(.5))<1e-15);
        QVERIFY(std::abs(responseGain(shape,.1875,.0625,.1875)-std::sqrt(.5))<1e-15);
    }
    QVERIFY(std::abs(responseGain(Shape::Taner,.25,.0625,.1875)/(1e-12/std::sqrt(2.))-1)<1e-13);
    for(double f:{.0625,.125,.1875,.25}) {
        std::vector<double> y(256);for(int i=0;i<256;++i)y[i]=std::cos(2*pi*f*i+.3);
        auto r=bandpass(y,1,Shape::Taner,.0625,.1875);double gain=responseGain(Shape::Taner,f,.0625,.1875);
        for(int i=0;i<256;++i)QVERIFY(std::abs(r.filtered[i]-gain*y[i])<3e-14);
    }
}
void FilterAudit::hilbertEvenOddDcNyquist(){
    const auto example=hilbert({1,2,3,4});const std::vector<double> q{1,-1,-1,1};
    for(int i=0;i<4;++i)QVERIFY(std::abs(example.quadrature[i]-q[i])<1e-14);
    for(int n:{127,128,257}) {
        std::vector<double> y(n);
        for(int i=0;i<n;++i)y[i]=(2+.4*std::cos(2*pi*3*i/n))*std::cos(2*pi*24*i/n);
        const auto a=hilbert(y);
        for(int i=0;i<n;++i) {
            QVERIFY(std::abs(a.envelope[i]-(2+.4*std::cos(2*pi*3*i/n)))<1e-12);
            QVERIFY(std::abs(a.quadrature[i]-(2+.4*std::cos(2*pi*3*i/n))*std::sin(2*pi*24*i/n))<1e-12);
        }
    }
    std::vector<double> nyquist(128);for(int i=0;i<128;++i)nyquist[i]=(i%2?-2:2);
    const auto a=hilbert(nyquist);for(int i=0;i<128;++i){QCOMPARE(a.envelope[i],2.);QCOMPARE(a.quadrature[i],0.);}
    auto z=hilbert(std::vector<double>(17,3));for(double e:z.envelope)QVERIFY(std::abs(e-3)<1e-14);
    z=hilbert(std::vector<double>(17,3),true);for(double e:z.envelope)QCOMPARE(e,0.);
}
void FilterAudit::constantOverflowAndCancellation(){
    for(auto shape:{Shape::Gaussian,Shape::Taner}){
        const auto r=bandpass(std::vector<double>(16,5),1,shape,.1,.3);
        for(double y:r.filtered) { QCOMPARE(y,0.); }
        QCOMPARE(r.removedMean,5.);
    }
    const auto zero=hilbert(std::vector<double>(33,0));for(double v:zero.envelope)QCOMPARE(v,0.);
    const auto huge=hilbert(std::vector<double>(16,1e308));for(double v:huge.envelope)QVERIFY(std::isfinite(v));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,hilbert({1,qQNaN()}));
    QVERIFY_THROWS_EXCEPTION(Cancelled,hilbert({1,2},false,[]{return true;}));
    int polls=0;QVERIFY_THROWS_EXCEPTION(Cancelled,bandpass(std::vector<double>(20000,1),1,Shape::Gaussian,.1,.3,12,[&]{return ++polls>3;}));
    QVERIFY(polls>3);
}
void FilterAudit::invalidInputsAndBudgets(){
    const auto registry=makeScientificRegistry();
    for(const auto& method:{"gaussian_filter","taner_filter","hilbert_envelope"}) {
        auto d=signal();d.x[5]=d.x[4];auto task=registry.create(method,{});
        QCOMPARE(task->execute({makeData(d)},{}).diagnostic().code,QString("INPUT_X_DUPLICATE"));
        d=signal();d.x[5]+=.1;QCOMPARE(task->execute({makeData(d)},{}).diagnostic().code,QString("INPUT_X_NONUNIFORM"));
        d=signal();d.y.pop_back();QCOMPARE(task->execute({makeData(d)},{}).diagnostic().code,QString("INPUT_INVALID"));
        QCOMPARE(task->execute({makeData(GridData{})},{}).diagnostic().code,QString("INPUT_TYPE"));
        CancellationToken cancel;cancel.cancel();ExecutionContext c;c.cancellation=cancel;
        QCOMPARE(task->execute({makeData(signal())},c).state,NodeState::Cancelled);
    }
    for(QJsonObject p:{QJsonObject{{"lower_frequency",.2},{"upper_frequency",.1}},QJsonObject{{"upper_frequency",.5}},QJsonObject{{"lower_frequency",0}}})
        QCOMPARE(registry.create("gaussian_filter",p)->execute({makeData(signal())},{}).diagnostic().code,QString("FILTER_BAND"));
    QVERIFY_THROWS_EXCEPTION(std::length_error,fftLength(Shape::Gaussian,262145));
    QCOMPARE(fftLength(Shape::Gaussian,262144),std::size_t(2621440));
    QVERIFY_THROWS_EXCEPTION(std::length_error,fftLength(Shape::Taner,1000001));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,responseGain(Shape::Taner,.1,.05,.2,0));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,bandpass({1,2},1,Shape::Gaussian,.1,.5));
}
void FilterAudit::workflowPersistenceAndUnits(){
    WorkflowModel model(makeScientificRegistry());const auto root=model.addRoot("input_series",{0,0}).nodeId;
    const auto filter=model.addChild(root,ChildSlot::Down,"taner_filter",{{"lower_frequency",.08},{"upper_frequency",.18}}).nodeId;
    const auto child=model.addChild(filter,ChildSlot::Down,"hilbert_envelope").nodeId;
    const auto raw=makeData(signal());const auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"AM",raw,{}}});
    QCOMPARE(run.state,RunState::Succeeded);const auto f=run.nodes[filter],h=run.nodes[child];
    QCOMPARE(dataAs<SeriesData>(f.outputs["envelope"])->y,dataAs<SeriesData>(h.outputs["envelope"])->y);
    const auto* response=dataAs<SeriesData>(f.outputs["filter_response"]);QVERIFY(response);QCOMPARE(response->xAxis.unit,QString("1/ka"));QCOMPARE(response->yAxis.unit,QString("1"));
    QCOMPARE(dataAs<SeriesData>(f.outputs["filtered"])->x,dataAs<SeriesData>(raw)->x);
    QCOMPARE(dataAs<SeriesData>(f.outputs["envelope"])->yAxis.unit,QString("u"));
    QTemporaryDir dir;ResultRepository repo;QVERIFY(repo.open(dir.filePath("filters.sqlite")).isEmpty());auto saved=repo.save(run);QVERIFY2(saved.saved(),qPrintable(saved.error));
    auto loaded=repo.load(saved.recordId);QVERIFY(loaded.ok());
    for(auto it=f.outputs.begin();it!=f.outputs.end();++it) {
        const auto path=dir.filePath(it.key()+".csv");QVERIFY(exportDataCsv(it.value(),path).ok());
        QVERIFY(QFileInfo(path).size()>100);
    }
    QCOMPARE(loaded.run.nodes[filter].report,f.report);QCOMPARE(loaded.run.nodes[filter].warnings,f.warnings);
    QCOMPARE(dataAs<SeriesData>(loaded.run.nodes[filter].outputs["filtered"])->y,dataAs<SeriesData>(f.outputs["filtered"])->y);
}
void FilterAudit::insolationMapMatchesEveryLatitude(){
    auto registry=makeScientificRegistry();
    for(const auto& basis:{"solar_longitude","calendar_365"}){
        QJsonObject p{{"age_end_ka",8},{"age_step_ka",.5},{"latitude_start_deg",-90},{"latitude_end_deg",90},{"latitude_step_deg",30},{"season_basis",basis}};
        auto result=registry.create("insolation_map",p)->execute({},{});QCOMPARE(result.state,NodeState::Succeeded);
        const auto* g=dataAs<GridData>(result.outputs["insolation_map"]);QVERIFY(g);QCOMPARE(g->rows,7);QCOMPARE(g->columns,17);QVERIFY(validateData(result.outputs["insolation_map"]).isEmpty());
        p.remove("latitude_start_deg");p.remove("latitude_end_deg");p.remove("latitude_step_deg");
        for(qsizetype row=0;row<g->rows;++row){
            p["latitude_deg"]=g->y[row];auto one=registry.create("insolation_source",p)->execute({},{});
            QCOMPARE(one.state,NodeState::Succeeded);const auto* s=dataAs<SeriesData>(one.outputs["insolation"]);QCOMPARE(g->x,s->x);
            for(qsizetype col=0;col<g->columns;++col)QCOMPARE(g->values[row*g->columns+col],s->y[col]);
        }
        // At the northern summer solstice, the south pole is in polar night.
        if(QString(basis)=="solar_longitude")for(int col=0;col<g->columns;++col)QCOMPARE(g->values[col],0.);
    }
}
void FilterAudit::mapValidationAndPersistence(){
    auto r=makeScientificRegistry();
    QCOMPARE(r.create("insolation_map",{{"latitude_start_deg",60},{"latitude_end_deg",-60}})->execute({},{}).diagnostic().code,QString("INSOLATION_MAP_GRID"));
    QCOMPARE(r.create("insolation_map",{{"age_end_ka",50000}})->execute({},{}).diagnostic().code,QString("INSOLATION_MAP_BUDGET"));
    QCOMPARE(r.create("insolation_map",{{"season_basis","calendar_365"},{"month_start",2},{"day_start",30}})->execute({},{}).diagnostic().code,QString("ASTRONOMY_PARAMETERS"));
    ExecutionContext c;c.progress=[&](int p,const QString&){if(p>0)c.cancellation.cancel();};
    QCOMPARE(r.create("insolation_map",{})->execute({},c).state,NodeState::Cancelled);
    WorkflowModel m(r);auto root=m.addRoot("insolation_map",{0,0},{{"age_end_ka",5}}).nodeId;
    QVERIFY(!m.addChild(root,ChildSlot::Down,"taner_filter").ok());
    auto run=WorkflowExecutor().run({m.freeze().snapshot,root,{}});QCOMPARE(run.state,RunState::Succeeded);
    QTemporaryDir dir;ResultRepository repo;QVERIFY(repo.open(dir.filePath("map.sqlite")).isEmpty());auto saved=repo.save(run);QVERIFY(saved.saved());auto loaded=repo.load(saved.recordId);QVERIFY(loaded.ok());
    const auto* grid=dataAs<GridData>(loaded.run.nodes[root].outputs["insolation_map"]);QVERIFY(grid);
    QCOMPARE(grid->values,dataAs<GridData>(run.nodes[root].outputs["insolation_map"])->values);
}
void FilterAudit::concurrentAndLarge(){
    std::vector<double> y(65536);for(std::size_t i=0;i<y.size();++i)y[i]=std::sin(2*pi*.125*i);
    QElapsedTimer timer;timer.start();
    auto future=std::async(std::launch::async,[&]{return bandpass(y,1,Shape::Gaussian,.08,.18);});
    const auto expected=bandpass(y,1,Shape::Gaussian,.08,.18);const auto other=future.get();
    QCOMPARE(expected.filtered,other.filtered);QCOMPARE(expected.filtered.size(),y.size());
    qInfo()<<"Two concurrent 65536-point Gaussian calls, ms:"<<timer.elapsed();
}
int runFilterAudit(int argc,char** argv){FilterAudit t;return QTest::qExec(&t,argc,argv);}
#include "filter_audit.moc"
