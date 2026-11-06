// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/statistics/significance.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <gsl/gsl_cdf.h>
#include <iterator>
#include <utility>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::array<double, 4> qLevels{.05, .01, .001, .0001};
double harmonic(size_t n) { double h = 0; for (size_t i = 1; i <= n; ++i) h += 1.0 / i; return h; }
double at(const std::vector<double>& v, size_t i, double fallback) { return v.empty() ? fallback : v[i]; }
void validateInference(const SpectrumInference& opt, size_t n)
{
    for (const auto* v : {&opt.degreesOfFreedom, &opt.densityScale}) {
        if (!v->empty() && v->size() != n) throw std::invalid_argument("Spectral inference size mismatch");
        for (double x : *v) if (!std::isfinite(x) || x <= 0)
            throw std::invalid_argument("Spectral inference requires positive finite scales and degrees of freedom");
    }
    if (!opt.tested.empty() && opt.tested.size() != n) throw std::invalid_argument("FDR mask size mismatch");
}
ChiConf chiCurves(const std::vector<double>& background, int fallbackDof, const SpectrumInference& opt)
{
    ChiConf c;
    const std::array<double, 4> tails{.1, .05, .01, .001};
    const std::array<std::vector<double>*, 4> curves{&c.chi90, &c.chi95, &c.chi99, &c.chi999};
    for (size_t j = 0; j < curves.size(); ++j) {
        auto& curve = *curves[j]; curve.reserve(background.size());
        for (size_t i = 0; i < background.size(); ++i) {
            const auto nu = at(opt.degreesOfFreedom, i, fallbackDof);
            curve.push_back(background[i] * (gsl_cdf_chisq_Qinv(tails[j], nu) / nu));
        }
    }
    return c;
}
// Two balanced halves give exact medians, including even windows at the edges.
// Updating and querying the rolling median costs O(log width), not O(width).
std::vector<double> median(const std::vector<double>& values, size_t width)
{
    const size_t n = values.size(), half = width / 2;
    const size_t behind = width % 2 ? half : half-1;
    std::multiset<double> low, high;
    auto balance = [&] {
        while (low.size() > high.size() + 1) { auto it = std::prev(low.end()); high.insert(*it); low.erase(it); }
        while (low.size() < high.size()) { auto it = high.begin(); low.insert(*it); high.erase(it); }
    };
    std::vector<double> result(n); size_t left = 0, right = 0;
    for (size_t i = 0; i < n; ++i) {
        const auto end = std::min(n, i + half + 1), begin = i > behind ? i - behind : 0;
        while (right < end) {
            const auto x = values[right++];
            if (low.empty() || x <= *low.rbegin()) low.insert(x); else high.insert(x);
            balance();
        }
        while (left < begin) {
            const auto x = values[left++]; auto it = low.find(x);
            if (it != low.end()) low.erase(it); else high.erase(high.find(x));
            balance();
        }
        result[i] = low.size() == high.size() ? .5 * *low.rbegin() + .5 * *high.begin() : *low.rbegin();
    }
    return result;
}
struct Ar1Fit { double scale, rho; };
Ar1Fit fitAr1(const double* f, const std::vector<double>& p, double nyquist, bool logfit)
{
    // Profile out the amplitude analytically; optimize only rho=tanh(theta).
    // Normalization keeps the objective independent of the physical power unit.
    const auto maximum = *std::max_element(p.begin(), p.end());
    if (!(maximum > 0)) throw std::invalid_argument("AR1 requires nonzero power");
    std::vector<double> y(p.size()), sine2(p.size()), cosine2(p.size());
    for (size_t i = 0; i < p.size(); ++i) {
        if (logfit && p[i] <= 0) throw std::invalid_argument("Logarithmic AR1 fit requires positive power");
        y[i] = logfit ? std::log(p[i]) - std::log(maximum) : p[i] / maximum;
        const double angle = .5 * pi * f[i] / nyquist;
        sine2[i] = std::pow(std::sin(angle), 2); cosine2[i] = std::pow(std::cos(angle), 2);
    }
    auto shape = [&](size_t i, double rho) {
        // Avoid cancellation for |rho| close to 1 at either endpoint.
        const auto den = rho >= 0 ? (1-rho)*(1-rho) + 4*rho*sine2[i]
                                  : (1+rho)*(1+rho) - 4*rho*cosine2[i];
        return (1-rho)*(1+rho) / den;
    };
    auto evaluate = [&](double theta, double* amplitude = nullptr) {
        const auto rho = std::tanh(theta);
        long double xy = 0, xx = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            const auto g = shape(i, rho);
            if (logfit) xy += y[i] - std::log(g);
            else { xy += static_cast<long double>(y[i]) * g; xx += static_cast<long double>(g) * g; }
        }
        const double a = logfit ? static_cast<double>(xy / p.size()) : static_cast<double>(xy / xx);
        long double error = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            const auto g = shape(i, rho);
            const auto r = logfit ? y[i] - a - std::log(g) : y[i] - a * g;
            error += static_cast<long double>(r) * r;
        }
        if (amplitude) *amplitude = logfit ? std::exp(a + std::log(maximum)) : a * maximum;
        return static_cast<double>(error / p.size());
    };
    constexpr int grid = 128;
    const auto bound = std::atanh(1.0 - 1e-9), step = 2 * bound / grid;
    double bestTheta = 0, best = evaluate(0);
    int index = grid / 2;
    std::vector<double> errors(grid + 1);
    for (int j = 0; j <= grid; ++j) {
        errors[j] = evaluate(-bound + j * step);
        if (errors[j] < best) { best = errors[j]; bestTheta = -bound + j * step; index = j; }
    }
    // Refine every bracketed local minimum, rather than trusting an initial rho.
    for (int j = 1; j < grid; ++j) {
        if (!(errors[j] <= errors[j-1] && errors[j] <= errors[j+1])) continue;
        double lo = -bound + (j-1)*step, hi = -bound + (j+1)*step;
        constexpr double golden = .6180339887498948482;
        double a = hi-golden*(hi-lo), b = lo+golden*(hi-lo), fa = evaluate(a), fb = evaluate(b);
        for (int iteration = 0; iteration < 64; ++iteration) {
            if (fa < fb) { hi=b; b=a; fb=fa; a=hi-golden*(hi-lo); fa=evaluate(a); }
            else { lo=a; a=b; fa=fb; b=lo+golden*(hi-lo); fb=evaluate(b); }
        }
        const auto theta = fa < fb ? a : b, error = std::min(fa, fb);
        if (error < best) { best=error; bestTheta=theta; index=j; }
    }
    if (index == 0 || index == grid) throw std::runtime_error("AR1 optimum reached the stationarity boundary; background model may be unsuitable");
    double scale = 0; evaluate(bestTheta, &scale);
    if (!std::isfinite(scale) || scale <= 0) throw std::runtime_error("AR1 fitted scale is not representable");
    return {scale, std::tanh(bestTheta)};
}
} // namespace

