// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/statistics/ar1_nuisance.h"
#include "math/statistics/monte_carlo_test.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <iterator>

namespace aster::numeric {
namespace {
void checkRho(double r) {
    if(!std::isfinite(r) || std::abs(r)>=1)
        throw std::invalid_argument("AR1 correlation must be finite and strictly inside (-1,1)");
}
void checkProbability(double p) {
    if(!std::isfinite(p) || p<0 || p>1)
        throw std::invalid_argument("Invalid component probability in composite test");
}
}
double ar1TotalVariationBound(std::size_t n,double rho,double reference) {
    checkRho(rho);checkRho(reference);
    if(n<2 || n>1000000)throw std::invalid_argument("AR1 bound requires 2..1000000 samples");
    if(rho==reference)return 0;
    const long double r=rho,s=reference,d=r-s,v=(1-s)*(1+s);
    const long double u=(d*(2*s+d))/v;
    // KL/(n-1)/0.5 = -log(1-u)-u + d^2/v. The expression avoids
    // subtracting nearly equal first-order terms when cells are very narrow.
    long double remainder;
    if(std::abs(u)<0.01L) {
        long double term=u*u;remainder=term/2;
        for(int k=3;k<=18;++k){term*=u;remainder+=term/k;}
    } else remainder=-std::log1p(-u)-u;
    long double kl=(n-1)*0.5L*(remainder+d*d/v);
    // Positive rounding allowance; an analytic Pinsker bound, not an interval
    // arithmetic certificate for all floating point implementations.
    if(!std::isfinite(kl) || kl<0)throw std::runtime_error("AR1 distance outside numerical range");
    const long double bound=std::sqrt(kl/2);
    return std::min(1.,std::nextafter(static_cast<double>(bound*(1+1e-12L)+1e-15L),1.));
}
std::vector<Ar1GridCell> ar1NuisanceGrid(std::size_t n,double lower,double upper,
    double maximumVariation,std::size_t maximumCells) {
    checkRho(lower);checkRho(upper);
    if(lower>=upper || !std::isfinite(maximumVariation) || maximumVariation<=0 ||
       maximumVariation>=1 || maximumCells<1 || maximumCells>1000000)
        throw std::invalid_argument("Invalid AR1 grid range, variation budget or cell budget");
    // Validate n even before subdivision.
    ar1TotalVariationBound(n,lower,upper);
    struct Interval{double lower,upper;};
    std::vector<Interval> pending{{lower,upper}};
    std::vector<Ar1GridCell> cells;
    while(!pending.empty()) {
        const auto q=pending.back();pending.pop_back();
        const double center=std::tanh((std::atanh(q.lower)+std::atanh(q.upper))/2);
        if(!(center>q.lower && center<q.upper))throw std::runtime_error("AR1 grid reached floating point resolution");
        const double bound=std::max(ar1TotalVariationBound(n,q.lower,center),
                                    ar1TotalVariationBound(n,q.upper,center));
        if(bound<=maximumVariation)cells.push_back({q.lower,center,q.upper,bound});
        else {
            if(cells.size()+pending.size()+2>maximumCells)
                throw std::invalid_argument("AR1 grid exceeds cell budget; no test was produced");
            pending.push_back({center,q.upper});pending.push_back({q.lower,center});
        }
    }
    return cells;
}
double centeredLagOne(const std::vector<double>& y) {
    if(y.size()<4)throw std::invalid_argument("Lag statistic requires four or more samples");
    long double mean=0,scale=0;
    for(double v:y){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite lag input");mean+=static_cast<long double>(v)/y.size();}
    for(double v:y)scale=std::max(scale,std::abs(static_cast<long double>(v)-mean));
    if(!(scale>0))throw std::invalid_argument("Constant input has no lag statistic");
    long double squares=0,cross=0,previous=0;
    for(std::size_t i=0;i<y.size();++i){
        const long double z=(static_cast<long double>(y[i])-mean)/scale;
        squares+=z*z;if(i)cross+=previous*z;previous=z;
    }
    return static_cast<double>(cross/squares);
}
double empiricalTwoSidedP(const std::vector<double>& reference,double observed) {
    const double upper=empiricalUpperTailP(reference,observed); // validates input
    const double tolerance=100*std::numeric_limits<double>::epsilon()*std::abs(observed);
    const auto last=std::upper_bound(reference.begin(),reference.end(),observed+tolerance);
    const double lower=(1.+std::distance(reference.begin(),last))/(1.+reference.size());
    return std::min(1.,2*std::min(upper,lower));
}
EmpiricalTailReference::EmpiricalTailReference(std::vector<double> reference):m_sorted(std::move(reference)) {
    empiricalUpperTailP(m_sorted,0); // Includes finite, nonempty and order checks.
}
double EmpiricalTailReference::upperTail(double observed) const {
    if(!std::isfinite(observed))throw std::invalid_argument("Nonfinite observed statistic");
    const double tolerance=100*std::numeric_limits<double>::epsilon()*std::abs(observed);
    const auto first=std::lower_bound(m_sorted.begin(),m_sorted.end(),observed-tolerance);
    return (1.+std::distance(first,m_sorted.end()))/(1.+m_sorted.size());
}
double EmpiricalTailReference::twoSided(double observed) const {
    const double upper=upperTail(observed);
    const double tolerance=100*std::numeric_limits<double>::epsilon()*std::abs(observed);
    const auto last=std::upper_bound(m_sorted.begin(),m_sorted.end(),observed+tolerance);
    return std::min(1.,2*std::min(upper,(1.+std::distance(m_sorted.begin(),last))/(1.+m_sorted.size())));
}
CompositeNullResult combineNuisancePValues(const std::vector<NuisancePValue>& models,
    double beta,double variationBound) {
    checkProbability(beta);checkProbability(variationBound);
    if(models.empty() || beta>=1 || beta+variationBound>=1)
        throw std::invalid_argument("Invalid composite test budget or empty parameter grid");
    CompositeNullResult r;double maximum=0;
    for(std::size_t i=0;i<models.size();++i){
        checkProbability(models[i].screeningP);checkProbability(models[i].analysisP);
        if(models[i].screeningP>beta){r.includedModels.push_back(i);maximum=std::max(maximum,models[i].analysisP);}
    }
    r.emptyConfidenceSet=r.includedModels.empty();
    if(!r.emptyConfidenceSet){r.largestAnalysisP=maximum;r.p=std::min(1.,maximum+beta+variationBound);}
    return r;
}
}
