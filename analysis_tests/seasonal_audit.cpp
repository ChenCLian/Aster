#include "seasonal_audit.h"
#include "math/astronomy/seasonal_insolation.h"
#include "math/astronomy/astronomical_curves.h"
#include "processing/scientific_registry.h"
#include "processing/services/series_cleanup.h"
#include "io/files/series_file_reader.h"
#include "io/database/result_repository.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
using namespace aster;
using namespace aster::astronomy;
namespace {
QJsonObject reference() {
    QFile f(QFINDTESTDATA("fixtures/seasonal_reference.json"));
    if(!f.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
OrbitalElements orbit(const QJsonObject& c) {
    const auto o=c["orbit"].toArray();return {o[0].toDouble(),o[1].toDouble(),o[2].toDouble()};
}
}
class SeasonalAudit:public QObject {
    Q_OBJECT
private slots:
    void independentTimeWeightedReference();
    void physicalInvariants();
    void calendarValidationAndCancellation();
    void sourcePersistenceAndBudget();
    void standaloneCleanupPolicies();
    void realImportPeaksAndFrequencySearch();
};
void SeasonalAudit::independentTimeWeightedReference() {
    const auto ref=reference();QVERIFY(!ref.isEmpty());
    double maximum=0;
    for(const auto v:ref["seasons"].toArray()) {
        const auto c=v.toObject();const auto r=integrateSeason(orbit(c),c["lat"].toDouble()*radians,c["start"].toDouble(),c["end"].toDouble(),1365);
        maximum=std::max(maximum,std::abs(r.meanWm2-c["mean"].toDouble()));
        QVERIFY2(std::abs(r.meanWm2-c["mean"].toDouble())<2e-6,qPrintable(QString::number(r.meanWm2,'g',17)));
        QVERIFY(std::abs(r.energyMJm2-c["energy"].toDouble())<2e-5);
        QVERIFY(std::abs(r.durationDays-c["duration"].toDouble())<1e-8);
    }
    for(const auto v:ref["dates"].toArray()) {
        const auto c=v.toObject();const auto longitude=solarLongitudeAtDay(orbit(c),c["day"].toDouble());
        QVERIFY(std::abs(std::remainder(longitude-c["longitude"].toDouble(),2*pi))<1e-10);
    }
    for(const auto v:ref["calendar"].toArray()) {
        const auto c=v.toObject();const bool summer=c["first"].toInt()==152;
        const auto r=summer ? integrateCalendarSeason(orbit(c),c["lat"].toDouble()*radians,6,1,9,1,1365)
                            : integrateCalendarSeason(orbit(c),c["lat"].toDouble()*radians,12,1,3,1,1365);
        QVERIFY(std::abs(r.meanWm2-c["mean"].toDouble())<2e-6);
        QVERIFY(std::abs(r.energyMJm2-c["energy"].toDouble())<2e-5);
        QVERIFY(std::abs(r.durationDays-c["duration"].toDouble())<1e-9);
    }
    qInfo()<<"Season mean maximum absolute difference from independent time-weighted quadrature (W/m2):"<<maximum;
}
void SeasonalAudit::physicalInvariants() {
    const OrbitalElements circular{0,0,0},elliptical{.08,.42,1.5};
    const auto exact=integrateSeason(circular,0,pi/2,pi,1365);
    QVERIFY(std::abs(exact.meanWm2-1365/pi)<1e-10);
    QVERIFY(std::abs(exact.durationDays-tropicalYearDays/4)<1e-10);
    const auto full=integrateSeason(elliptical,65*radians,0,0,1365);
    double energy=0,duration=0;
    for(int i=0;i<4;++i){const auto r=integrateSeason(elliptical,65*radians,i*pi/2,(i+1)*pi/2,1365);energy+=r.energyMJm2;duration+=r.durationDays;}
    QVERIFY(std::abs(energy-full.energyMJm2)<2e-5);QVERIFY(std::abs(duration-full.durationDays)<1e-10);
    auto rotated=elliptical;rotated.perihelion+=1.2;
    const auto other=integrateSeason(rotated,65*radians,0,0,1365);
    QVERIFY(std::abs(other.energyMJm2-full.energyMJm2)<1e-8);
    const auto twice=integrateSeason(elliptical,65*radians,0,0,2730);
    QVERIFY(std::abs(twice.energyMJm2-2*full.energyMJm2)<1e-8);
    const auto polar=integrateSeason(elliptical,pi/2,pi,2*pi,1365);
    QVERIFY(polar.energyMJm2<1e-10);
    const auto wrap=integrateSeason(elliptical,0,3*pi/2,pi/2,1365);
    const auto first=integrateSeason(elliptical,0,3*pi/2,2*pi,1365),second=integrateSeason(elliptical,0,0,pi/2,1365);
    QVERIFY(std::abs(wrap.energyMJm2-first.energyMJm2-second.energyMJm2)<1e-7);
    const auto calendar=integrateCalendarSeason(elliptical,65*radians,6,1,6,1,1365);
    QVERIFY(std::abs(calendar.energyMJm2-full.energyMJm2)<2e-5);
    QCOMPARE(calendar.durationDays,tropicalYearDays);
}
void SeasonalAudit::calendarValidationAndCancellation() {
    QCOMPARE(calendarDay365(3,21),80);QCOMPARE(calendarDay365(12,31),365);
    for(const auto& date:QList<QPair<int,int>>{{2,29},{4,31},{0,1},{13,1},{1,0}})
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,calendarDay365(date.first,date.second));
    const OrbitalElements o{.0167,.409,1.8};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,integrateSeason(o,0,-.1,1,1365));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,integrateSeason(o,0,0,7,1365));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,integrateSeason(o,2,0,1,1365));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,solarLongitudeAtDay(o,qQNaN()));
    int calls=0;QVERIFY_THROWS_EXCEPTION(std::runtime_error,integrateSeason(o,65*radians,0,2*pi,1365,[&]{return ++calls>2;}));
    QVERIFY(calls>2);
}
void SeasonalAudit::sourcePersistenceAndBudget() {
    const auto registry=makeScientificRegistry();
    const auto* desc=registry.find("seasonal_insolation");QVERIFY(desc);QVERIFY(!desc->descriptor.inputKind);QVERIFY(!desc->descriptor.requiresSourceBinding);
    auto invalid=registry.create("seasonal_insolation",{{"age_end_ka",2},{"season_basis","calendar_365"},{"month_start",2},{"day_start",29}})->execute({},{});
    QCOMPARE(invalid.diagnostic().code,QString("ASTRONOMY_PARAMETERS"));QVERIFY(invalid.outputs.isEmpty());
    auto large=registry.create("seasonal_insolation",{{"age_end_ka",25000}})->execute({},{});
    QCOMPARE(large.diagnostic().code,QString("ASTRONOMY_SEASON_BUDGET"));
    ExecutionContext cancelled;cancelled.cancellation.cancel();
    QCOMPARE(registry.create("seasonal_insolation",{})->execute({},cancelled).state,NodeState::Cancelled);
    WorkflowModel model(registry);
    auto root=model.addRoot("seasonal_insolation",{0,0},{{"age_end_ka",4},{"season_basis","calendar_365"}}).nodeId;QVERIFY(!root.isNull());
    const auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"season",{},{}}});
    QCOMPARE(run.state,RunState::Succeeded);const auto r=run.nodes[root];
    QVERIFY(registry.validateOutput(desc->descriptor,r).isEmpty());
    const auto* mean=dataAs<SeriesData>(r.outputs["season_mean"]);const auto* energy=dataAs<SeriesData>(r.outputs["season_energy"]);const auto* duration=dataAs<SeriesData>(r.outputs["season_duration"]);
    QVERIFY(mean&&energy&&duration);QCOMPARE(mean->x.size(),5);QCOMPARE(energy->yAxis.unit,QString::fromUtf8("MJ/m²"));
    for(int i=0;i<5;++i)QVERIFY(std::abs(mean->y[i]*duration->y[i]*86400/1e6-energy->y[i])<1e-10);
    QTemporaryDir dir;ResultRepository repo;QVERIFY(repo.open(dir.filePath("season.sqlite")).isEmpty());
    const auto saved=repo.save(run);QVERIFY(saved.saved());const auto loaded=repo.load(saved.recordId);QVERIFY(loaded.ok());
    QCOMPARE(loaded.run.nodes[root].report,r.report);QCOMPARE(dataAs<SeriesData>(loaded.run.nodes[root].outputs["season_energy"])->y,energy->y);
}
void SeasonalAudit::standaloneCleanupPolicies() {
    SeriesData input{{3,1,1,2,4},{30,8,12,20,20},{"X","ka"},{"Y","u"}};
    for(const auto& policy:{QString("average"),QString("first"),QString("last")}){
        const auto r=makeScientificRegistry().create("clean_series",{{"duplicate_policy",policy}})->execute({makeData(input)},{});
        QCOMPARE(r.state,NodeState::Succeeded);const auto* out=dataAs<SeriesData>(r.outputs["series"]);QVERIFY(out);
        QCOMPARE(out->x,QVector<double>({1,2,3,4}));QCOMPARE(out->y,QVector<double>({policy=="average"?10.:policy=="first"?8.:12.,20,30,20}));
        QCOMPARE(r.report["merged_rows"].toInt(),1);QVERIFY(r.report["reordered"].toBool());QVERIFY(!r.report["resampled"].toBool());
    }
    QCOMPARE(cleanSeries(input,"error").error.code,QString("INPUT_X_DUPLICATE"));
    auto bad=input;bad.y.pop_back();QVERIFY(!cleanSeries(bad,"average").error.isEmpty());
    bad=input;bad.y[0]=qQNaN();QVERIFY(!cleanSeries(bad,"average").error.isEmpty());
    CancellationToken cancel;cancel.cancel();QVERIFY(cleanSeries(input,"average",cancel).cancelled);
    QCOMPARE(input.x,QVector<double>({3,1,1,2,4}));
    const double huge=std::numeric_limits<double>::max();
    const auto finite=cleanSeries(SeriesData{{0,0,0,1},{huge,huge,huge,0},{},{}},"average");
    QVERIFY(finite.error.isEmpty());QCOMPARE(finite.data.y[0],huge);
    const auto cancelling=cleanSeries(SeriesData{{0,0,1},{huge,-huge,0},{},{}},"average");
    QCOMPARE(cancelling.data.y[0],0.);
}
void SeasonalAudit::realImportPeaksAndFrequencySearch() {
    QTemporaryDir dir;const auto path=dir.filePath("signal.csv");QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("time,signal\n9,NaN\n8,\n");
    for(int i=255;i>=0;--i){const QByteArray row=QByteArray::number(i)+","+QByteArray::number(std::sin(2*pi*i/16),'g',17)+"\n";f.write(row);if(i==30)f.write(row);}f.close();
    SeriesFileOptions options;options.skipInvalidRows=true;options.xUnit="ka";
    const auto preview=readSeriesFile(path,options);QVERIFY(preview.ok());QCOMPARE(preview.report["skipped_rows"].toInt(),2);
    WorkflowModel model(makeScientificRegistry());auto root=model.addRoot("file_source",{0,0}).nodeId;
    auto clean=model.addChild(root,ChildSlot::Down,"clean_series").nodeId;
    auto mtm=model.addChild(clean,ChildSlot::Down,"mtm",{{"ar1",false}}).nodeId;
    auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"peak signal",{},{{"path",path},{"file_options",fileOptionsToJson(options)}}}});
    QCOMPARE(run.state,RunState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(run.nodes[root].outputs["series"])->y,dataAs<SeriesData>(preview.data)->y);
    QCOMPARE(run.nodes[clean].report["merged_rows"].toInt(),1);
    const auto* peaks=dataAs<TableData>(run.nodes[mtm].outputs["peaks"]);QVERIFY(peaks);QVERIFY(!peaks->rows.isEmpty());QCOMPARE(run.nodes[mtm].report["peak_count"].toInt(),peaks->rows.size());
    const auto detection=run.nodes[mtm].report["peak_detection"].toObject();
    QCOMPARE(detection["relative_prominence_fraction"].toDouble(),.001);
    const double threshold=detection["effective_minimum_prominence"].toDouble();
    QVERIFY(threshold>0);
    for(const auto& row:peaks->rows) QVERIFY(row[3]>=threshold);
    // The default registry/UI parameter, output table and persisted index share one filter.
    ResultRepository repo;QVERIFY(repo.open(dir.filePath("peaks.sqlite")).isEmpty());const auto saved=repo.save(run);QVERIFY(saved.saved());
    RecordFilter filter;filter.minFrequency=.055;filter.maxFrequency=.07;filter.frequencyUnit="1/ka";
    auto found=repo.list(filter);QVERIFY(found.error.isEmpty());QCOMPARE(found.records.size(),1);
    const auto hits=repo.matchingPeaks(saved.recordId,filter);QVERIFY(hits.error.isEmpty());QVERIFY(!hits.peaks.isEmpty());
    QVERIFY(model.setParameters(mtm,{{"ar1",false},{"peak_height",1e20}}).ok());
    auto noPeaks=WorkflowExecutor().run({model.freeze().snapshot,root,{"filtered peaks",{},{{"path",path},{"file_options",fileOptionsToJson(options)}}}});
    QCOMPARE(noPeaks.state,RunState::Succeeded);QCOMPARE(noPeaks.nodes[mtm].report["peak_count"].toInt(),0);
    const auto second=repo.save(noPeaks);QVERIFY(second.saved());QVERIFY(repo.matchingPeaks(second.recordId,filter).peaks.isEmpty());
}
int runSeasonalAudit(int argc,char** argv){SeasonalAudit test;return QTest::qExec(&test,argc,argv);}
#include "seasonal_audit.moc"