RedNoiseConf computeRedNoiseConfidence(const double* pxx, const double* ft, double smoothwin,
                                       double nyquist, size_t n, double nw, bool logfit,
                                       const SpectrumInference& inference)
{
    if (!pxx || !ft || n < 3 || !std::isfinite(nw) || nw < 1 || nw > 1000000 || !std::isfinite(nyquist) || nyquist <= 0 ||
        !std::isfinite(smoothwin) || smoothwin <= 0 || smoothwin > 1)
        throw std::invalid_argument("AR1 requires at least three bins, positive Nyquist and a window fraction in (0,1]");
    validateInference(inference, n);
    std::vector<double> normalized(n);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(pxx[i]) || pxx[i] < 0 || !std::isfinite(ft[i]) || ft[i] < 0 ||
            ft[i] > nyquist * (1+1e-12) || (i && !(ft[i] > ft[i-1])))
            throw std::invalid_argument("AR1 requires finite nonnegative power and increasing frequencies within Nyquist");
        normalized[i] = pxx[i] / at(inference.densityScale, i, 1);
    }
    auto width = static_cast<size_t>(std::round(smoothwin * n));
    if (inference.acycle && width < 1)
        throw std::invalid_argument("Too few frequency bins for the requested median smoothing fraction");
    width = std::max(size_t(1), width);
    if (!inference.acycle && width % 2 == 0) ++width;
    auto smooth = median(normalized, width);
    const auto fit = fitAr1(ft, smooth, nyquist, logfit);
    std::vector<double> background(n);
    for (size_t i = 0; i < n; ++i) {
        const auto rho = fit.rho, angle = .5*pi*ft[i]/nyquist;
        const auto den = rho >= 0 ? (1-rho)*(1-rho)+4*rho*std::pow(std::sin(angle),2)
                                  : (1+rho)*(1+rho)-4*rho*std::pow(std::cos(angle),2);
        background[i] = fit.scale * ((1-rho)*(1+rho)/den) * at(inference.densityScale,i,1);
        smooth[i] *= at(inference.densityScale,i,1);
    }
    const int tapers = nw == 1 ? 2 : int(std::lround(2*nw))-1;
    auto chi = chiCurves(background, 2*tapers, inference);
    return {std::move(smooth), std::move(background), std::move(chi), fit.scale, fit.rho};
}

std::array<double, 4> spectralFdrCutoffs(const std::vector<double>& pValues)
{
    std::array<double, 4> result; result.fill(std::numeric_limits<double>::quiet_NaN());
    if (pValues.empty()) return result;
    auto sorted = pValues;
    for (double p : sorted) if (!std::isfinite(p) || p < 0 || p > 1) throw std::invalid_argument("Invalid FDR p-value");
    std::sort(sorted.begin(), sorted.end());
    const auto m = sorted.size(); const auto correction = harmonic(m);
    for (size_t q = 0; q < qLevels.size(); ++q)
        for (size_t j = 1; j <= m; ++j) {
            const auto critical = qLevels[q] * (static_cast<double>(j) / m) / correction;
            if (sorted[j-1] <= critical) result[q] = critical;
        }
    return result;
}

