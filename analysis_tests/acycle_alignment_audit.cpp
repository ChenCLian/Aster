// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "acycle_alignment_audit.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include "processing/scientific_registry.h"
#include "core/workflow_model.h"
#include "core/workflow_executor.h"
#include "io/database/data_codec.h"
#include "math/statistics/significance.h"
#include "math/wavelet/cwt_support.h"
using namespace aster;
namespace {
QVector<double> numbers(const QJsonValue& value)
{ QVector<double> r; for (const auto& v:value.toArray()) r.push_back(v.toDouble()); return r; }
QJsonArray json(const QVector<double>& v)
{ QJsonArray r; for (double x:v) r.push_back(x); return r; }
NodeResult execute(const QString& method,const SeriesData& data,const QJsonObject& parameters={})
{
    WorkflowModel model(makeScientificRegistry());
    const auto root=model.addRoot("input_series",{0,0}).nodeId;
    const auto child=model.addChild(root,ChildSlot::Down,method,parameters).nodeId;
    return WorkflowExecutor().run({model.freeze().snapshot,root,{"Acycle audit",makeData(data),{}}}).nodes[child];
}
SeriesData signal(int n=128)
{
    SeriesData d;d.xAxis={"Time","ka"};d.yAxis={"Signal","u"};
    for(int i=0;i<n;++i){d.x.push_back(i);d.y.push_back(std::sin(.31*i)+.27*std::cos(.87*i));}
    return d;
}
double relativeError(const QVector<double>& actual,const QVector<double>& expected)
{
    if(actual.size()!=expected.size()) return std::numeric_limits<double>::infinity();
    double scale=0,error=0;
    for(qsizetype i=0;i<actual.size();++i){
        if(!std::isfinite(actual[i]))return std::numeric_limits<double>::infinity();
        scale=std::max(scale,std::abs(expected[i]));error=std::max(error,std::abs(actual[i]-expected[i]));
    }
    return error/std::max(scale,std::numeric_limits<double>::min());
}
}
class AcycleAlignmentAudit:public QObject {
    Q_OBJECT
    QJsonArray m_exports;
private slots:
    void references_data();void references();
    void defaultsAndLegacyIdentity();
    void outputLimitDoesNotRefit();
    void bandwidthFdrIsNotBy();
    void interpolationDefaultsAndEndpoint();
    void waveletUnitsAndInvalidInput();
    void waveletStationarityGuard();
    void fftValidationAndShortSwa();
    void cleanupTestCase();
};
void AcycleAlignmentAudit::references_data()
{
    QTest::addColumn<QJsonObject>("c");
    QFile file(QFINDTESTDATA("fixtures/acycle_reference.json"));QVERIFY(file.open(QIODevice::ReadOnly));
    for(const auto& value:QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray()){
        auto c=value.toObject();QTest::newRow(qPrintable(c["name"].toString()))<<c;
    }
}
void AcycleAlignmentAudit::references()
{
    QFETCH(QJsonObject,c);
    SeriesData data{numbers(c["x"]),numbers(c["y"]),{"Time","ka"},{"Signal","u"}};
    const auto result=execute(c["method"].toString(),data,c["parameters"].toObject());
    QVERIFY2(result.state==NodeState::Succeeded,qPrintable(result.error));
    QJsonObject actual,errors;
    const auto expected=c["expected"].toObject();
    for(auto it=expected.begin();it!=expected.end();++it){
        const auto target=numbers(it.value());
        if(target.isEmpty()){QVERIFY2(!result.outputs.contains(it.key()),qPrintable(it.key()));continue;}
        QVector<double> values;
        if(const auto* p=dataAs<SpectrumData>(result.outputs[it.key()])){
            values=p->power;QVERIFY(relativeError(p->frequency,numbers(c["frequency"]))<2e-13);
        }else if(const auto* g=dataAs<GridData>(result.outputs[it.key()])){
            values=g->values;QCOMPARE(g->rows,c["rows"].toInt());QCOMPARE(g->columns,data.x.size());
            QVERIFY(relativeError(g->y,numbers(c["period"]))<2e-13);
        }else QFAIL(qPrintable("Missing output: "+it.key()+"; "+result.warnings.join("; ")));
        const double error=relativeError(values,target);
        // Independent nonlinear optimizers may stop at slightly different rho.
        const double tolerance=it.key().startsWith("ar1_") ? 3e-6 : 2e-8;
        QVERIFY2(error<tolerance,qPrintable(it.key()+" relative error "+QString::number(error,'g',12)));
        actual[it.key()]=json(values);errors[it.key()]=error;
        const auto encoded=encodeData(result.outputs[it.key()]);QVERIFY(encoded.ok());
        const auto decoded=decodeData(encoded.bytes);QVERIFY(decoded.ok());
        QCOMPARE(encodeData(decoded.data).bytes,encoded.bytes);
    }
    if(c["method"]=="mtm"){
        QCOMPARE(result.report["tapers"].toInt(),c["tapers"].toInt());
        QCOMPARE(result.report["adaptive_iterations"].toInt(),c["iterations"].toInt());
        QCOMPARE(result.report["significance_dof"].toInt(),2*c["tapers"].toInt());
    }else{
        QVERIFY(std::abs(result.report["lag1"].toDouble()-c["rho"].toDouble())<2e-13);
        const auto* coi=dataAs<SeriesData>(result.outputs["coi"]);QVERIFY(coi);
        QVERIFY(relativeError(coi->y,numbers(c["coi"]))<2e-13);
        actual["coi"]=json(coi->y);
    }
    c["actual_cpp"]=actual;c["relative_errors"]=errors;c["report"]=result.report;m_exports.append(c);
}
void AcycleAlignmentAudit::defaultsAndLegacyIdentity()
{
    const auto registry=makeScientificRegistry();
    QCOMPARE(registry.find("mtm")->descriptor.version,7);
    const auto mtm=registry.normalize("mtm",{}).parameters;
    QCOMPARE(mtm["nw"].toDouble(),2.);QCOMPARE(mtm["nfft"].toInt(),0);
    QCOMPARE(mtm["smooth_window"].toDouble(),.2);QVERIFY(mtm["ar1"].toBool());
    QVERIFY(!mtm["logfit"].toBool());
    QCOMPARE(registry.normalize("detrend_lowess",{}).parameters["iterations"].toInt(),0);
    QVERIFY(registry.find("mtm_ar1_simulation"));
    QVERIFY(!registry.find("mtm_ar1_simulation")->descriptor.catalogVisible);
    QCOMPARE(registry.normalize("cwt",{}).parameters["method"].toString(),QString("acycle_cwt"));
}
void AcycleAlignmentAudit::outputLimitDoesNotRefit()
{
    const auto full=execute("mtm",signal());
    const auto cut=execute("mtm",signal(),{{"fmax_ratio",.4}});
    QCOMPARE(full.state,NodeState::Succeeded);QCOMPARE(cut.state,NodeState::Succeeded);
    QCOMPARE(full.report["ar1_rho"],cut.report["ar1_rho"]);
    QCOMPARE(full.report["fdr_test_count"],cut.report["fdr_test_count"]);
    for(const auto& key:QStringList{"spectrum","ar1_background","swa_background","swa_95"}){
        const auto* a=dataAs<SpectrumData>(full.outputs[key]);const auto* b=dataAs<SpectrumData>(cut.outputs[key]);
        QVERIFY(a && b);QVERIFY(b->power.size()<a->power.size());QCOMPARE(b->power,a->power.mid(0,b->power.size()));
    }
}
void AcycleAlignmentAudit::bandwidthFdrIsNotBy()
{
    const std::vector<double> p{.0001,.015,.8,.9};
    const auto a=acycleFdrCutoffs(p,2.5),b=spectralFdrCutoffs(p);
    QCOMPARE(a[0],.015);QVERIFY(b[0]<.015);
    const auto integerBandwidth=acycleFdrCutoffs(p,2);
    for(size_t i=0;i<a.size();++i) {
        if(std::isnan(a[i])) QVERIFY(std::isnan(integerBandwidth[i]));
        else QCOMPARE(a[i],integerBandwidth[i]);
    }
    QVERIFY(std::isnan(acycleFdrCutoffs({.9,.8},2)[0]));
    QVERIFY_EXCEPTION_THROWN(acycleFdrCutoffs({-.1},2),std::invalid_argument);
}
void AcycleAlignmentAudit::interpolationDefaultsAndEndpoint()
{
    const SeriesData input{{0,1,2,10},{0,1,4,20},{"Time","ka"},{"Signal","u"}};
    const auto result=execute("preprocess",input);
    QCOMPARE(result.state,NodeState::Succeeded);const auto* d=dataAs<SeriesData>(result.outputs["series"]);QVERIFY(d);
    QCOMPARE(d->x.size(),11);QCOMPARE(d->y[3],6.);QCOMPARE(d->x.back(),10.);
    const SeriesData small{{0,.1,.2,.3},{0,1,2,3},{},{}};
    const auto grid=execute("preprocess",small,{{"spacing_mode","fixed"},{"spacing",.1}});
    QCOMPARE(grid.state,NodeState::Succeeded);QCOMPARE(dataAs<SeriesData>(grid.outputs["series"])->x.back(),.3);
}
void AcycleAlignmentAudit::waveletUnitsAndInvalidInput()
{
    auto original=signal(64),scaled=original;
    for(auto& x:scaled.x)x*=4;
    for(auto& y:scaled.y)y*=3;
    const auto a=execute("cwt",original),b=execute("cwt",scaled);
    QCOMPARE(a.state,NodeState::Succeeded);QCOMPARE(b.state,NodeState::Succeeded);
    const auto* raw=dataAs<GridData>(a.outputs["raw_power"]);QVERIFY(raw);
    QCOMPARE(raw->valueAxis.unit,QString("(u)^2"));
    for(const auto& key:QStringList{"power","raw_power","scale_corrected_power","significance_ratio"}){
        const auto* left=dataAs<GridData>(a.outputs[key]);const auto* right=dataAs<GridData>(b.outputs[key]);QVERIFY(left && right);
        auto expected=left->values;const double factor=key=="power"?.25:key=="raw_power"?9:key=="scale_corrected_power"?2.25:1;
        for(auto& x:expected)x*=factor;
        QVERIFY(relativeError(right->values,expected)<1e-11);
    }
    original.y.fill(2);QCOMPARE(execute("cwt",original).state,NodeState::Failed);
    QCOMPARE(execute("cwt",original,{{"method","torrence_compo"}}).state,NodeState::Succeeded);
}
void AcycleAlignmentAudit::fftValidationAndShortSwa()
{
    QCOMPARE(execute("mtm",signal(),{{"nfft",127}}).state,NodeState::Failed);
    QCOMPARE(execute("mtm",signal(),{{"nw",1.1}}).state,NodeState::Failed);
    const auto r=execute("mtm",signal(32),{{"nw",1}});
    QCOMPARE(r.state,NodeState::Succeeded);QVERIFY(!r.outputs.contains("swa_background"));
    QVERIFY(r.warnings.join(";").contains("33 frequency bins"));
}
void AcycleAlignmentAudit::waveletStationarityGuard()
{
    auto data=signal(512);
    for(qsizetype i=0;i<data.y.size();++i)data.y[i]=double(i);
    const auto valid=execute("cwt",data);
    QCOMPARE(valid.state,NodeState::Succeeded);
    QVERIFY(valid.report["lag1"].toDouble()>.99);
    QCOMPARE(valid.report["lag1"],valid.report["lag1_raw"]);
    QVERIFY(valid.outputs.contains("significance_ratio"));
    for(qsizetype i=0;i<data.y.size();++i)data.y[i]=std::sin(2*std::acos(-1.)*i/(data.y.size()-1));
    const auto invalid=execute("cwt",data);
    QCOMPARE(invalid.state,NodeState::Succeeded);
    QVERIFY(invalid.report["lag1_raw"].toDouble()>=1);
    QVERIFY(!invalid.outputs.contains("significance_ratio"));
    QVERIFY(invalid.outputs.contains("raw_power"));QVERIFY(invalid.outputs.contains("power"));
    QVERIFY(invalid.warnings.join(" ").contains("not stationary"));
}
void AcycleAlignmentAudit::cleanupTestCase()
{
    const auto path=qEnvironmentVariable("ASTER_ACYCLE_EXPORT");if(path.isEmpty())return;
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"description","Actual C++ results for optional MATLAB comparison"},{"cases",m_exports}}).toJson());
}
int runAcycleAlignmentAudit(int argc,char** argv){AcycleAlignmentAudit tests;return QTest::qExec(&tests,argc,argv);}
#include "acycle_alignment_audit.moc"
