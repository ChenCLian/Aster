// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/statistics/mtm_simulation.h"
#include "math/statistics/monte_carlo_test.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace aster::numeric {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double fitRhoLimit = .999999;
constexpr int fitGridIntervals = 128;
void stopIf(const std::function<bool()>& cancelled)
{ if (cancelled && cancelled()) throw std::runtime_error("Cancellation requested"); }
// Each task owns its generator. SplitMix64/Box-Muller also specifies the stream
// independently of the standard library's normal_distribution implementation.
class NormalStream {
    std::uint64_t state;
    double uniform() {
        auto z = (state += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return ((z ^ (z >> 31)) >> 11) * 0x1.0p-53 + 0x1.0p-54;
    }
public:
    explicit NormalStream(std::uint64_t seed) : state(seed) {}
    double normal() {
        const double u = uniform(), v = uniform(); // Evaluation order is explicit.
        return std::sqrt(-2 * std::log(u)) * std::cos(2 * pi * v);
    }
};
double maximumRatio(const MtmSpectrum& spectrum, const std::vector<double>& base,
                    const std::vector<size_t>& tested)
{
    double maximum = 0;
    for (auto i : tested) {
        const double ratio = spectrum.power[i] / base[i];
        if (!std::isfinite(ratio)) throw std::runtime_error("Non-finite spectral statistic");
        maximum = std::max(maximum, ratio);
    }
    return maximum;
}
}

std::vector<double> stationaryAr1Background(const MtmSpectrum& spectrum, const Ar1LikelihoodFit& fit, double fs)
{
    if(spectrum.frequency.empty() || spectrum.densityScale.size()!=spectrum.frequency.size() ||
       !std::isfinite(fs) || fs<=0 || !std::isfinite(fit.rho) || std::abs(fit.rho)>=1 ||
       !std::isfinite(fit.innovationVariance) || fit.innovationVariance<=0)
        throw std::invalid_argument("Invalid AR1 background grid or model");
    std::vector<double> values;
    for (size_t i = 0; i < spectrum.frequency.size(); ++i) {
        const double s = std::sin(pi * spectrum.frequency[i] / fs);
        const double denominator = (1-fit.rho)*(1-fit.rho) + 4*fit.rho*s*s;
        const double value = (fit.innovationVariance / fs) / denominator * spectrum.densityScale[i];
        if (!(value > 0) || !std::isfinite(value))
            throw std::runtime_error("AR1 background is outside numerical range; rescale the signal");
        values.push_back(value);
    }
    return values;
}
Ar1LikelihoodFit fitStationaryAr1(const std::vector<double>& y, bool estimateRho, double fixedRho)
{
    if (y.size() < 4 || (!estimateRho && (!std::isfinite(fixedRho) || std::abs(fixedRho) >= 1)))
        throw std::invalid_argument("AR1 fit requires at least four samples and |rho| < 1");
    long double center = 0, scale = 0;
    for (double v : y) {
        if (!std::isfinite(v)) throw std::invalid_argument("Non-finite AR1 input");
        center += static_cast<long double>(v) / y.size();
    }
    for (double v : y) scale = std::max(scale, std::abs(static_cast<long double>(v)-center));
    if (!(scale > 0)) throw std::invalid_argument("A constant signal has no estimable noise variance");
    const auto n = y.size();
    std::vector<long double> z(n);
    for (size_t i = 0; i < n; ++i) z[i] = (static_cast<long double>(y[i])-center)/scale;
    long double a = 0, interiorSquares = 0, lag = 0, interior = 0;
    for (size_t i = 0; i < n; ++i) {
        a += z[i]*z[i];
        if (i > 0) lag += z[i]*z[i-1];
        if (i > 0 && i+1 < n) { interiorSquares += z[i]*z[i]; interior += z[i]; }
    }
    const long double ends = z.front()+z.back();
    const auto objective = [&](double rho) {
        const long double r = rho, numerator = ends+(1-r)*interior;
        const long double sse = a+r*r*interiorSquares-2*r*lag
            -(1-r)*numerator*numerator/(2+(n-2)*(1-r));
        return sse > 0 ? static_cast<double>(n*std::log(sse/n)-std::log1p(-r*r))
                       : std::numeric_limits<double>::infinity();
    };
    double rho = fixedRho;
    if (estimateRho) {
        double grid[fitGridIntervals+1], value[fitGridIntervals+1];
        const double edge = std::atanh(fitRhoLimit);
        int best = 0;
        for (int j = 0; j <= fitGridIntervals; ++j) {
            grid[j] = std::tanh(-edge+2*edge*j/fitGridIntervals);
            value[j] = objective(grid[j]);
            if (value[j] < value[best]) best = j;
        }
        double bestValue = value[best]; rho = grid[best];
        // Refine each grid-local minimum, not just a single initial guess.
        for (int j = 1; j < fitGridIntervals; ++j) {
            if (value[j] > value[j-1] || value[j] > value[j+1]) continue;
            double left = grid[j-1], right = grid[j+1];
            for (int iteration = 0; iteration < 64; ++iteration) {
                const double p = left+(right-left)/3, q = right-(right-left)/3;
                if (objective(p) < objective(q)) right = q; else left = p;
            }
            const double candidate = (left+right)/2, score = objective(candidate);
            if (score < bestValue) { bestValue = score; rho = candidate; }
        }
        if (!std::isfinite(bestValue) || std::abs(rho) >= fitRhoLimit-1e-9)
            throw std::runtime_error("AR1 likelihood reached its stationary search boundary; no test was produced");
    }
    // Recompute residual squares directly to avoid cancellation near |rho|=1.
    const long double r = rho, mu = (ends+(1-r)*interior)/(2+(n-2)*(1-r));
    long double sse = (1-r)*(1+r)*(z.front()-mu)*(z.front()-mu);
    for (size_t i = 1; i < n; ++i) {
        const long double residual = (z[i]-mu)-r*(z[i-1]-mu);
        sse += residual*residual;
    }
    Ar1LikelihoodFit fit{rho, static_cast<double>(center+scale*mu), static_cast<double>(sse/n*scale*scale)};
    if (!std::isfinite(fit.mean) || !(fit.innovationVariance > 0) || !std::isfinite(fit.innovationVariance))
        throw std::runtime_error("AR1 variance is outside numerical range; rescale the signal");
    return fit;
}

