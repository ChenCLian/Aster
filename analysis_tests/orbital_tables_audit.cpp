#include "orbital_tables_audit.h"
#include "io/astronomy/orbital_table.h"
#include "math/astronomy/orbital_elements.h"
#include "processing/orbital_table_tasks.h"
#include "processing/scientific_registry.h"
#include "application/batch_runner.h"
#include "io/exports/csv_export.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <future>
#include <cmath>
using namespace aster;
using namespace aster::astronomy;
namespace {
QJsonArray reference()
{
    QFile file(QFINDTESTDATA("fixtures/orbital_tables_reference.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object()["solutions"].toArray();
}
OrbitalQuantity quantity(const QString& key)
{
    if (key=="eccentricity") return OrbitalQuantity::Eccentricity;
    if (key=="obliquity") return OrbitalQuantity::Obliquity;
    if (key=="precession") return OrbitalQuantity::ClimaticPrecession;
    return OrbitalQuantity::AxialPrecession;
}
}
class OrbitalTablesAudit : public QObject {
    Q_OBJECT
private slots:
    void officialTables_data();
    void officialTables();
    void capabilitySeparation();
    void irregularTimeAndAngleWrap();
    void malformedFiles_data();
    void malformedFiles();
    void registryAndOutputs();
    void gridAndCoverageLimits();
    void localPtFileAndProvenance();
    void cancellationAndConcurrentReads();
    void workflowPersistence();
};
void OrbitalTablesAudit::officialTables_data()
{
    QTest::addColumn<QJsonObject>("referenceCase");
    const auto rows=reference(); QVERIFY(!rows.isEmpty());
    for (const auto& row:rows) {
        auto object=row.toObject(); QTest::newRow(qPrintable(object["id"].toString()))<<object;
    }
}
void OrbitalTablesAudit::officialTables()
{
    QFETCH(QJsonObject,referenceCase);
    const auto data=orbitalDataset(referenceCase["id"].toString());
    QCOMPARE(data.table->size(),size_t(referenceCase["rows"].toInt()));
    QCOMPARE(data.table->maximumAge(),referenceCase["maximum_age"].toDouble());
    QCOMPARE(data.provenance["source_sha256"],referenceCase["source_sha256"]);
    QCOMPARE(data.provenance["epoch"].toString(),QString("J2000"));
    for (const auto& item:referenceCase["knots"].toArray()) {
        const auto knot=item.toObject(); const auto values=knot["values"].toObject();
        for (auto it=values.begin();it!=values.end();++it)
            QCOMPARE(data.table->at(knot["age"].toDouble(),quantity(it.key())),it.value().toDouble());
    }
    for (const auto& item:referenceCase["interpolated"].toArray()) {
        const auto knot=item.toObject(); const auto values=knot["values"].toObject();
        for (auto it=values.begin();it!=values.end();++it) {
            const double got=data.table->at(knot["age"].toDouble(),quantity(it.key()));
            // Independent reference unwrap accumulates ~3e4 rad before interpolation.
            const double error=it.key()=="axial_precession" ? std::abs(std::remainder(got-it.value().toDouble(),2*pi))
                                                           : std::abs(got-it.value().toDouble());
            QVERIFY2(error<(it.key()=="axial_precession"?2e-8:1e-14),qPrintable(QString("%1 error=%2").arg(it.key()).arg(error,0,'g',17)));
        }
    }
    const auto q=data.table->supports(OrbitalQuantity::Eccentricity)?OrbitalQuantity::Eccentricity:OrbitalQuantity::Obliquity;
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,data.table->at(-1,q));
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,data.table->at(data.table->maximumAge()+.01,q));
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,data.table->at(qQNaN(),q));
}
void OrbitalTablesAudit::capabilitySeparation()
{
    const auto ids=eccentricitySolutionIds(); QCOMPARE(ids.size(),9);
    QVERIFY(!ids.contains("la2011")); QVERIFY(!ids.contains("pt_zb18a"));
    for (const auto& id:ids) {
        const auto table=orbitalDataset(id).table;
        QVERIFY(table->supports(OrbitalQuantity::Eccentricity));
        QVERIFY(!table->supports(OrbitalQuantity::Obliquity));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,table->at(0,OrbitalQuantity::Obliquity));
    }
    const auto pt=precessionTiltDataset();
    QVERIFY(!pt.table->supports(OrbitalQuantity::Eccentricity));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,pt.table->at(0,OrbitalQuantity::Eccentricity));
    QCOMPARE(pt.provenance["insolation_supported"].toBool(),false);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,orbitalDataset("unknown"));
}
void OrbitalTablesAudit::irregularTimeAndAngleWrap()
{
    auto table=OrbitalTable::parse("0 .4 3.1 .01\n-.4 .44 -3.1 -.01\n-1 .5 -3 .02\n",OrbitalFormat::PrecessionTilt);
    QCOMPARE(table.size(),size_t(3)); QCOMPARE(table.minimumSpacing(),.4); QCOMPARE(table.maximumSpacing(),.6);
    QVERIFY(std::abs(table.at(.2,OrbitalQuantity::Obliquity)-.42)<1e-15);
    QVERIFY(std::abs(table.at(.2,OrbitalQuantity::ClimaticPrecession))<1e-15);
    QVERIFY(std::abs(std::abs(table.at(.2,OrbitalQuantity::AxialPrecession))-pi)<1e-15);
    QVERIFY(std::abs(table.at(.7,OrbitalQuantity::Obliquity)-.47)<1e-15);
    // PT climate precession is independent of sin(column 3); no reconstruction.
    QCOMPARE(table.at(0,OrbitalQuantity::ClimaticPrecession),.01);
    auto fortran=OrbitalTable::parse("0 .1D0\n-1 .2d0\n",OrbitalFormat::La2010);
    QVERIFY(std::abs(fortran.at(.5,OrbitalQuantity::Eccentricity)-.15)<1e-15);
}
void OrbitalTablesAudit::malformedFiles_data()
{
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<int>("schema");
    QTest::newRow("empty")<<QByteArray()<<int(OrbitalFormat::La2010);
    QTest::newRow("one-row")<<QByteArray("0 .1\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("nan")<<QByteArray("0 nan\n-1 .1\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("infinity")<<QByteArray("0 .1\n-1 inf\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("ecc-range")<<QByteArray("0 1\n-1 .1\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("duplicate")<<QByteArray("0 .1\n0 .2\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("wrong-sign")<<QByteArray("0 .1\n1 .2\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("missing-zero")<<QByteArray("-1 .1\n-2 .2\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("wrong-schema")<<QByteArray("0 .1 .4\n-1 .1 .4\n")<<int(OrbitalFormat::La2010);
    QTest::newRow("bad-tilt")<<QByteArray("0 7 0 .01\n-1 .4 .1 .02\n")<<int(OrbitalFormat::PrecessionTilt);
    QTest::newRow("bad-climate")<<QByteArray("0 .4 0 2\n-1 .4 .1 .02\n")<<int(OrbitalFormat::PrecessionTilt);
    QTest::newRow("bad-inc")<<QByteArray("age ecc inc\n0 .1 200\n1.6 .2 7\n")<<int(OrbitalFormat::ZbEccentricity);
    QTest::newRow("wrong-header")<<QByteArray("age inc ecc\n0 .1 7\n1.6 .2 7\n")<<int(OrbitalFormat::ZbEccentricity);
}
void OrbitalTablesAudit::malformedFiles()
{
    QFETCH(QByteArray,bytes); QFETCH(int,schema);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,OrbitalTable::parse(bytes,OrbitalFormat(schema)));
}
void OrbitalTablesAudit::registryAndOutputs()
{
    const auto registry=makeScientificRegistry();
    for (const auto& key:{"eccentricity_source","precession_tilt_source"}) {
        const auto& d=registry.find(key)->descriptor;
        QVERIFY(!d.inputKind); QVERIFY(!d.requiresSourceBinding);
        const auto normalized=registry.normalize(key,{}); QVERIFY(normalized.validation.ok());
        const auto result=registry.create(key,{})->execute({},{});
        QCOMPARE(result.state,NodeState::Succeeded); QVERIFY(registry.validateOutput(d,result).isEmpty());
        for (const auto& ref:result.outputs) {
            const auto* series=dataAs<SeriesData>(ref); QVERIFY(series);
            QCOMPARE(series->x.size(),2001); QCOMPARE(series->x.front(),0.); QCOMPARE(series->x.back(),2000.);
            QVERIFY(validateData(ref).isEmpty()); QCOMPARE(series->xAxis.unit,QString("ka"));
        }
        QVERIFY(!result.warnings.isEmpty()); QVERIFY(result.report.contains("orbital_solution"));
    }
    const auto result=registry.create("precession_tilt_source",{})->execute({},{});
    QCOMPARE(result.outputs.size(),3);
    QVERIFY(std::abs(dataAs<SeriesData>(result.outputs["obliquity"])->y.front()-23.4392911)<1e-7);
    QCOMPARE(dataAs<SeriesData>(result.outputs["precession"])->yAxis.unit,QString("1"));
    QCOMPARE(dataAs<SeriesData>(result.outputs["axial_precession"])->yAxis.unit,QString("rad"));
    QVERIFY(!registry.create("eccentricity_source",{{"eccentricity_solution","La2011"}}));
    for (const auto& id:eccentricitySolutionIds()) {
        auto r=registry.create("eccentricity_source",{{"eccentricity_solution",id},{"orbit_end_ka",100}})->execute({},{});
        QCOMPARE(r.state,NodeState::Succeeded); QCOMPARE(r.outputs.size(),1);
        QCOMPARE(dataAs<SeriesData>(r.outputs["eccentricity"])->y.front(),orbitalDataset(id).table->at(0,OrbitalQuantity::Eccentricity));
    }
}
void OrbitalTablesAudit::gridAndCoverageLimits()
{
    auto registry=makeScientificRegistry();
    for (const QJsonObject& p:QList<QJsonObject>{{{"orbit_end_ka",0}},{{"orbit_start_ka",100},{"orbit_end_ka",10}},
        {{"orbit_step_ka",.001}},{{"orbit_step_ka",3000}}}) {
        auto r=registry.create("eccentricity_source",p)->execute({},{});
        QCOMPARE(r.state,NodeState::Failed); QCOMPARE(r.diagnostic().code,QString("ORBITAL_GRID")); QVERIFY(r.outputs.isEmpty());
    }
    const auto over=registry.create("eccentricity_source",{{"orbit_end_ka",250000}})->execute({},{});
    QCOMPARE(over.state,NodeState::Failed); QCOMPARE(over.diagnostic().code,QString("ORBITAL_RANGE"));
    const auto edge=registry.create("eccentricity_source",{{"orbit_start_ka",249998},{"orbit_end_ka",249999}})->execute({},{});
    QCOMPARE(edge.state,NodeState::Succeeded); QVERIFY(edge.warnings.contains("warning.orbital_deep_time"));
    const auto decimal=registry.create("eccentricity_source",{{"orbit_start_ka",.1},{"orbit_end_ka",.3},{"orbit_step_ka",.2}})->execute({},{});
    QCOMPARE(decimal.state,NodeState::Succeeded); QCOMPARE(dataAs<SeriesData>(decimal.outputs["eccentricity"])->x.size(),2);
    auto p=registry.normalize("eccentricity_source",{}).parameters;p["orbit_step_ka"]=0;
    QCOMPARE(EccentricitySourceTask(p).execute({},{}).state,NodeState::Failed);
    p["orbit_step_ka"]=1;p["orbit_start_ka"]=QJsonValue();
    QCOMPARE(EccentricitySourceTask(p).execute({},{}).state,NodeState::Failed);
}
void OrbitalTablesAudit::localPtFileAndProvenance()
{
    const auto embedded=precessionTiltDataset(); // also initializes the resources
    QFile compressed(":/orbital_solutions/pt_zb18a.qz"); QVERIFY(compressed.open(QIODevice::ReadOnly));
    const auto bytes=qUncompress(compressed.readAll()); QVERIFY(!bytes.isEmpty());
    QTemporaryDir dir; const auto path=dir.filePath("PT.De1.0000Td1.0000.dat");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes),bytes.size()); file.close();
    const auto external=precessionTiltDataset(path);
    QCOMPARE(external.provenance["source_sha256"],embedded.provenance["source_sha256"]);
    QCOMPARE(external.provenance["ed"].toDouble(),1.); QCOMPARE(external.provenance["td"].toDouble(),1.);
    QCOMPARE(external.provenance["label"].toString(),QString("PT-ZB18a (Ed=1.0000, Td=1.0000)"));
    QVERIFY(!external.provenance.contains("resource")); QVERIFY(!external.provenance.contains("retrieved"));
    QCOMPARE(external.provenance["storage"].toString(),QString("external ASCII file"));
    QCOMPARE(external.table->at(12345.6,OrbitalQuantity::ClimaticPrecession),embedded.table->at(12345.6,OrbitalQuantity::ClimaticPrecession));
    const auto result=makeScientificRegistry().create("precession_tilt_source",{{"pt_table_path",path}})->execute({},{});
    QCOMPARE(result.state,NodeState::Succeeded); QVERIFY(result.warnings.contains("warning.pt_external"));
    QCOMPARE(result.report["orbital_solution"].toObject()["source_sha256"],embedded.provenance["source_sha256"]);
    QFile wrong(dir.filePath("renamed.dat")); QVERIFY(wrong.open(QIODevice::WriteOnly));wrong.write(bytes);wrong.close();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,precessionTiltDataset(wrong.fileName()));
    QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write("0 .4 0 .01\n-1 .4 .1 .01\n");file.close();
    const auto invalid=makeScientificRegistry().create("precession_tilt_source",{{"pt_table_path",path}})->execute({},{});
    QCOMPARE(invalid.state,NodeState::Failed);QCOMPARE(invalid.diagnostic().code,QString("ORBITAL_DATA"));
    QVERIFY(invalid.outputs.isEmpty());
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,precessionTiltDataset(dir.filePath("PT.De1.0000Td0.4000.dat")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,precessionTiltDataset(dir.filePath("PT.De1.5000Td1.0000.dat")));
}
void OrbitalTablesAudit::cancellationAndConcurrentReads()
{
    ExecutionContext context; context.cancellation.cancel();
    auto registry=makeScientificRegistry();
    for (const auto& key:{"eccentricity_source","precession_tilt_source"})
        QCOMPARE(registry.create(key,{})->execute({},context).state,NodeState::Cancelled);
    auto evaluate=[] {return makeScientificRegistry().create("eccentricity_source",{{"eccentricity_solution","zb20d"}})->execute({},{});};
    auto future=std::async(std::launch::async,evaluate); const auto a=evaluate(),b=future.get();
    QCOMPARE(a.state,NodeState::Succeeded); QCOMPARE(b.state,NodeState::Succeeded);
    QCOMPARE(dataAs<SeriesData>(a.outputs["eccentricity"])->y,dataAs<SeriesData>(b.outputs["eccentricity"])->y);
    ExecutionContext midway;midway.progress=[&](int,const QString&){midway.cancellation.cancel();};
    QCOMPARE(registry.create("eccentricity_source",{})->execute({},midway).state,NodeState::Cancelled);
}
void OrbitalTablesAudit::workflowPersistence()
{
    WorkflowModel model(makeScientificRegistry());
    const auto root=model.addRoot("eccentricity_source",{0,0},{{"eccentricity_solution","zb20a"}}).nodeId;
    QVERIFY(!root.isNull());
    const auto filter=model.addChild(root,ChildSlot::Down,"gaussian_filter",{{"lower_frequency",.002},{"upper_frequency",.003}}).nodeId;
    QVERIFY(!filter.isNull());
    const auto run=WorkflowExecutor().run({model.freeze().snapshot,root,{"ZB20a",{},{}}});
    QCOMPARE(run.state,RunState::Succeeded);QCOMPARE(run.executionOrder.size(),2);
    QTemporaryDir directory;ResultRepository repository;QVERIFY(repository.open(directory.filePath("orbit.sqlite")).isEmpty());
    const auto save=repository.save(run);QVERIFY(save.saved());const auto loaded=repository.load(save.recordId);QVERIFY(loaded.error.isEmpty());
    QCOMPARE(loaded.run.nodes[root].report,run.nodes[root].report);
    QCOMPARE(dataAs<SeriesData>(loaded.run.nodes[root].outputs["eccentricity"])->y,dataAs<SeriesData>(run.nodes[root].outputs["eccentricity"])->y);
    // Independent PT root; table channels flow through ordinary Series ports.
    const auto pt=model.addRoot("precession_tilt_source",{10,0},{{"orbit_end_ka",100}}).nodeId;
    const auto lowess=model.addChild(pt,ChildSlot::Down,"detrend_lowess",{},"obliquity").nodeId;
    QVERIFY(!lowess.isNull());
    const auto second=WorkflowExecutor().run({model.freeze().snapshot,pt,{"PT",{},{}}});
    QCOMPARE(second.state,RunState::Succeeded);QCOMPARE(second.executionOrder.size(),2);
    const auto savedPt=repository.save(second);QVERIFY(savedPt.saved());
    const auto restored=repository.load(savedPt.recordId);QVERIFY(restored.error.isEmpty());
    QCOMPARE(restored.run.nodes[pt].report,second.nodes[pt].report);
}
int runOrbitalTablesAudit(int argc,char** argv) { OrbitalTablesAudit test;return QTest::qExec(&test,argc,argv); }
#include "orbital_tables_audit.moc"
