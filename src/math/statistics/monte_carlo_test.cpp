// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/statistics/monte_carlo_test.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iterator>

namespace aster::numeric {
namespace {
void checkFinite(const std::vector<double>& values) {
    if(values.empty() || std::any_of(values.begin(),values.end(),[](double v){return !std::isfinite(v);}))
        throw std::invalid_argument("Monte Carlo statistics must be nonempty and finite");
}
double upperTailUnchecked(const std::vector<double>& sorted,double observed) {
    const double tolerance=100*std::numeric_limits<double>::epsilon()*std::abs(observed);
    const auto first=std::lower_bound(sorted.begin(),sorted.end(),observed-tolerance);
    return (1.+std::distance(first,sorted.end()))/(1.+sorted.size());
}
}
double empiricalUpperTailP(const std::vector<double>& sorted,double observed) {
    checkFinite(sorted);
    if(!std::isfinite(observed) || !std::is_sorted(sorted.begin(),sorted.end()))
        throw std::invalid_argument("Monte Carlo reference must be sorted and observation finite");
    return upperTailUnchecked(sorted,observed);
}
MonteCarloMaximumResult monteCarloMaximumTest(const std::vector<double>& observed,
    size_t simulations,double alpha,const std::function<std::vector<double>(size_t)>& generate,
    const std::function<bool()>& cancelled) {
    checkFinite(observed);
    if(!generate || simulations<1 || simulations>1000000 || !std::isfinite(alpha) || alpha<=0 || alpha>=1)
        throw std::invalid_argument("Invalid Monte Carlo count, alpha or callback");
    // Deliberately use the attained discrete level, rather than rounding alpha up.
    auto rank=static_cast<size_t>(std::floor(alpha*(simulations+1.)));
    // Match the actual floating-point p<=alpha decision without an epsilon
    // that could admit a level just BELOW a reachable rank probability.
    while(rank<simulations && (rank+1.)/(simulations+1.)<=alpha)++rank;
    while(rank>0 && double(rank)/(simulations+1.)>alpha)--rank;
    if(rank<1)throw std::invalid_argument("Monte Carlo resolution requires 1/(B+1) <= alpha");
    auto stop=[&]{if(cancelled && cancelled())throw std::runtime_error("Cancellation requested");};
    stop();
    MonteCarloMaximumResult result;
    result.observedMaximum=*std::max_element(observed.begin(),observed.end());
    result.nullMaxima.reserve(simulations);
    for(size_t j=0;j<simulations;++j) {
        stop();const auto values=generate(j);stop();
        if(values.size()!=observed.size())throw std::runtime_error("Monte Carlo test family changed during replay");
        checkFinite(values);
        result.nullMaxima.push_back(*std::max_element(values.begin(),values.end()));
    }
    auto sorted=result.nullMaxima;std::sort(sorted.begin(),sorted.end());
    result.globalP=upperTailUnchecked(sorted,result.observedMaximum);
    result.criticalValue=sorted[sorted.size()-rank];
    result.adjustedP.reserve(observed.size());
    for(double value:observed)result.adjustedP.push_back(upperTailUnchecked(sorted,value));
    return result;
}
}