std::array<double, 4> acycleFdrCutoffs(const std::vector<double>& pValues, double nw)
{
    if (!std::isfinite(nw) || nw < 1 || nw > 1000000)
        throw std::invalid_argument("Invalid Acycle FDR bandwidth");
    std::array<double, 4> result; result.fill(std::numeric_limits<double>::quiet_NaN());
    if (pValues.empty()) return result;
    auto sorted = pValues;
    for (double p : sorted) if (!std::isfinite(p) || p < 0 || p > 1)
        throw std::invalid_argument("Invalid FDR p-value");
    std::sort(sorted.begin(), sorted.end());
    const double correction = harmonic(static_cast<size_t>(nw));
    for (size_t q=0; q<qLevels.size(); ++q)
        for (size_t j=1; j<=sorted.size(); ++j)
            if (sorted[j-1] < qLevels[q]*double(j)/sorted.size()/correction)
                result[q] = sorted[j-1];
    return result;
}

std::vector<double> estimateSwaBackground(const std::vector<double>& power,
    const std::vector<double>& freq,const SpectrumInference& inference,SwaDiagnostics* diagnostics)
{
    if(power.empty() || power.size()!=freq.size())throw std::invalid_argument("Invalid SWA input");
    validateInference(inference, power.size());
    if (inference.acycle && power.size() < 33)
        throw std::invalid_argument("SWA requires at least 33 frequency bins (three 11-bin windows)");
    double maxLog = -std::numeric_limits<double>::infinity();
    std::vector<double> logPower(power.size());
    for (size_t i = 0; i < power.size(); ++i) {
        if (!std::isfinite(power[i]) || power[i] < 0) throw std::invalid_argument("SWA requires finite nonnegative power");
        logPower[i] = power[i] > 0 ? std::log10(power[i]) - std::log10(at(inference.densityScale,i,1))
                                 : -std::numeric_limits<double>::infinity();
        maxLog = std::max(maxLog, logPower[i]);
    }
    if (!std::isfinite(maxLog)) throw std::invalid_argument("SWA requires nonzero power");
    // Only exact zeros need a floor. Centering logs also preserves unit invariance.
    // The relative floor is 300 decades below the largest bin; positive bins are never clipped.
    for (auto& p : logPower) p = std::isfinite(p) ? p-maxLog : -300;
    auto normalizedBackground = specswa(freq, logPower, diagnostics);
    std::vector<double> background(power.size());
    for (size_t i = 0; i < power.size(); ++i) {
        const auto logBackground = std::log10(normalizedBackground[i]) + maxLog + std::log10(at(inference.densityScale,i,1));
        background[i] = std::pow(10.0, logBackground);
        if (!std::isfinite(background[i]) || background[i] <= 0) throw std::runtime_error("SWA background is not representable");
    }
    return background;
}

FDRSpectralAnalysisResult FDR_TEST(const std::vector<double>& power, const std::vector<double>& freq,
                                  double nw, const SpectrumInference& inference)
{
    if (power.empty() || power.size() != freq.size() || !std::isfinite(nw) || nw < 1 || nw > 1000000)
        throw std::invalid_argument("Invalid SWA/FDR input");
    const int dof = 2*(nw == 1 ? 2 : int(std::lround(2*nw))-1);
    FDRSpectralAnalysisResult result;
    result.swa=estimateSwaBackground(power,freq,inference,&result.diagnostics);
    std::vector<double> pValues;
    for(size_t i=0;i<power.size();++i) {
        if (!inference.tested.empty() && !inference.tested[i]) continue;
        const auto nu = at(inference.degreesOfFreedom,i,dof), statistic = (power[i]/result.swa[i])*nu;
        // Q computes the small upper tail directly. Overflow means overwhelming power, not p=1.
        pValues.push_back(std::isinf(statistic) ? 0 : gsl_cdf_chisq_Q(statistic,nu));
    }
    result.testCount = pValues.size();
    result.dependenceCorrection = harmonic(inference.acycle ? static_cast<size_t>(nw) : pValues.size());
    const auto cutoffs = inference.acycle ? acycleFdrCutoffs(pValues,nw) : spectralFdrCutoffs(pValues);
    const std::array<std::vector<double>*,4> curves{&result.fdrConf.clfdr5,&result.fdrConf.clfdr1,&result.fdrConf.clfdr01,&result.fdrConf.clfdr001};
    for (size_t q = 0; q < cutoffs.size(); ++q) {
        if (std::isnan(cutoffs[q])) continue; // no discoveries, not a failed calculation
        for (size_t i = 0; i < power.size(); ++i) {
            const auto nu = at(inference.degreesOfFreedom,i,dof);
            const auto threshold = result.swa[i] * (gsl_cdf_chisq_Qinv(cutoffs[q],nu)/nu);
            if (!std::isfinite(threshold)) throw std::runtime_error("FDR threshold is not representable");
            curves[q]->push_back(threshold);
        }
    }
    result.chiConf = chiCurves(result.swa,dof,inference);
    return result;
}
