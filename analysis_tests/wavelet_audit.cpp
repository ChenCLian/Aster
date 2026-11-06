// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "wavelet_audit.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <algorithm>
#include "processing/scientific_registry.h"
#include "processing/science_helpers.h"
#include "core/workflow_executor.h"
#include "core/workflow_model.h"
#include "io/database/data_codec.h"
#include "math/statistics/significance.h"
#include "math/wavelet/cwt_support.h"
using namespace aster;
namespace {
QVector<double> numbers(const QJsonValue& json)
{ QVector<double> out; for (const auto& v:json.toArray()) out.push_back(v.toDouble()); return out; }
NodeResult execute(const SeriesData& data, const QJsonObject& parameters = {})
{
    WorkflowModel model(makeScientificRegistry());
    const auto root=model.addRoot("input_series",{0,0}).nodeId;
    auto explicitConvention = parameters;
    explicitConvention["method"] = "torrence_compo";
    const auto node=model.addChild(root,ChildSlot::Down,"cwt",explicitConvention).nodeId;
    return WorkflowExecutor().run({model.freeze().snapshot,root,{"wavelet audit",makeData(data),{}}}).nodes[node];
}
SeriesData signal(int n=64, double dt=1)
{
    SeriesData data; data.xAxis={"Time","ka"}; data.yAxis={"Signal","u"};
    for (int i=0;i<n;++i) { data.x.push_back(i*dt); data.y.push_back(std::sin(.31*i)+.2*std::cos(.77*i)); }
    return data;
}
bool close(const QVector<double>& a,const QVector<double>& b,double tolerance=2e-8)
{
    if (a.size()!=b.size()) return false;
    double scale=0;for(double v:b) scale=std::max(scale,std::abs(v));
    for(qsizetype i=0;i<a.size();++i)
        if(!std::isfinite(a[i]) || std::abs(a[i]-b[i])>tolerance*std::max(scale,std::numeric_limits<double>::min())) return false;
    return true;
}
}
class WaveletAudit:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {science_detail::initializeGsl();}
    void references_data(); void references();
    void timeUnitInvariance_data(); void timeUnitInvariance();
    void amplitudeAndOffsetInvariance();
    void tinyPowerAndConstant();
    void numericRangeErrors();
    void orderAndNominalPeriod();
    void clippedLagAndMetadata();
    void significanceFormulaAndValidation();
};
void WaveletAudit::references_data()
{
    QTest::addColumn<QJsonObject>("c");
    QFile file(QFINDTESTDATA("fixtures/wavelet_reference.json"));QVERIFY(file.open(QIODevice::ReadOnly));
    for(const auto& v:QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray()) {
        auto c=v.toObject();QTest::newRow(qPrintable(c["name"].toString()))<<c;
    }
}
void WaveletAudit::references()
{
    QFETCH(QJsonObject,c);
    SeriesData data{numbers(c["x"]),numbers(c["y"]),{"Time","ka"},{"Signal","u"}};
    const auto result=execute(data,{{"mother",c["mother"]},{"parameter",c["parameter"]},{"dj",c["dj"]}});
    QVERIFY2(result.state==NodeState::Succeeded,qPrintable(result.error));
    const auto* power=dataAs<GridData>(result.outputs["power"]);
    const auto* ratio=dataAs<GridData>(result.outputs["significance_ratio"]);
    const auto* coi=dataAs<SeriesData>(result.outputs["coi"]);
    QVERIFY(power && ratio && coi);
    QCOMPARE(power->rows,c["rows"].toInt());QCOMPARE(power->columns,data.x.size());
    QVERIFY(close(power->y,numbers(c["period"])));QVERIFY(close(power->values,numbers(c["power"])));
    QVERIFY(close(ratio->values,numbers(c["ratio"])));QVERIFY(close(coi->y,numbers(c["coi"])));
    QCOMPARE(power->x,data.x);QCOMPARE(coi->x,data.x);QCOMPARE(power->valueAxis.unit,QString("(u)^2"));
    QCOMPARE(power->yAxis.unit,QString("ka"));QCOMPARE(ratio->valueAxis.unit,QString("dimensionless"));
    QCOMPARE(result.report["padding_samples"].toInt(),c["padding"].toInt());
    QVERIFY(std::abs(result.report["lag1"].toDouble()-c["rho"].toDouble())<1e-12);
    for(auto it=result.outputs.begin();it!=result.outputs.end();++it) {
        const auto encoded=encodeData(it.value());QVERIFY(encoded.ok());
        const auto decoded=decodeData(encoded.bytes);QVERIFY(decoded.ok());
        QCOMPARE(encodeData(decoded.data).bytes,encoded.bytes);
    }
}
void WaveletAudit::timeUnitInvariance_data()
{
    QTest::addColumn<double>("dt");
    QTest::newRow("subnormal")<<1e-310;QTest::newRow("small")<<1e-100;
    QTest::newRow("ordinary")<<.001;QTest::newRow("large")<<1e306;
}
void WaveletAudit::timeUnitInvariance()
{
    QFETCH(double,dt);const auto base=execute(signal()),scaled=execute(signal(64,dt));
    QVERIFY2(scaled.state==NodeState::Succeeded,qPrintable(scaled.error));
    for(const auto& key:QStringList{"power","significance_ratio"}) {
        const auto* a=dataAs<GridData>(base.outputs[key]);const auto* b=dataAs<GridData>(scaled.outputs[key]);
        QVERIFY(a && b);QVERIFY(close(a->values,b->values,2e-12));
        for(qsizetype i=0;i<a->y.size();++i) QVERIFY(std::abs(b->y[i]/dt/a->y[i]-1)<2e-12);
    }
}
void WaveletAudit::amplitudeAndOffsetInvariance()
{
    auto data=signal();for(int i=0;i<data.y.size();++i) data.y[i]=(i*7)%19-9;
    const auto base=execute(data);for(auto& v:data.y) v=8*v+1e15;
    const auto shifted=execute(data);QVERIFY(shifted.state==NodeState::Succeeded);
    auto expected=dataAs<GridData>(base.outputs["power"])->values;for(auto&v:expected)v*=64;
    QVERIFY(close(dataAs<GridData>(shifted.outputs["power"])->values,expected,1e-12));
    QVERIFY(close(dataAs<GridData>(shifted.outputs["significance_ratio"])->values,dataAs<GridData>(base.outputs["significance_ratio"])->values,1e-12));
}
void WaveletAudit::tinyPowerAndConstant()
{
    auto data=signal();const auto base=execute(data);for(auto&v:data.y)v*=1e-160;
    const auto tiny=execute(data);QVERIFY2(tiny.state==NodeState::Succeeded,qPrintable(tiny.error));
    QVERIFY(close(dataAs<GridData>(tiny.outputs["significance_ratio"])->values,dataAs<GridData>(base.outputs["significance_ratio"])->values,1e-12));
    const auto& p=dataAs<GridData>(tiny.outputs["power"])->values;
    QVERIFY(std::any_of(p.begin(),p.end(),[](double x){return x>0;}));
    for(double value:{0.,1e307}) {
        data.y.fill(value);const auto constant=execute(data);QVERIFY(constant.state==NodeState::Succeeded);
        QVERIFY(!constant.outputs.contains("significance_ratio"));QVERIFY(!constant.warnings.isEmpty());
        for(double v:dataAs<GridData>(constant.outputs["power"])->values)QCOMPARE(v,0.);
    }
}
void WaveletAudit::numericRangeErrors()
{
    for(double scale:{1e155,1e-200}) {
        auto data=signal();for(auto&v:data.y)v*=scale;
        const auto r=execute(data);QCOMPARE(r.state,NodeState::Failed);QCOMPARE(r.diagnostic().code,QString("NUMERIC_RANGE"));
    }
    const auto r=execute(signal(64,1e306),{{"mother","dog"},{"parameter",2}});
    QCOMPARE(r.state,NodeState::Failed);QCOMPARE(r.diagnostic().code,QString("NUMERIC_RANGE"));
}
void WaveletAudit::orderAndNominalPeriod()
{
    const auto r=execute(signal(),{{"parameter",1}});QCOMPARE(r.diagnostic().code,QString("CWT_MORLET_PARAMETER"));
    for(const auto& mother:QStringList{"morlet","paul"}) {
        const auto result=execute(signal(),{{"mother",mother},{"parameter",20}});
        QCOMPARE(result.state,NodeState::Succeeded);QVERIFY(dataAs<GridData>(result.outputs["power"])->y.front()>=2-1e-14);
        QVERIFY(result.report["s0_samples"].toDouble()>2);QVERIFY(!result.warnings.isEmpty());
    }
    const auto result=execute(signal(),{{"dj",.25},{"significance",false}});
    QCOMPARE(dataAs<GridData>(result.outputs["power"])->rows,21);QVERIFY(!result.outputs.contains("significance_ratio"));
}
void WaveletAudit::clippedLagAndMetadata()
{
    auto data=signal();for(int i=0;i<data.y.size();++i)data.y[i]=std::exp(.1*i);
    const auto r=execute(data);QCOMPARE(r.state,NodeState::Succeeded);
    QVERIFY(r.report["lag1_clipped"].toBool());QVERIFY(r.report["lag1_raw"].toDouble()>.99);QCOMPARE(r.report["lag1"].toDouble(),.99);
    QCOMPARE(r.report["significance_dof"].toDouble(),2.);QVERIFY(r.report["significance_model"].toString().contains("pointwise"));
    QVERIFY(r.report["coi_semantics"].toString().contains("not masked"));
    QCOMPARE(makeScientificRegistry().find("cwt")->descriptor.version,3);
}
void WaveletAudit::significanceFormulaAndValidation()
{
    const double power[]={1e-320,2e-320,3e-320},scale[]={4};
    const auto ratio=wave_Significance(3,1,power,scale,1e-320,1,6,"morlet",0,.95);
    const double quantile=-std::log(.05); // chi-square(2)/2 is exponential(1).
    for(int i=0;i<3;++i)QVERIFY(std::abs(ratio[i]-(power[i]/1e-320)/quantile)<1e-12);
    QVERIFY_EXCEPTION_THROWN(wave_Significance(3,1,power,scale,0,1,6,"morlet"),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(wave_Significance(3,1,power,scale,1,1,6,"morlet",1),std::invalid_argument);
}
int runWaveletAudit(int argc,char**argv) {WaveletAudit tests;return QTest::qExec(&tests,argc,argv);}
#include "wavelet_audit.moc"