MtmSimulationResult simulateMtmAr1(const std::vector<double>& y, double fs,
    const MtmSimulationOptions& o, const std::function<bool()>& cancelled)
{
    // This is an explicitly budgeted experimental method, not the large-data PSD path.
    if (y.size() < 32 || y.size() > 16384 || o.nw < 1 || o.nw > 32 || o.nw >= y.size()/2.0 ||
        o.simulations < 199 || o.simulations > 9999 || !std::isfinite(o.fmaxRatio) ||
        o.fmaxRatio <= 0 || o.fmaxRatio > 1 || !std::isfinite(o.alpha) || o.alpha <= 0 || o.alpha >= 1 ||
        !std::isfinite(fs) || fs <= 0 || !std::isfinite(o.fixedRho) || std::abs(o.fixedRho) >= 1)
        throw std::invalid_argument("Invalid AR1 simulation options; sample budget is 32..16384, replicates 199..9999");
    if (1./(o.simulations+1.) > o.alpha)
        throw std::invalid_argument("Too few simulations for alpha; require 1/(B+1) <= alpha");
    stopIf(cancelled);
    MtmSimulationResult result;
    result.backgroundFit = fitStationaryAr1(y, o.estimateRho, o.fixedRho);
    const auto compute = [&](const std::vector<double>& values) {
        auto s = mtmSpectrum(values, fs, o.nw, 2*o.nw-1, o.fmaxRatio*fs/2, cancelled);
        if (!s.converged) throw std::runtime_error("Adaptive MTM did not converge; simulation test is unavailable");
        return s;
    };
    result.spectrum = compute(y);
    result.background = stationaryAr1Background(result.spectrum, result.backgroundFit, fs);
    for (size_t i = 0; i < result.spectrum.frequency.size(); ++i)
        if (result.spectrum.densityScale[i] == 2) result.testedBins.push_back(i);
    if (result.testedBins.empty()) throw std::invalid_argument("The chosen frequency range contains no testable bins");
    result.maximumRatio = maximumRatio(result.spectrum, result.background, result.testedBins);
    NormalStream random(o.seed);
    std::vector<double> simulated(y.size());
    const double rho = result.backgroundFit.rho, innovation = std::sqrt((1-rho)*(1+rho));
    std::vector<double> observed;
    for(auto i:result.testedBins)observed.push_back(result.spectrum.power[i]/result.background[i]);
    const auto calibrated=monteCarloMaximumTest(observed,o.simulations,o.alpha,[&](size_t) {
        stopIf(cancelled);
        simulated.front() = random.normal(); // Exact stationary initial distribution.
        for (size_t i = 1; i < simulated.size(); ++i) simulated[i] = rho*simulated[i-1]+innovation*random.normal();
        // Refit EVERY unknown parameter on EACH simulation. Do not discard or
        // retry failed simulations: doing so conditions and biases the null law.
        const auto fit = fitStationaryAr1(simulated, o.estimateRho, o.fixedRho);
        const auto spectrum = compute(simulated);
        const auto base=stationaryAr1Background(spectrum,fit,fs);
        std::vector<double> statistics;
        for(auto i:result.testedBins)statistics.push_back(spectrum.power[i]/base[i]);
        return statistics;
    },cancelled);
    result.nullMaxima=calibrated.nullMaxima;
    result.globalP=calibrated.globalP;
    result.criticalRatio=calibrated.criticalValue;
    result.adjustedP.assign(result.spectrum.power.size(), 1.0);
    for(size_t j=0;j<result.testedBins.size();++j)result.adjustedP[result.testedBins[j]]=calibrated.adjustedP[j];
    return result;
}
}
