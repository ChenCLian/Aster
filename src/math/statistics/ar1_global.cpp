// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/statistics/ar1_global.h"
#include "third_party/lowess/Lowess.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aster::numeric {
namespace {
void stop(const std::function<bool()>& cancelled) {
    if(cancelled && cancelled()) throw std::runtime_error("Cancellation requested");
}
// Uses the validated deterministic stream with local state; no shared RNG.
class NormalStream {
    std::uint64_t state;
    double uniform() {
        auto z=(state+=UINT64_C(0x9e3779b97f4a7c15));
        z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
        return ((z^(z>>31))>>11)*0x1.0p-53+0x1.0p-54;
    }
public:
    explicit NormalStream(std::uint64_t seed):state(seed){}
    double normal(){const double u=uniform(),v=uniform();return std::sqrt(-2*std::log(u))*std::cos(2*std::acos(-1.)*v);}
};
void check(std::size_t n,const Ar1GlobalOptions& o) {
    if(n<64 || n>1024)throw std::invalid_argument("AR1 global test supports 64..1024 samples; no automatic truncation or resampling");
    if(o.analysisReplicates<199 || o.analysisReplicates>999 || o.screeningReplicates!=999 ||
       o.analysisReplicates>o.screeningReplicates || !std::isfinite(o.lowessFraction) ||
       o.lowessFraction<.05 || o.lowessFraction>1 || !std::isfinite(o.screeningBudget) ||
       o.screeningBudget<.002 || o.screeningBudget>=.02 || !std::isfinite(o.variationBudget) ||
       o.variationBudget<=0 || o.variationBudget+o.screeningBudget>=.045)
        throw std::invalid_argument("Invalid AR1 global test simulation settings");
}
}
std::vector<Ar1GridCell> ar1GlobalGrid(std::size_t n,const Ar1GlobalOptions& o) {
    check(n,o);
    auto grid=ar1NuisanceGrid(n,o.rhoLower,o.rhoUpper,o.variationBudget,20000);
    const long double entries=static_cast<long double>(grid.size())*(o.screeningReplicates+o.analysisReplicates);
    if(entries*8>160.L*1024*1024 || static_cast<long double>(grid.size())*o.analysisReplicates*n>9000000000.L)
        throw std::invalid_argument("AR1 reference bank exceeds resource budget; use standard simulation precision or a shorter preselected record");
    return grid;
}
Ar1GlobalResult ar1GlobalStatistic(const std::vector<double>& y,double fs,const Ar1GlobalOptions& o,
    std::shared_ptr<const MtmTaperPlan> plan,const std::function<bool()>& cancelled) {
    check(y.size(),o);stop(cancelled);
    if(!std::isfinite(fs)||fs<=0)throw std::invalid_argument("Invalid AR1 sample rate");
    Ar1GlobalResult r;r.lag=centeredLagOne(y);r.analysedSeries=y;
    if(o.lowess){
        std::vector<double> x(y.size()),fit(y.size()),robust(y.size()),residual(y.size());
        for(std::size_t i=0;i<x.size();++i)x[i]=double(i)/(x.size()-1);
        CppLowess::TemplatedLowess<std::vector<double>,double> algorithm;
        if(algorithm.lowess(x,y,o.lowessFraction,0,0.,fit,robust,residual)!=0)
            throw std::runtime_error("Internal LOWESS failed");
        r.analysedSeries=std::move(residual);stop(cancelled);
    }
    r.spectrum=mtmSpectrum(r.analysedSeries,fs,2,3,fs/2,cancelled,y.size(),MtmConvergenceDomain::FullSpectrum,std::move(plan));
    if(!r.spectrum.converged)throw std::runtime_error("MTM did not converge; no global test produced");
    r.fit=fitStationaryAr1(r.analysedSeries);r.background=stationaryAr1Background(r.spectrum,r.fit,fs);
    for(std::size_t i=1;i<r.spectrum.power.size();++i){
        if(y.size()%2==0 && i==y.size()/2)continue;
        const double ratio=r.spectrum.power[i]/r.background[i];
        if(!std::isfinite(ratio))throw std::runtime_error("Nonfinite AR1 global statistic");
        r.maximumRatio=std::max(r.maximumRatio,ratio);
    }
    stop(cancelled);return r;
}
std::shared_ptr<const Ar1ReferenceBank> buildAr1ReferenceBank(std::size_t n,const Ar1GlobalOptions& o,
    const std::function<bool()>& cancelled,const Ar1Progress& progress) {
    const auto grid=ar1GlobalGrid(n,o);stop(cancelled);
    const auto plan=makeMtmTaperPlan(n,2,3,cancelled);
    NormalStream random(o.seed);std::vector<double> innovations(n*o.screeningReplicates);
    for(std::size_t i=0;i<innovations.size();++i){if(i%4096==0)stop(cancelled);innovations[i]=random.normal();}
    auto bank=std::make_shared<Ar1ReferenceBank>();bank->samples=n;bank->options=o;bank->rows.reserve(grid.size());
    int previous=-1;
    for(std::size_t i=0;i<grid.size();++i){
        stop(cancelled);Ar1ReferenceRow row;row.screening.reserve(o.screeningReplicates);row.analysis.reserve(o.analysisReplicates);
        const double rho=grid[i].center,sd=std::sqrt((1-rho)*(1+rho));std::vector<double> y(n);
        for(int j=0;j<o.screeningReplicates;++j){
            if(j%16==0)stop(cancelled);
            const auto base=std::size_t(j)*n;y[0]=innovations[base];
            for(std::size_t k=1;k<n;++k)y[k]=rho*y[k-1]+sd*innovations[base+k];
            row.screening.push_back(centeredLagOne(y));
            if(j<o.analysisReplicates)row.analysis.push_back(ar1GlobalStatistic(y,1,o,plan,cancelled).maximumRatio);
        }
        std::sort(row.screening.begin(),row.screening.end());std::sort(row.analysis.begin(),row.analysis.end());
        bank->rows.push_back(std::move(row));const int percent=int(100*(i+1)/grid.size());
        if(progress && percent!=previous){progress(percent);previous=percent;}
    }
    stop(cancelled);return bank;
}
Ar1GlobalResult evaluateAr1Global(const std::vector<double>& y,double fs,const Ar1ReferenceBank& bank,
    const std::function<bool()>& cancelled) {
    if(y.size()!=bank.samples)throw std::invalid_argument("AR1 reference length differs from input");
    const auto grid=ar1GlobalGrid(bank.samples,bank.options);
    if(grid.size()!=bank.rows.size())throw std::invalid_argument("Incomplete AR1 reference domain");
    auto r=ar1GlobalStatistic(y,fs,bank.options,{},cancelled);
    std::vector<NuisancePValue> p(grid.size());
    for(std::size_t i=0;i<grid.size();++i){
        if(i%64==0)stop(cancelled);
        const auto& row=bank.rows[i];
        if(row.screening.size()!=std::size_t(bank.options.screeningReplicates) || row.analysis.size()!=std::size_t(bank.options.analysisReplicates))
            throw std::invalid_argument("Truncated AR1 reference simulations");
        p[i].screeningP=empiricalTwoSidedP(row.screening,r.lag);
        // Validate every reference row even if excluded by screening.
        EmpiricalTailReference analysis(row.analysis);
        p[i].analysisP=analysis.upperTail(r.maximumRatio);
        r.variationBound=std::max(r.variationBound,grid[i].variationBound);
    }
    r.test=combineNuisancePValues(p,bank.options.screeningBudget,r.variationBound);return r;
}
}
