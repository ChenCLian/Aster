#include "mtm_simulation_audit.h"
#include "math/statistics/mtm_simulation.h"
#include "math/statistics/monte_carlo_test.h"
#include "math/statistics/ar1_nuisance.h"
#include "math/statistics/significance.h"
#include "processing/scientific_registry.h"
#include "application/batch_runner.h"
#include "io/database/data_codec.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <future>
#include <cmath>
#include <limits>
using namespace aster;
namespace {
std::vector<double> vector(const QJsonArray& array) {
    std::vector<double> values;
    for (const auto& value : array) values.push_back(value.toDouble());
    return values;
}
void close(double actual, double expected, double tolerance = 2e-6) {
    QVERIFY2(std::abs(actual-expected) <= tolerance*(1+std::abs(expected)),
        qPrintable(QString("actual %1 expected %2").arg(actual,0,'g',17).arg(expected,0,'g',17)));
}
void close(const std::vector<double>& actual, const QJsonArray& expected, double tolerance = 2e-6) {
    QCOMPARE(actual.size(),size_t(expected.size()));
    for (size_t i=0; i<actual.size(); ++i) close(actual[i],expected[int(i)].toDouble(),tolerance);
}
DataRef signal() {
    SeriesData series;
    for (int i=0;i<64;++i) { series.x.push_back(i);series.y.push_back(std::sin(.39*i)+.2*std::cos(2.3*i)); }
    series.xAxis={"Age","ka"};series.yAxis={"Value","u"};return makeData(series);
}
}
class MtmSimulationAudit : public QObject {
    Q_OBJECT
private slots:
    void independentReference_data();
    void independentReference();
    void locationScaleAndUnits();
    void invalidInput_data();
    void invalidInput();
    void deterministicIndependentRuns();
    void cancellationDuringSimulation();
    void workflowDatabaseAndFailureIsolation();
    void empiricalRankExhaustive();
    void empiricalFamilyAndTies();
    void empiricalReplayFailures();
    void empiricalResolutionBoundary();
    void ar1DistanceIndependentGaussianReference();
    void ar1GridCoverageAndBudget();
    void nuisanceCombinationAndEmptySet();
    void nuisanceReferenceRanksAndInvalid();
    void nuisanceLagInvariance();
    void nuisanceFiniteOrbitControl();
    void reusableTapersParityAndIsolation();
    void ar1BackgroundFormulaAndInvalid();
};
void MtmSimulationAudit::ar1BackgroundFormulaAndInvalid() {
    numeric::MtmSpectrum spectrum;spectrum.frequency={0,.25,.5};spectrum.densityScale={1,2,1};
    const numeric::Ar1LikelihoodFit fit{.5,8,2};
    const auto background=numeric::stationaryAr1Background(spectrum,fit,1);
    close(background[0],8,1e-15);close(background[1],3.2,1e-15);close(background[2],2./2.25,1e-15);
    QVERIFY_EXCEPTION_THROWN(numeric::stationaryAr1Background(spectrum,fit,0),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::stationaryAr1Background(spectrum,{1,0,1},1),std::invalid_argument);
    spectrum.densityScale.clear();
    QVERIFY_EXCEPTION_THROWN(numeric::stationaryAr1Background(spectrum,fit,1),std::invalid_argument);
}
void MtmSimulationAudit::reusableTapersParityAndIsolation() {
    const auto plan=numeric::makeMtmTaperPlan(128,2,3);
    std::vector<double> y(128);for(size_t i=0;i<y.size();++i)y[i]=std::sin(i*.23)+.2*std::cos(i*.7);
    auto compute=[&](const std::vector<double>& v, bool cache){return numeric::mtmSpectrum(v,1,2,3,.5,{},128,
        numeric::MtmConvergenceDomain::FullSpectrum,cache?plan:nullptr);};
    const auto normal=compute(y,false),cached=compute(y,true);
    QVERIFY(normal.power==cached.power);QVERIFY(normal.degreesOfFreedom==cached.degreesOfFreedom);
    QCOMPARE(normal.iterations,cached.iterations);
    SpectrumInference inference;inference.acycle=true;
    QVERIFY(estimateSwaBackground(normal.power,normal.frequency,inference)==FDR_TEST(normal.power,normal.frequency,2,inference).swa);
    auto work=std::async(std::launch::async,[&]{return compute(y,true);});
    auto second=y;for(auto& v:second)v=3*v+2;
    QVERIFY(compute(second,false).power==compute(second,true).power);
    QVERIFY(work.get().power==normal.power);
    QVERIFY_EXCEPTION_THROWN(numeric::mtmSpectrum(y,1,3,3,.5,{},128,numeric::MtmConvergenceDomain::FullSpectrum,plan),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::makeMtmTaperPlan(128,2,3,[]{return true;}),std::runtime_error);
}
void MtmSimulationAudit::ar1DistanceIndependentGaussianReference() {
    // Independent NumPy dense covariance trace/inverse/logdet computation.
    struct Row{size_t n;double actual,reference,tv;};
    for(const auto& r:std::vector<Row>{{4,.1,.15,.044485536467637296},
        {12,.9,.91,.1259045687187957},{16,-.8,-.799,.006866103780633923},
        {32,.995,.9949,.03885340356312077},{5,-.3,.3,.6289709020331509}})
        close(numeric::ar1TotalVariationBound(r.n,r.actual,r.reference),r.tv,1e-10);
    QCOMPARE(numeric::ar1TotalVariationBound(512,.995,.995),0.);
    QVERIFY(numeric::ar1TotalVariationBound(512,.995000000001,.995)>0);
    QVERIFY_EXCEPTION_THROWN(numeric::ar1TotalVariationBound(1,.5,.4),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::ar1TotalVariationBound(128,1,.5),std::invalid_argument);
}
void MtmSimulationAudit::ar1GridCoverageAndBudget() {
    const auto cells=numeric::ar1NuisanceGrid(128,-.999,.999,.02);
    QCOMPARE(cells.front().lower,-.999);QCOMPARE(cells.back().upper,.999);
    for(size_t j=0;j<cells.size();++j){const auto& c=cells[j];
        QVERIFY(c.lower<c.center&&c.center<c.upper);QVERIFY(c.variationBound<=.02);
        if(j)QCOMPARE(cells[j-1].upper,c.lower);
        for(int i=0;i<=10;++i){const double r=c.lower+(c.upper-c.lower)*i/10;
            QVERIFY(numeric::ar1TotalVariationBound(128,r,c.center)<=c.variationBound+1e-12);}
    }
    QVERIFY_EXCEPTION_THROWN(numeric::ar1NuisanceGrid(128,-.999,.999,.001,2),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::ar1NuisanceGrid(128,.5,.4,.01),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::ar1NuisanceGrid(128,-1,.4,.01),std::invalid_argument);
}
void MtmSimulationAudit::nuisanceCombinationAndEmptySet() {
    const auto r=numeric::combineNuisancePValues({{.001,.9},{.1,.01},{.2,.03}},.005,.01);
    close(r.p,.045,1e-15);QCOMPARE(r.includedModels.size(),size_t(2));
    // Exact screening boundary is excluded; no selective choice of analysis model.
    const auto e=numeric::combineNuisancePValues({{.005,0},{0,0}},.005,.01);
    QVERIFY(e.emptyConfidenceSet);QCOMPARE(e.p,1.);
    QCOMPARE(numeric::combineNuisancePValues({{1,.99}},.005,.01).p,1.);
    QVERIFY_EXCEPTION_THROWN(numeric::combineNuisancePValues({},.005,.01),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::combineNuisancePValues({{1,NAN}},.005,.01),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::combineNuisancePValues({{1,.1}},.5,.5),std::invalid_argument);
}
void MtmSimulationAudit::nuisanceReferenceRanksAndInvalid() {
    const std::vector<double> ref{-3,-1,0,0,1,3};numeric::EmpiricalTailReference cached(ref);
    for(double x:{-4.,-3.,-2.,-1.,0.,.5,1.,2.,3.,4.}){
        QCOMPARE(cached.upperTail(x),numeric::empiricalUpperTailP(ref,x));
        QCOMPARE(cached.twoSided(x),numeric::empiricalTwoSidedP(ref,x));}
    QCOMPARE(cached.twoSided(-4.),2./7);QCOMPARE(cached.twoSided(4.),2./7);
    QCOMPARE(cached.twoSided(0.),1.);
    QVERIFY_EXCEPTION_THROWN(numeric::EmpiricalTailReference({1,0}),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::EmpiricalTailReference({NAN}),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(cached.upperTail(INFINITY),std::invalid_argument);
}
void MtmSimulationAudit::nuisanceLagInvariance() {
    std::vector<double> y{1,3,-2,1,4,6,-3};const double r=numeric::centeredLagOne(y);
    for(auto& x:y)x=20-4*x;
    close(numeric::centeredLagOne(y),r,1e-15);
    QVERIFY_EXCEPTION_THROWN(numeric::centeredLagOne({1,1,1,1}),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::centeredLagOne({1,2,NAN,4}),std::invalid_argument);
}
void MtmSimulationAudit::nuisanceFiniteOrbitControl() {
    // Exhaustive exchangeable orbit: screening and test ranks are deliberately
    // dependent. Other candidate p-values cannot make the true-model maximum smaller.
    int rejected=0;const double beta=.1,alpha=.25;
    for(int held=0;held<20;++held){std::vector<double> ref;
        for(int j=0;j<20;++j)if(j!=held)ref.push_back(j);
        const double screen=numeric::empiricalTwoSidedP(ref,held);
        const double analysis=numeric::empiricalUpperTailP(ref,held);
        const auto r=numeric::combineNuisancePValues({{screen,analysis},{1,0}},beta,0);
        rejected+=r.p<=alpha;
    }
    QVERIFY(rejected<=5);
}
void MtmSimulationAudit::empiricalRankExhaustive() {
    // Exhaust every held-out observation of an exchangeable six-element orbit.
    // For B=5 and alpha=1/3 exactly two ranks reject, independent of their spacing.
    const std::vector<double> orbit{-8,-3,.25,2,19,100};int rejected=0;
    for(size_t held=0;held<orbit.size();++held) {
        std::vector<double> reference;
        for(size_t i=0;i<orbit.size();++i)if(i!=held)reference.push_back(orbit[i]);
        const auto result=numeric::monteCarloMaximumTest({orbit[held]},5,1./3,
            [&](size_t j){return std::vector<double>{reference[j]};});
        QCOMPARE(result.globalP,(6.-held)/6.);
        QCOMPARE(result.adjustedP.front(),result.globalP);
        rejected+=result.globalP<=1./3;
    }
    QCOMPARE(rejected,2);
}
void MtmSimulationAudit::empiricalFamilyAndTies() {
    const auto result=numeric::monteCarloMaximumTest({3,7},4,.4,[](size_t j){
        const std::vector<std::vector<double>> rows{{2,1},{5,4},{1,6},{8,7}};return rows[j];});
    QCOMPARE(result.nullMaxima,std::vector<double>({2,5,6,8}));
    QCOMPARE(result.globalP,.4);QCOMPARE(result.adjustedP[0],.8);QCOMPARE(result.adjustedP[1],.4);
    QCOMPARE(result.criticalValue,6.);
    QCOMPARE(numeric::empiricalUpperTailP({2,5,6,8},6),.6);
    QCOMPARE(numeric::empiricalUpperTailP({1,1,1,1},1),1.);
    QCOMPARE(numeric::empiricalUpperTailP({1,1,1,1},std::nextafter(1.,2.)),1.);
    QCOMPARE(numeric::empiricalUpperTailP({1,1,1,1},2),.2);
    QVERIFY_EXCEPTION_THROWN(numeric::empiricalUpperTailP({2,1},1),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(numeric::empiricalUpperTailP({},1),std::invalid_argument);
}
void MtmSimulationAudit::empiricalReplayFailures() {
    int calls=0;
    QVERIFY_EXCEPTION_THROWN(numeric::monteCarloMaximumTest({1,2},4,.2,[&](size_t j){
        ++calls;return j==1?std::vector<double>{1}:std::vector<double>{1,2};}),std::runtime_error);
    QCOMPARE(calls,2); // Not discarded or retried.
    QVERIFY_EXCEPTION_THROWN(numeric::monteCarloMaximumTest({1},4,.2,[](size_t){
        return std::vector<double>{std::numeric_limits<double>::quiet_NaN()};}),std::invalid_argument);
    calls=0;
    QVERIFY_EXCEPTION_THROWN(numeric::monteCarloMaximumTest({1},4,.2,[&](size_t){
        ++calls;return std::vector<double>{1};},[&]{return calls==2;}),std::runtime_error);
    QCOMPARE(calls,2);
}
void MtmSimulationAudit::empiricalResolutionBoundary() {
    const auto generate=[](size_t j){return std::vector<double>{double(j)};};
    QVERIFY_EXCEPTION_THROWN(numeric::monteCarloMaximumTest({1},19,std::nextafter(.05,0.),generate),std::invalid_argument);
    const auto result=numeric::monteCarloMaximumTest({100},19,.05,generate);
    QCOMPARE(result.globalP,.05);QCOMPARE(result.criticalValue,18.);
    for(int k=1;k<=19;++k) {
        const double alpha=double(k)/20;
        const auto exact=numeric::monteCarloMaximumTest({100},19,alpha,generate);
        QCOMPARE(exact.criticalValue,double(19-k));
    }
}
void MtmSimulationAudit::independentReference_data() {
    QTest::addColumn<QJsonObject>("row");
    QFile file(QFINDTESTDATA("fixtures/mtm_simulation_reference.json"));QVERIFY(file.open(QIODevice::ReadOnly));
    const auto rows=QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray();QCOMPARE(rows.size(),5);
    for(int i=0;i<rows.size();++i) QTest::newRow(qPrintable(QString::number(i))) << rows[i].toObject();
}
void MtmSimulationAudit::independentReference() {
    QFETCH(QJsonObject,row);
    numeric::MtmSimulationOptions o;o.nw=row["nw"].toInt();o.simulations=row["replicates"].toInt();
    o.fmaxRatio=row["fmax"].toDouble();o.estimateRho=row["estimate"].toBool();o.fixedRho=row["fixed_rho"].toDouble();o.seed=row["seed"].toInt();
    const auto r=numeric::simulateMtmAr1(vector(row["y"].toArray()),row["fs"].toDouble(),o);
    close(r.backgroundFit.rho,row["rho"].toDouble());close(r.backgroundFit.mean,row["mean"].toDouble());
    close(r.backgroundFit.innovationVariance,row["innovation_variance"].toDouble());
    close(r.spectrum.frequency,row["frequency"].toArray(),1e-12);close(r.spectrum.power,row["power"].toArray(),1e-8);
    close(r.background,row["background"].toArray());close(r.nullMaxima,row["null_maxima"].toArray(),2e-5);
    close(r.maximumRatio,row["maximum_ratio"].toDouble());close(r.criticalRatio,row["critical_ratio"].toDouble(),2e-5);
    close(r.adjustedP,row["adjusted_p"].toArray(),1e-12);QCOMPARE(r.globalP,row["global_p"].toDouble());
    QVERIFY(!r.testedBins.empty());QVERIFY(r.testedBins.front()>0);
    for(auto i:r.testedBins) QCOMPARE(r.spectrum.densityScale[i],2.0);
}
void MtmSimulationAudit::locationScaleAndUnits() {
    const auto data=signal();const auto s=dataAs<SeriesData>(data);auto y=std::vector<double>(s->y.begin(),s->y.end());
    numeric::MtmSimulationOptions o;o.simulations=199;
    const auto a=numeric::simulateMtmAr1(y,1,o);
    for(auto& v:y)v=3*v+1000;
    const auto b=numeric::simulateMtmAr1(y,2,o);
    close(a.backgroundFit.rho,b.backgroundFit.rho,1e-7);QCOMPARE(a.globalP,b.globalP);
    close(b.backgroundFit.innovationVariance,9*a.backgroundFit.innovationVariance,1e-7);
    for(size_t i=0;i<a.spectrum.power.size();++i) {
        close(b.spectrum.frequency[i],2*a.spectrum.frequency[i],1e-12);
        close(b.spectrum.power[i],4.5*a.spectrum.power[i],1e-9);
    }
}
void MtmSimulationAudit::invalidInput_data() {
    QTest::addColumn<QString>("condition");
    for(const char* s:{"constant","nan","short","budget","rho","alpha_resolution","empty_family","nw","sampling"})
        QTest::newRow(s)<<QString::fromLatin1(s);
}
void MtmSimulationAudit::invalidInput() {
    QFETCH(QString,condition);
    const auto data=signal();const auto s=dataAs<SeriesData>(data);auto y=std::vector<double>(s->y.begin(),s->y.end());
    numeric::MtmSimulationOptions o;o.simulations=199;double fs=1;
    if(condition=="constant")std::fill(y.begin(),y.end(),1);
    if(condition=="nan")y[3]=std::numeric_limits<double>::quiet_NaN();
    if(condition=="short")y.resize(16);
    if(condition=="budget")o.simulations=198;
    if(condition=="rho"){o.estimateRho=false;o.fixedRho=1;}
    if(condition=="alpha_resolution")o.alpha=.001;
    if(condition=="empty_family")o.fmaxRatio=.001;
    if(condition=="nw")o.nw=32;
    if(condition=="sampling")fs=0;
    QVERIFY_EXCEPTION_THROWN(numeric::simulateMtmAr1(y,fs,o),std::exception);
}
void MtmSimulationAudit::deterministicIndependentRuns() {
    auto data=signal();const auto s=dataAs<SeriesData>(data);auto y=std::vector<double>(s->y.begin(),s->y.end());
    numeric::MtmSimulationOptions o;o.simulations=199;
    auto calculate=[&]{return numeric::simulateMtmAr1(y,1,o);};
    auto future=std::async(std::launch::async,calculate);const auto a=calculate(),b=future.get();
    QVERIFY(a.nullMaxima==b.nullMaxima);QCOMPARE(a.globalP,b.globalP);
    ++o.seed;const auto c=calculate();QVERIFY(a.nullMaxima!=c.nullMaxima);
}
void MtmSimulationAudit::cancellationDuringSimulation() {
    auto data=signal();const auto s=dataAs<SeriesData>(data);auto y=std::vector<double>(s->y.begin(),s->y.end());
    numeric::MtmSimulationOptions o;o.simulations=999;int calls=0;
    QVERIFY_EXCEPTION_THROWN(numeric::simulateMtmAr1(y,1,o,[&]{return ++calls>80;}),std::runtime_error);
    QVERIFY(calls>80);
}
void MtmSimulationAudit::workflowDatabaseAndFailureIsolation() {
    const auto registry=makeScientificRegistry();QVERIFY(registry.find("mtm_ar1_simulation"));
    WorkflowModel model(registry);const auto root=model.addRoot("input_series",{0,0}).nodeId;
    const auto good=model.addChild(root,ChildSlot::Left,"mtm_ar1_simulation",{{"mc_simulations",199}}).nodeId;
    const auto bad=model.addChild(root,ChildSlot::Right,"mtm_ar1_simulation",{{"mc_simulations",199},{"mc_alpha",.001}}).nodeId;
    const auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"simulation",signal(),{}}});
    QCOMPARE(run.nodes[good].state,NodeState::Succeeded);QCOMPARE(run.nodes[bad].state,NodeState::Failed);
    QCOMPARE(run.nodes[bad].diagnostic().code,QString("MTM_SIMULATION_FAILED"));QVERIFY(run.nodes[bad].outputs.isEmpty());
    QCOMPARE(run.nodes[good].outputs.size(),6);QVERIFY(!run.nodes[good].warnings.isEmpty());
    QVERIFY(registry.validateOutput(registry.find("mtm_ar1_simulation")->descriptor,run.nodes[good]).isEmpty());
    const auto* main=dataAs<SpectrumData>(run.nodes[good].outputs["spectrum"]);QVERIFY(main);QCOMPARE(main->powerAxis.unit,QString("(u)^2 * (ka)"));
    ResultRepository repo;QVERIFY(repo.open(":memory:").isEmpty());const auto saved=repo.save(run);QVERIFY2(saved.saved(),qPrintable(saved.error));
    const auto loaded=repo.load(saved.recordId);QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.nodes[good].report,run.nodes[good].report);QCOMPARE(loaded.run.nodes[good].warnings,run.nodes[good].warnings);
    const auto node=run.nodes.value(good), restored=loaded.run.nodes.value(good);
    for(auto it=node.outputs.cbegin();it!=node.outputs.cend();++it)
        QCOMPARE(encodeData(restored.outputs.value(it.key())).bytes,encodeData(it.value()).bytes);
}
int runMtmSimulationAudit(int argc,char** argv) { MtmSimulationAudit tests;return QTest::qExec(&tests,argc,argv); }
#include "mtm_simulation_audit.moc"
