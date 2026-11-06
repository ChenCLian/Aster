// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/mtm_spectrum.h"
#include "math/numeric_utils.h"
#include "math/backend/numeric_backend.h"
#include <utility>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace aster::numeric {
namespace {
constexpr double pi = 3.14159265358979323846;
void stopIf(const std::function<bool()>& cancelled)
{ if (cancelled && cancelled()) throw std::runtime_error("Cancellation requested"); }
std::vector<double> autocorrelation(const std::vector<double>& tapers, size_t n, int k)
{
    const auto pad = next_fast_len(2 * n - 1);
    std::vector<double> input(pad * k, 0.0);
    for (int j = 0; j < k; ++j) std::copy_n(tapers.data() + j * n, n, input.data() + j * pad);
    auto spectrum = backend::realFft(std::move(input), pad, k);
    for (auto& value : spectrum) value = {std::norm(value), 0.0};
    const auto result = backend::inverseRealFft(std::move(spectrum), pad, k);
    std::vector<double> compact(n * k);
    for (int j = 0; j < k; ++j) std::copy_n(result.data() + j * pad, n, compact.data() + j * n);
    return compact;
}
}

std::shared_ptr<const MtmTaperPlan> makeMtmTaperPlan(size_t n,double nw,int k,
    const std::function<bool()>& cancelled)
{
    if(n<4 || n>1000000 || !std::isfinite(nw) || nw<=0 || nw>=n/2.0 || k<1 ||
       static_cast<size_t>(k)>n || n*static_cast<size_t>(k)>16000000)
        throw std::invalid_argument("Invalid MTM taper plan dimensions");
    const auto pad=next_fast_len(2*n-1);
    if(static_cast<long double>(k)*(24.L*pad+16.L*n)>512.L*1024*1024)
        throw std::invalid_argument("MTM taper plan workspace exceeds 512 MiB");
    stopIf(cancelled);
        const double w = nw / n;
        std::vector<double> diagonal(n), off(n - 1);
        for (size_t i = 0; i < n; ++i) {
            const double t = (static_cast<double>(n) - 1 - 2.0 * i) / 2;
            diagonal[i] = t * t * std::cos(2 * pi * w);
            if (i + 1 < n) off[i] = (i + 1.0) * (n - i - 1.0) / 2;
        }
        auto eigenpairs = backend::largestTridiagonalEigenpairs(std::move(diagonal), std::move(off), k);
        auto tapers = std::move(eigenpairs.vectors);
        for (int j = 0; j < k / 2; ++j)
            std::swap_ranges(tapers.begin() + j * n, tapers.begin() + (j + 1) * n, tapers.begin() + (k - 1 - j) * n);
        stopIf(cancelled);
        const auto corr = autocorrelation(tapers, n, k);
        std::vector<double> ratios(k);
        for (int j = 0; j < k; ++j) {
            long double ratio = corr[j * n] * 2 * w;
            for (size_t i = 1; i < n; ++i) ratio += corr[j * n + i] * 2 * std::sin(2 * pi * w * i) / (pi * i);
            ratios[j] = std::clamp(static_cast<double>(ratio), 0.0, 1.0);
        }
    auto plan=std::shared_ptr<MtmTaperPlan>(new MtmTaperPlan(n,nw,k));
    plan->m_tapers=std::move(tapers);plan->m_ratios=std::move(ratios);
    return plan;
}

