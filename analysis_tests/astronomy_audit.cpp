#include "astronomy_audit.h"
#include "math/astronomy/astronomical_curves.h"
#include "io/astronomy/la2004_reader.h"
#include "processing/scientific_registry.h"
#include "processing/astronomy_source_tasks.h"
#include "application/batch_runner.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <future>
#include <cmath>
using namespace aster;
namespace {
QJsonObject reference()
{
    QFile f(QFINDTESTDATA("fixtures/astronomy_reference.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
}
class AstronomyAudit : public QObject {
    Q_OBJECT
private slots:
    void tableKnotsAndProvenance();
    void corruptionAndAngularInterpolation();
    void independentDailyIntegration();
    void physicalLimitsAndInvalidParameters();
    void acycleEtpExample();
    void etpNormalizationAndWeights();
    void etpConstantAndSmallVariation();
    void sourceContractsAndCancellation();
    void invalidGridsAndSampling();
    void concurrentReaders();
    void workflowPersistence();
};
void AstronomyAudit::tableKnotsAndProvenance()
{
    const auto& table = astronomy::la2004(); QCOMPARE(table.size(),size_t(51001));
    const auto ref = reference(); QVERIFY(!ref.isEmpty());
    for(const auto& item:ref["knots"].toArray()) {
        const auto k=item.toObject(); const auto v=table.at(k["age_ka"].toDouble());
        QCOMPARE(v.eccentricity,k["eccentricity"].toDouble());
        QCOMPARE(v.obliquity,k["obliquity"].toDouble());
        QCOMPARE(v.perihelion,k["perihelion"].toDouble());
    }
    const auto p=astronomy::la2004Provenance();QCOMPARE(p["epoch"].toString(),QString("J2000"));
    QCOMPARE(p["sha256"].toString(),QString("3f13b9f8e69085baf40bc67a2669e6f6af4148fef9218ed2158772aa91e35f8c"));
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,table.at(-1));
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,table.at(51000.1));
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,table.at(qQNaN()));
}
void AstronomyAudit::corruptionAndAngularInterpolation()
{
    for(const auto& invalid:QList<QByteArray>{"", "0 .1 .4 0\n", "0 .1 .4 0\n-2 .1 .4 0\n",
        "0 .1 .4 nan\n-1 .1 .4 0\n", "0 1.1 .4 0\n-1 .1 .4 0\n", "0 .1 .4 0 5\n-1 .1 .4 0\n"})
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,astronomy::La2004Table::parse(invalid));
    auto t=astronomy::La2004Table::parse("0 0.1D0 0.4D0 6.1D0\n-1 0.3D0 0.6D0 0.1D0\n");
    const auto middle=t.at(.5);QVERIFY(std::abs(middle.eccentricity-.2)<1e-15);
    QVERIFY(std::abs(middle.obliquity-.5)<1e-15);
    QVERIFY(std::abs(middle.perihelion-(6.1+(.1+2*astronomy::pi-6.1)/2))<1e-15);
    const auto before=t.at(.9999999),after=t.at(1);
    QVERIFY(std::abs(std::sin(before.perihelion)-std::sin(after.perihelion))<1e-7);
}
void AstronomyAudit::independentDailyIntegration()
{
    const auto ref=reference();QVERIFY(!ref.isEmpty());
    for(const auto& item:ref["insolation"].toArray()) {
        const auto c=item.toObject();
        const double got=astronomy::dailyMeanInsolation(astronomy::la2004().at(c["age_ka"].toDouble()),
            c["latitude_deg"].toDouble()*astronomy::radians,
            c["solar_longitude_deg"].toDouble()*astronomy::radians,c["solar_constant"].toDouble());
        QVERIFY2(std::abs(got-c["expected"].toDouble())<1e-7,qPrintable(QString::number(got,'g',17)));
    }
}
void AstronomyAudit::physicalLimitsAndInvalidParameters()
{
    astronomy::OrbitalElements circle{0,0,0};
    QCOMPARE(astronomy::dailyMeanInsolation(circle,0,0,1365),1365/astronomy::pi);
    const auto o=astronomy::la2004().at(0);
    QCOMPARE(astronomy::dailyMeanInsolation(o,astronomy::pi/2,3*astronomy::pi/2,1365),0.0);
    for(double lat:{-90.,-45.,0.,45.,90.})
        QVERIFY(astronomy::dailyMeanInsolation(o,lat*astronomy::radians,0,1365)>=0);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,astronomy::dailyMeanInsolation(o,2,0,1365));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,astronomy::dailyMeanInsolation(o,0,qQNaN(),1365));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,astronomy::dailyMeanInsolation(o,0,0,-1));
    auto bad=o;bad.eccentricity=1;
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,astronomy::dailyMeanInsolation(bad,0,0,1365));
    const auto a=astronomy::dailyMeanInsolation(o,.8,1.7,1365);
    QVERIFY(std::abs(astronomy::dailyMeanInsolation(o,.8,1.7,2730)-2*a)<1e-12);
    // Global instantaneous spherical average, integrated over latitude, is S0/(4 r²).
    // Midpoint area quadrature in sin(latitude), independent of day-length branches.
    const double lambda=1.7;double globe=0;const int n=100000;
    for(int i=0;i<n;++i) globe+=astronomy::dailyMeanInsolation(o,std::asin(-1+(i+.5)*2/n),lambda,1365)/n;
    const double inv=(1-o.eccentricity*std::cos(lambda-o.perihelion))/(1-o.eccentricity*o.eccentricity);
    QVERIFY(std::abs(globe-1365*inv*inv/4)<1e-5);
}
void AstronomyAudit::acycleEtpExample()
{
    QFile f(QFINDTESTDATA("fixtures/acycle_etp_la2004.csv"));QVERIFY(f.open(QIODevice::ReadOnly));
    auto task=makeScientificRegistry().create("etp_source",{{"etp_t",.5}});QVERIFY(task);
    const auto r=task->execute({},{});QCOMPARE(r.state,NodeState::Succeeded);
    const auto* data=dataAs<SeriesData>(r.outputs["etp"]);QVERIFY(data);
    double maxError=0;int i=0;
    while(!f.atEnd()) {
        const auto parts=f.readLine().trimmed().split(',');if(parts.size()!=2)continue;
        QVERIFY(i<data->x.size());QCOMPARE(data->x[i],parts[0].toDouble());
        maxError=std::max(maxError,std::abs(data->y[i]-parts[1].toDouble()));++i;
    }
    QCOMPARE(i,2001);QCOMPARE(i,data->y.size());QVERIFY(maxError<1e-4);
    qInfo()<<"Acycle bundled ETP example maximum absolute difference:"<<maxError;
}
void AstronomyAudit::etpNormalizationAndWeights()
{
    std::vector<astronomy::OrbitalElements> o;
    for(int i=0;i<100;++i)o.push_back(astronomy::la2004().at(i));
    auto e=astronomy::etp(o,1,0,0),t=astronomy::etp(o,0,1,0),p=astronomy::etp(o,0,0,1),sum=astronomy::etp(o,1,.5,-1);
    double mean=0,sd=0;for(double v:e){mean+=v/e.size();sd+=v*v/(e.size()-1);}
    QVERIFY(std::abs(mean)<1e-14);QVERIFY(std::abs(sd-1)<1e-14);
    for(size_t i=0;i<sum.size();++i)QVERIFY(std::abs(sum[i]-(e[i]+.5*t[i]-p[i]))<1e-14);
    o.assign(10,o.front());QVERIFY_THROWS_EXCEPTION(std::invalid_argument,astronomy::etp(o,1,0,0));
    QCOMPARE(astronomy::etp(o,0,0,0),std::vector<double>(10,0));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,astronomy::etp({},1,1,-1));
}
void AstronomyAudit::etpConstantAndSmallVariation()
{
    for (double eccentricity : {.1, .0167, .3}) {
        for (size_t count : {size_t(3), size_t(10), size_t(101)}) {
            const std::vector<astronomy::OrbitalElements> constant(count, {eccentricity, .4091, 1.2});
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, astronomy::etp(constant, 1, 0, 0));
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, astronomy::etp(constant, 0, 1, 0));
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, astronomy::etp(constant, 0, 0, -1));
            QCOMPARE(astronomy::etp(constant, 0, 0, 0), std::vector<double>(count, 0));
        }
    }
    // Adjacent representable values really do vary: a blanket epsilon guard
    // must not turn this valid series into a constant-component error.
    const double low = .0167, high = std::nextafter(low, 1.0);
    std::vector<astronomy::OrbitalElements> small(10, {low, .4091, 1.2});
    for (size_t i = 1; i < small.size(); i += 2) small[i].eccentricity = high;
    const auto normalized = astronomy::etp(small, 1, 0, 0);
    const double expected = std::sqrt(9.0 / 10.0);
    for (size_t i = 0; i < normalized.size(); ++i)
        QVERIFY(std::abs(normalized[i] - (i % 2 ? expected : -expected)) < 1e-14);
}
void AstronomyAudit::sourceContractsAndCancellation()
{
    auto registry=makeScientificRegistry();
    for(const auto& key:{"orbital_source","etp_source","insolation_source","seasonal_insolation"}) {
        const auto& d=registry.find(key)->descriptor;QVERIFY(!d.inputKind);QVERIFY(!d.requiresSourceBinding);
        auto task=registry.create(key,{});const auto r=task->execute({},{});QCOMPARE(r.state,NodeState::Succeeded);
        QVERIFY(registry.validateOutput(d,r).isEmpty());
        for(const auto& data:r.outputs){const auto* s=dataAs<SeriesData>(data);QVERIFY(s);QCOMPARE(s->x.size(),2001);
            QCOMPARE(s->xAxis.unit,QString("ka"));QVERIFY(validateData(data).isEmpty());}
        ExecutionContext context;context.cancellation.cancel();QCOMPARE(task->execute({},context).state,NodeState::Cancelled);
        QVERIFY(r.report.contains("orbital_solution"));
    }
}
void AstronomyAudit::invalidGridsAndSampling()
{
    auto registry=makeScientificRegistry();
    for(const auto& params:QList<QJsonObject>{{{"age_start_ka",20},{"age_end_ka",10}},
        {{"age_end_ka",0}},{{"age_step_ka",5000}},{{"age_step_ka",.001}}}) {
        auto task=registry.create("orbital_source",params);QVERIFY(task);
        const auto result=task->execute({},{});QCOMPARE(result.state,NodeState::Failed);
        QCOMPARE(result.diagnostic().code,QString("ASTRONOMY_GRID"));QVERIFY(result.outputs.isEmpty());
    }
    QVERIFY(!registry.create("orbital_source",{{"age_end_ka",50001}}));
    auto task=registry.create("orbital_source",{{"age_start_ka",.25},{"age_end_ka",2},{"age_step_ka",.5}});
    auto r=task->execute({},{});QCOMPARE(r.state,NodeState::Succeeded);
    const auto* s=dataAs<SeriesData>(r.outputs["eccentricity"]);QVERIFY(s);QCOMPARE(s->x.size(),4);
    QCOMPARE(s->x.back(),1.75);QVERIFY(!r.warnings.isEmpty());
    QCOMPARE(s->y.front(),astronomy::la2004().at(.25).eccentricity);
    auto decimal=registry.create("orbital_source",{{"age_start_ka",.1},{"age_end_ka",.3},{"age_step_ka",.2}})->execute({},{});
    QCOMPARE(decimal.state,NodeState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(decimal.outputs["eccentricity"])->x.size(),2);
    auto edge=registry.create("orbital_source",{{"age_start_ka",49999.5},{"age_end_ka",50000},{"age_step_ka",.5}})->execute({},{});
    QCOMPARE(edge.state,NodeState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(edge.outputs["eccentricity"])->x.back(),50000.0);
}
void AstronomyAudit::concurrentReaders()
{
    auto evaluate=[] {return makeScientificRegistry().create("insolation_source",{})->execute({},{});};
    auto f=std::async(std::launch::async,evaluate);const auto a=evaluate(),b=f.get();
    QCOMPARE(a.state,NodeState::Succeeded);QCOMPARE(b.state,NodeState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(a.outputs["insolation"])->y,dataAs<SeriesData>(b.outputs["insolation"])->y);
}
void AstronomyAudit::workflowPersistence()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root=model.addRoot("orbital_source",{0,0},{{"age_end_ka",100}}).nodeId;
    QVERIFY(!root.isNull());
    const auto child=model.addChild(root,ChildSlot::Down,"detrend_lowess",{},"precession").nodeId;
    QVERIFY(!child.isNull());
    const auto mtm=model.addChild(child,ChildSlot::Down,"mtm",{{"ar1",false}}).nodeId;QVERIFY(!mtm.isNull());
    const auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"La2004",{},{}}});
    QCOMPARE(run.state,RunState::Succeeded);QCOMPARE(run.executionOrder.size(),3);
    QVERIFY(run.nodes[mtm].outputs.contains("spectrum"));
    QTemporaryDir dir;ResultRepository repo;QVERIFY(repo.open(dir.filePath("astronomy.sqlite")).isEmpty());
    auto saved=repo.save(run);QVERIFY(saved.saved());const auto loaded=repo.load(saved.recordId);QVERIFY(loaded.error.isEmpty());
    QCOMPARE(loaded.run.nodes[root].report,run.nodes[root].report);
    QCOMPARE(dataAs<SeriesData>(loaded.run.nodes[root].outputs["precession"])->y,
             dataAs<SeriesData>(run.nodes[root].outputs["precession"])->y);
}
int runAstronomyAudit(int argc,char** argv){AstronomyAudit test;return QTest::qExec(&test,argc,argv);}
#include "astronomy_audit.moc"
