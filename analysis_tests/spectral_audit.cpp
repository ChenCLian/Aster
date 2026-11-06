// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "spectral_audit.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <algorithm>
#include <gsl/gsl_cdf.h>
#include "math/spectrum/mtm_spectrum.h"
#include "math/spectrum/spectrum_peaks.h"
#include "math/statistics/significance.h"
#include "processing/science_helpers.h"
#include "processing/scientific_registry.h"
#include "core/workflow_executor.h"
#include "core/workflow_model.h"
#include "io/database/data_codec.h"
using namespace aster;
namespace {
std::vector<double> values(const QJsonValue& array)
{ std::vector<double> out; for (const auto& v : array.toArray()) out.push_back(v.toDouble()); return out; }
bool close(const std::vector<double>& actual, const std::vector<double>& expected, double relative = 2e-7)
{
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
        if (!std::isfinite(actual[i]) || std::abs(actual[i]-expected[i]) > relative*std::max(1e-100,std::abs(expected[i])))
            return false;
    return true;
}
}
class SpectralAudit : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { science_detail::initializeGsl(); }
    void relativeProminenceAndRoundoff();
    void constantMtmHasNoPeaks();
    void references_data();
    void references();
    void amplitudeAndSamplingUnits();
    void intermediateRangeErrors();
    void fdrScaleInvariance();
    void realEndpointsAndDegreesOfFreedom();
    void distancePrefersProminence();
    void rejectsInvalidInput();
    void workflowMetadataAndDensityUnits();
};
void SpectralAudit::constantMtmHasNoPeaks()
{
    for (double level : {0., 3., -3., 1e100, 1e-100}) {
        const std::vector<double> y(2183,level);
        const auto result=numeric::mtmSpectrum(y,1.,2.,3,.5,{},y.size(),numeric::MtmConvergenceDomain::FullSpectrum);
        for (double power:result.power) QCOMPARE(power,0.);
        QVERIFY(spectrum::findSpectrumPeaks(int(result.power.size()),result.power.data()).empty());
    }
}
void SpectralAudit::relativeProminenceAndRoundoff()
{
    // Same shape at different physical units: 3 real peaks plus tiny tail ripples.
    const std::vector<double> shape{0, 100, 0, 20, 0, .2, 0, .00001, 0, .00002, 0};
    for (double scale : {1e-200, 1e-12, 1., 1e100}) {
        auto y=shape; for(auto& v:y) v*=scale;
        spectrum::PeakOptions opt; opt.relativeProminence=.001;
        const auto p=spectrum::findSpectrumPeakIndices(int(y.size()),y.data(),opt);
        QVERIFY(p==std::vector<int>({1,3,5}));
        opt.relativeProminence=0;
        QCOMPARE(spectrum::findSpectrumPeaks(int(y.size()),y.data(),opt).size(),size_t(5));
    }
    const double plateau[]{0,2,2,2,0,0,0};
    QCOMPARE(spectrum::findSpectrumPeakIndices(7,plateau),std::vector<int>({2}));
    const double flat[]{1,1,1,1}; QVERIFY(spectrum::findSpectrumPeaks(4,flat).empty());
    const double roundoff[]{1,1+std::numeric_limits<double>::epsilon(),1};
    QVERIFY(spectrum::findSpectrumPeaks(3,roundoff).empty());
    spectrum::PeakOptions invalid; invalid.relativeProminence=-.01;
    QVERIFY_EXCEPTION_THROWN(spectrum::findSpectrumPeaks(7,plateau,invalid),std::invalid_argument);
    invalid.relativeProminence=std::numeric_limits<double>::quiet_NaN();
    QVERIFY_EXCEPTION_THROWN(spectrum::findSpectrumPeaks(7,plateau,invalid),std::invalid_argument);
}
void SpectralAudit::references_data()
{
    QTest::addColumn<QJsonObject>("c");
    QFile file(QFINDTESTDATA("fixtures/spectral_reference.json")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto cases=QJsonDocument::fromJson(file.readAll()).object()["cases"].toArray();
    for (const auto& item:cases) { const auto c=item.toObject(); QTest::newRow(qPrintable(c["name"].toString())) << c; }
}
void SpectralAudit::references()
{
    QFETCH(QJsonObject,c); const auto method=c["method"].toString(); const auto x=values(c["x"]), y=values(c["y"]);
    if (method=="mtm") {
        const auto fs=c["fs"].toDouble();
        const auto r=numeric::mtmSpectrum(y,fs,c["nw"].toDouble(),c["k"].toInt(),fs/2);
        QVERIFY(close(r.frequency,x)); QVERIFY(close(r.power,values(c["power"]),2e-6));
        QVERIFY(close(r.degreesOfFreedom,values(c["dof"]),2e-6)); QVERIFY(r.converged);
    } else if (method=="ar1") {
        const auto r=computeRedNoiseConfidence(y.data(),x.data(),c["window"].toDouble(.001),.5,y.size(),3,c["logfit"].toBool());
        QVERIFY(close(r.theored,c.contains("background") ? values(c["background"]) : y));
        if (c.contains("smooth")) QVERIFY(close(r.pxxsmooth,values(c["smooth"])));
        QVERIFY(std::abs(r.rho-c["rho"].toDouble())<2e-8);
        QVERIFY(std::abs(r.scale/c["amplitude"].toDouble()-1)<2e-7);
    } else if (method=="fdr_full") {
        const auto r=FDR_TEST(y,x,3); QVERIFY(close(r.swa,values(c["background"])));
        const std::array<std::vector<double>,4> curves{r.fdrConf.clfdr5,r.fdrConf.clfdr1,r.fdrConf.clfdr01,r.fdrConf.clfdr001};
        const auto expected=c["curves"].toArray();
        for (size_t i=0;i<4;++i) QVERIFY(close(curves[i],values(expected[i])));
    } else if (method=="swa") {
        SwaDiagnostics d; const auto r=specswa(x,y,&d); QVERIFY(close(r,values(c["background"]),2e-6));
        QCOMPARE(d.shortFallback,y.size()<33); if (!d.shortFallback) QVERIFY(d.window>=11 && d.window<=99);
    } else if (method=="peaks") {
        spectrum::PeakOptions o; o.prominenceRadius=c["radius"].toInt();
        const auto r=spectrum::findSpectrumPeaks(static_cast<int>(y.size()),y.data(),o);
        const auto indices=c["indices"].toArray(), prominences=c["prominence"].toArray(), widths=c["width"].toArray();
        QCOMPARE(r.size(),size_t(indices.size()));
        for (size_t i=0;i<r.size();++i) {
            QCOMPARE(r[i].index,indices[i].toInt());
            QVERIFY(std::abs(r[i].prominence-prominences[i].toDouble())<1e-10);
            QVERIFY(std::abs(r[i].width-widths[i].toDouble())<1e-10);
        }
    } else if (method=="fdr") {
        const auto cutoff=spectralFdrCutoffs(y);const auto expected=c["rejections"].toArray();
        for (size_t q=0;q<cutoff.size();++q) {
            const auto rejected=expected[q].toArray();
            for (size_t i=0;i<y.size();++i) QCOMPARE(!std::isnan(cutoff[q]) && y[i]<=cutoff[q],rejected[i].toBool());
        }
    } else QFAIL("Unknown reference case");
}
void SpectralAudit::amplitudeAndSamplingUnits()
{
    std::vector<double> y(151); for (size_t i=0;i<y.size();++i) y[i]=std::sin(.31*i)+.2*std::cos(.79*i);
    const auto base=numeric::mtmSpectrum(y,1,3,5,.5);
    for (auto& v:y) v*=1e-20;
    const auto scaled=numeric::mtmSpectrum(y,4,3,5,2);
    for (size_t i=0;i<base.power.size();++i) {
        QVERIFY(std::abs(scaled.power[i]/(base.power[i]*1e-40/4)-1)<1e-8);
        QCOMPARE(scaled.frequency[i],4*base.frequency[i]);
        QVERIFY(std::abs(scaled.degreesOfFreedom[i]-base.degreesOfFreedom[i])<1e-8);
    }
}
void SpectralAudit::intermediateRangeErrors()
{
    std::vector<double> large(64),tiny(64);
    for (size_t i=0;i<64;++i) {large[i]=(i%2 ? -1 : 1)*1e154; tiny[i]=(i%2 ? -1 : 1)*1e-200;}
    QVERIFY_EXCEPTION_THROWN(numeric::mtmSpectrum(large,1,3,5,.5),std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(numeric::mtmSpectrum(tiny,1,3,5,.5),std::runtime_error);
}
void SpectralAudit::fdrScaleInvariance()
{
    std::vector<double> x(257),p(257);
    for (size_t i=0;i<p.size();++i) {x[i]=i*.001; p[i]=std::exp(-.012*i)+.02*(1+std::sin(i*.53));}
    p[78]*=100; p[137]*=200;
    const auto base=FDR_TEST(p,x,3);
    for (auto& v:p) v*=1e-40;
    const auto small=FDR_TEST(p,x,3);
    for (size_t i=0;i<p.size();++i) QVERIFY(std::abs(small.swa[i]/(base.swa[i]*1e-40)-1)<1e-8);
    QCOMPARE(base.fdrConf.clfdr5.empty(),false); QCOMPARE(base.fdrConf.clfdr5.size(),small.fdrConf.clfdr5.size());
    for (size_t i=0;i<base.fdrConf.clfdr5.size();++i)
        QVERIFY(std::abs(small.fdrConf.clfdr5[i]/(base.fdrConf.clfdr5[i]*1e-40)-1)<1e-8);
    QCOMPARE(base.testCount,p.size());
    double h=0; for (size_t j=1;j<=p.size();++j) h+=1.0/j;
    QCOMPARE(base.dependenceCorrection,h);
    SpectrumInference mask; mask.tested.assign(p.size(),false);
    const auto none=FDR_TEST(p,x,3,mask); QCOMPARE(none.testCount,size_t(0)); QVERIFY(none.fdrConf.clfdr5.empty());
}
void SpectralAudit::realEndpointsAndDegreesOfFreedom()
{
    std::vector<double> x(257),p(257,2);
    SpectrumInference opt; opt.densityScale.assign(257,2);opt.degreesOfFreedom.assign(257,10);
    for (size_t i=0;i<x.size();++i) x[i]=i/512.0;
    for (const auto i:{0,256}) { p[i]=1;opt.densityScale[i]=1;opt.degreesOfFreedom[i]=5; }
    const auto r=computeRedNoiseConfidence(p.data(),x.data(),.15,.5,257,3,false,opt);
    QVERIFY(close(r.theored,p)); QVERIFY(close(r.pxxsmooth,p));
    QVERIFY(std::abs(r.chiConf.chi95[0]-gsl_cdf_chisq_Qinv(.05,5)/5)<1e-10);
    QVERIFY(std::abs(r.chiConf.chi95[100]-2*gsl_cdf_chisq_Qinv(.05,10)/10)<1e-10);
}
void SpectralAudit::distancePrefersProminence()
{
    // A taller peak sits on a raised local base. The lower but more prominent
    // peak wins the minimum-distance conflict by Aster's documented policy.
    const double y[]={0,8,0,9,10,9,11,0};
    spectrum::PeakOptions o;o.minDistance=4;o.prominenceRadius=1;
    const auto peaks=spectrum::findSpectrumPeaks(8,y,o);
    QCOMPARE(peaks.size(),size_t(2));QCOMPARE(peaks[0].index,1);QCOMPARE(peaks[1].index,6);
}
void SpectralAudit::rejectsInvalidInput()
{
    QVERIFY_EXCEPTION_THROWN(specswa({0,1,3},{1,2,3}),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(specswa({0,0},{1,2}),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(FDR_TEST({0,0},{0,1},3),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(FDR_TEST({1,-1},{0,1},3),std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(spectralFdrCutoffs({-1}),std::invalid_argument);
    const double x[]={0,.25,.5},p[]={1,2,3};
    QVERIFY_EXCEPTION_THROWN(computeRedNoiseConfidence(p,x,-.1,.5,3,3,false),std::invalid_argument);
}
void SpectralAudit::workflowMetadataAndDensityUnits()
{
    WorkflowModel m(makeScientificRegistry()); const auto root=m.addRoot("input_series",{0,0});
    const auto child=m.addChild(root.nodeId,ChildSlot::Down,"mtm",{{"ar1",true}});QVERIFY(child.ok());
    SeriesData s; s.xAxis={"Time","ka"};s.yAxis={"Signal","u"};
    for (int i=0;i<128;++i) {s.x.push_back(i);s.y.push_back(std::sin(.4*i)+.13*std::cos(.91*i));}
    const auto run=WorkflowExecutor().run({m.freeze().snapshot,root.nodeId,{"test",makeData(s),{}}});
    const auto r=run.nodes[child.nodeId];QCOMPARE(r.state,NodeState::Succeeded);
    QVERIFY(r.outputs.contains("ar1_background")); QVERIFY(r.outputs.contains("swa_background"));
    QCOMPARE(makeScientificRegistry().find("mtm")->descriptor.version,7);
    const auto* spectrum=dataAs<SpectrumData>(r.outputs["spectrum"]);QVERIFY(spectrum);
    QCOMPARE(spectrum->powerAxis.unit,QString("(u)^2 * (ka)"));
    QCOMPARE(spectrum->frequencyAxis.unit,QString("1/ka"));
    QCOMPARE(r.report["fdr_test_count"].toInt(),spectrum->frequency.size());
    QVERIFY(r.report["ar1_rho"].toDouble()>-1 && r.report["ar1_rho"].toDouble()<1);
    QVERIFY(r.report["fdr_method"].toString().contains("Acycle bandwidth"));
}
int runSpectralAudit(int argc,char** argv) { SpectralAudit tests; return QTest::qExec(&tests,argc,argv); }
#include "spectral_audit.moc"