MtmSpectrum mtmSpectrum(const std::vector<double>& y, double fs, double nw, int k, double fmax,
                        const std::function<bool()>& cancelled, size_t fftLength,
                        MtmConvergenceDomain convergenceDomain, std::shared_ptr<const MtmTaperPlan> prepared)
{
    const size_t n = y.size();
    if(prepared && (prepared->samples()!=n || prepared->bandwidth()!=nw || prepared->count()!=k))
        throw std::invalid_argument("MTM taper plan does not match N/NW/K");
    if (n < 4 || !std::isfinite(fs) || fs <= 0 || !std::isfinite(nw) || nw <= 0 || nw >= n / 2.0 ||
        k < 1 || static_cast<size_t>(k) > n || !std::isfinite(fmax) || fmax <= 0 || fmax > fs / 2.0)
        throw std::invalid_argument("Invalid MTM parameters");
    if (n > 1000000 || n * static_cast<size_t>(k) > 16000000)
        throw std::invalid_argument("MTM workspace limit exceeded");
    // The legacy FFT helper pads by 4x (also for autocorrelation). Account
    // for all simultaneous buffers instead of limiting only the DPSS array.
    const auto nfft = fftLength ? fftLength : next_fast_len(n);
    if (nfft < n || nfft > 4000000)
        throw std::invalid_argument("MTM FFT length must be between the input length and 4000000; wrapping is not supported");
    const auto paddedCorrelation = next_fast_len(2 * n - 1);
    const auto workspaceBytes = static_cast<long double>(k) *
        (24.L * paddedCorrelation + 16.L * n + 32.L * nfft) + 48.L * (n+nfft);
    if (workspaceBytes > 512.L * 1024 * 1024)
        throw std::invalid_argument("MTM estimated workspace exceeds 512 MiB");
    for (double v : y) if (!std::isfinite(v)) throw std::invalid_argument("Non-finite MTM input");
    stopIf(cancelled);
    long double mean = 0;
    // Exactly constant finite inputs must stay exactly zero after centering.
    // Repeated v/N summation otherwise leaves roundoff-sized false peaks.
    const bool constant = std::all_of(y.begin(), y.end(), [&](double v) { return v == y.front(); });
    if (constant) mean = y.front();
    else for (double v : y) mean += static_cast<long double>(v) / n;
    std::vector<double> centered(n);
    long double varianceAcc = 0;
    for (size_t i = 0; i < n; ++i) {
        centered[i] = static_cast<double>(static_cast<long double>(y[i]) - mean);
        varianceAcc += static_cast<long double>(centered[i]) * centered[i] / n;
    }
    const double variance = static_cast<double>(varianceAcc);
    if (!std::isfinite(variance)) throw std::runtime_error("MTM variance overflow");
    if (variance == 0 && std::any_of(centered.begin(),centered.end(),[](double v) { return v != 0; }))
        throw std::runtime_error("MTM variance underflow; rescale the signal before analysis");
    const auto bins = nfft / 2 + 1;
    std::vector<double> power(bins, 0.0), dof(bins, 2.0*k);
    MtmSpectrum output;
    if (variance > 0) {
        const auto plan=prepared ? prepared : makeMtmTaperPlan(n,nw,k,cancelled);
        const auto& tapers=plan->tapers();
        const auto& ratios=plan->concentration();
        std::vector<double> tapered(nfft * k, 0.0);
        for (int j = 0; j < k; ++j)
            for (size_t i = 0; i < n; ++i) tapered[j * nfft + i] = tapers[j * n + i] * centered[i];
        const auto ft = backend::realFft(std::move(tapered), nfft, k);
        std::vector<double> individual(k * bins);
        for (int j = 0; j < k; ++j) for (size_t i = 0; i < bins; ++i) {
            const auto value = ft[j * bins + i];
            // Adaptive leakage uses the two-sided variance convention. Convert to
            // a one-sided density only AFTER the weighted estimate is complete.
            individual[j * bins + i] = std::norm(value);
            if (!std::isfinite(individual[j * bins + i]))
                throw std::runtime_error("MTM eigenspectrum overflow; rescale the signal before analysis");
        }
        for (size_t i = 0; i < bins; ++i) power[i] = k > 1 ? 0.5 * (individual[i] + individual[bins + i]) : individual[i];
        std::vector<double> next(bins);
        output.converged = k == 1;
        for (int iteration = 0; iteration < (k > 1 ? 1000 : 0); ++iteration) {
            stopIf(cancelled);
            long double difference = 0;
            for (size_t i = 0; i < bins; ++i) {
                long double weighted = 0, weights = 0;
                for (int j = 0; j < k; ++j) {
                    const double denominator = ratios[j] * power[i] + variance * (1 - ratios[j]);
                    const double d = denominator > 0 ? power[i] / denominator : 0;
                    const double weight = d * d * ratios[j];
                    weighted += weight * individual[j * bins + i]; weights += weight;
                }
                next[i] = weights > 0 ? static_cast<double>(weighted / weights) : 0.0;
                // Interior real-FFT bins represent both +/-f. DC and an
                // even-length Nyquist bin occur only once in the full norm.
                const bool paired = i > 0 && !(nfft % 2 == 0 && i == nfft/2);
                const int multiplicity = convergenceDomain == MtmConvergenceDomain::FullSpectrum && paired ? 2 : 1;
                difference += multiplicity * std::abs(next[i] - power[i]);
            }
            power.swap(next); output.iterations = iteration + 1;
            if (difference < 0.0005 * variance) { output.converged = true; break; }
        }
        // Satterthwaite effective degrees of freedom for the final taper weights.
        // The chi-square approximation does not include background-fit uncertainty.
        for (size_t i = 0; i < bins; ++i) {
            long double sum=0, squares=0;
            for (int j=0;j<k;++j) {
                const auto denominator=ratios[j]*power[i]+variance*(1-ratios[j]);
                const auto d=denominator>0 ? power[i]/denominator : 0;
                const auto weight=d*d*ratios[j];
                sum+=weight; squares+=static_cast<long double>(weight)*weight;
            }
            if (squares>0) dof[i]=static_cast<double>(2*sum*sum/squares);
        }
    }
    const double step = fs / nfft;
    for (size_t i = 0; i < bins && i * step <= fmax + step * 1e-10; ++i) {
        const bool realEndpoint = i == 0 || (nfft % 2 == 0 && i + 1 == bins);
        const auto factor = realEndpoint ? 1.0 : 2.0;
        output.frequency.push_back(i * step); output.power.push_back((power[i] / fs) * factor);
        output.degreesOfFreedom.push_back(realEndpoint ? .5*dof[i] : dof[i]);
        output.densityScale.push_back(factor);
    }
    return output;
}
} // namespace aster::numeric
