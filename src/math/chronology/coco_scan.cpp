// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/chronology/coco_detail.h"
#include "math/backend/numeric_backend.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
namespace aster::chronology
{
namespace coco_detail
{
constexpr double pi = 3.1415926535897932384626433832795;
void check(const std::function<bool()> &c)
{
    if (c && c())
        throw CocoCancelled{};
}
void detrend(std::vector<double> &v, CocoOptions::Detrend mode)
{
    if (mode == CocoOptions::Detrend::None)
        return;
    const long double mean = std::accumulate(v.begin(), v.end(), 0.L) / v.size();
    const long double center = (v.size() - 1) / 2.L;
    long double xy = 0, xx = 0;
    if (mode == CocoOptions::Detrend::Linear)
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            const auto x = i - center;
            xy += x * (v[i] - mean);
            xx += x * x;
        }
    const long double slope = xx > 0 ? xy / xx : 0;
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = double(v[i] - mean - slope * (i - center));
}
std::vector<double> power(std::vector<double> v, std::size_t fft, CocoOptions::Detrend mode)
{
    detrend(v, mode);
    v.resize(fft, 0);
    auto ft = numeric::backend::realFft(std::move(v), fft);
    std::vector<double> p(ft.size());
    // Common PSD scaling cancels in correlation. Preserve one-sided endpoints.
    for (std::size_t i = 0; i < p.size(); ++i)
        p[i] = std::norm(ft[i]) * (i == 0 || i == fft / 2 ? 1 : 2);
    return p;
}
double correlation(const std::vector<double> &a, const std::vector<double> &b, std::size_t end)
{
    const long double ma = std::accumulate(a.begin() + 1, a.begin() + end + 1, 0.L) / end;
    const long double mb = std::accumulate(b.begin() + 1, b.begin() + end + 1, 0.L) / end;
    long double aa = 0, bb = 0, ab = 0;
    for (std::size_t i = 1; i <= end; ++i)
    {
        const auto x = a[i] - ma, y = b[i] - mb;
        aa += x * x;
        bb += y * y;
        ab += x * y;
    }
    if (!(aa > 0 && bb > 0))
        return std::numeric_limits<double>::quiet_NaN();
    return std::clamp(double(ab / std::sqrt(aa * bb)), -1., 1.);
}
void normalize(std::vector<double> &v)
{
    const auto peak = *std::max_element(v.begin(), v.end());
    if (!(peak > 0 && std::isfinite(peak)))
        throw std::domain_error("Empty/non-finite spectrum");
    for (auto &x : v)
        x /= peak;
}
CocoResult grid(std::size_t samples, double dx, const CocoOptions &o,
                const std::function<bool()> &cancelled)
{
    check(cancelled);
    if (samples < 16)
        throw std::invalid_argument("COCO requires at least 16 samples");
    if (samples > CocoLimits::maxSamples)
        throw std::length_error("COCO sample limit exceeded");
    const auto positive = [](double v) { return std::isfinite(v) && v > 0; };
    if (!positive(dx) || !positive(o.minimumRate) || !positive(o.maximumRate) ||
        !positive(o.rateStep) || o.maximumRate < o.minimumRate || !positive(o.maximumFrequency) ||
        o.paddingFactor < 1 || o.paddingFactor > 4 ||
        (o.detrend != CocoOptions::Detrend::None && o.detrend != CocoOptions::Detrend::Mean &&
         o.detrend != CocoOptions::Detrend::Linear))
        throw std::invalid_argument("Invalid COCO grid, spacing, padding or detrending");
    if (o.periodsKa.size() < 2 || o.periodsKa.size() > CocoLimits::maxTargets ||
        o.relativePower.size() != o.periodsKa.size())
        throw std::invalid_argument("Require 2 to 16 periods and the same number of powers");
    std::vector<double> frequencies;
    for (std::size_t i = 0; i < o.periodsKa.size(); ++i)
    {
        if (!positive(o.periodsKa[i]) || !positive(o.relativePower[i]) ||
            !positive(1 / o.periodsKa[i]) || 1 / o.periodsKa[i] >= o.maximumFrequency)
            throw std::invalid_argument(
                "Targets must have positive powers and frequencies inside the band");
        frequencies.push_back(1 / o.periodsKa[i]);
    }
    auto sorted = frequencies;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i] - sorted[i - 1] <= 1e-12 * sorted[i])
            throw std::invalid_argument("Duplicate target periods");
    const long double steps =
        (static_cast<long double>(o.maximumRate) - o.minimumRate) / o.rateStep;
    if (!std::isfinite(steps) || steps > CocoLimits::maxRates - 1 + 1e-9L)
        throw std::length_error("COCO rate grid limit exceeded");
    const auto count = std::size_t(std::floor(steps + 1e-12L * std::max(1.L, steps))) + 1;
    CocoResult result;
    std::size_t fft = 1;
    while (fft < samples * std::size_t(o.paddingFactor))
        fft *= 2;
    if (fft > CocoLimits::maxFft)
        throw std::length_error("COCO FFT limit exceeded");
    result.fftSize = fft;
    const double spatialNyquist = .5 / dx, spatialRayleigh = 1 / (double(samples) * dx);
    if (!positive(spatialNyquist) || !positive(spatialRayleigh))
        throw std::overflow_error("Depth interval outside numerical range");
    std::size_t eligible = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        CocoRow row;
        row.rate = double(static_cast<long double>(o.minimumRate) +
                          i * static_cast<long double>(o.rateStep));
        if (i && !(row.rate > result.rows.back().rate))
            throw std::invalid_argument("Rate step below floating point resolution");
        row.rayleigh = spatialRayleigh * row.rate / 100;
        row.nyquist = spatialNyquist * row.rate / 100;
        if (!positive(row.rayleigh) || !positive(row.nyquist))
            throw std::overflow_error("Converted frequency outside numerical range");
        for (auto f : frequencies)
            if (f >= row.rayleigh && f < row.nyquist)
                ++row.resolvedTargets;
        if (row.nyquist < o.maximumFrequency)
            row.status = CocoStatus::Nyquist;
        else if (row.resolvedTargets != frequencies.size())
            row.status = CocoStatus::Resolution;
        else
        {
            const auto requested = o.maximumFrequency / row.nyquist * (fft / 2);
            row.bins = std::min(fft / 2, std::size_t(std::floor(requested + 1e-12)));
            if (row.bins < 8)
                row.status = CocoStatus::Resolution;
            else
                ++eligible;
        }
        result.rows.push_back(row);
    }
    if (!eligible)
        throw std::domain_error("No candidate resolves the complete target and frequency band; "
                                "adjust rate range, band or input duration/sampling");
    if (static_cast<long double>(eligible) * frequencies.size() * 2 * fft *
            (std::log2(double(fft)) + 1) >
        CocoLimits::maxWork)
        throw std::length_error(
            "COCO work limit exceeded; narrow rate range, enlarge step or reduce padding");
    return result;
}
std::vector<double> observedPower(const std::vector<double> &y, std::size_t fft,
                                  CocoOptions::Detrend mode)
{
    std::vector<double> scaled = y;
    double magnitude = 0;
    for (auto v : y)
    {
        if (!std::isfinite(v))
            throw std::invalid_argument("Non-finite sample");
        magnitude = std::max(magnitude, std::abs(v));
    }
    if (!(magnitude > 0))
        throw std::domain_error("No signal variance");
    for (auto &v : scaled)
        v /= magnitude;
    // Reject constants and exact linear series removed by requested detrending.
    auto residual = scaled;
    detrend(residual, mode);
    const auto bounds = std::minmax_element(residual.begin(), residual.end());
    if (*bounds.second - *bounds.first <= 64 * std::numeric_limits<double>::epsilon())
        throw std::domain_error("No resolvable signal variance after detrending");
    return power(std::move(scaled), fft, mode);
}
std::vector<double> targetPower(std::size_t samples, double dx, double rate, std::size_t fft,
                                const CocoOptions &o, const std::function<bool()> &cancelled)
{
    std::vector<double> frequencies;
    for (double period : o.periodsKa)
        frequencies.push_back(1 / period);
    const double maxWeight = *std::max_element(o.relativePower.begin(), o.relativePower.end());
    std::vector<double> target(fft / 2 + 1, 0), basis(samples);
    for (std::size_t j = 0; j < frequencies.size(); ++j)
    {
        const double radians = 2 * pi * frequencies[j] * dx * 100 / rate;
        for (int phase = 0; phase < 2; ++phase)
        {
            check(cancelled);
            for (std::size_t i = 0; i < basis.size(); ++i)
                basis[i] = phase ? std::cos(radians * i) : std::sin(radians * i);
            const auto p = power(basis, fft, o.detrend);
            for (std::size_t k = 0; k < target.size(); ++k)
                target[k] += .5 * (o.relativePower[j] / maxWeight) * p[k];
        }
    }
    return target;
}
} // namespace coco_detail
CocoResult cocoScan(const std::vector<double> &y, double dx, const CocoOptions &o,
                    const std::function<bool()> &cancelled,
                    const std::function<void(int)> &progress)
{
    using namespace coco_detail;
    auto result = grid(y.size(), dx, o, cancelled);
    const auto fft = result.fftSize;
    const auto observed = observedPower(y, fft, o.detrend);
    bool haveBest = false;
    for (std::size_t r = 0; r < result.rows.size(); ++r)
    {
        check(cancelled);
        auto &row = result.rows[r];
        if (row.status == CocoStatus::Eligible)
        {
            const auto target = targetPower(y.size(), dx, row.rate, fft, o, cancelled);
            row.correlation = correlation(observed, target, row.bins);
            if (!std::isfinite(row.correlation))
                row.status = CocoStatus::NoVariance;
            else if (!haveBest || row.correlation > result.rows[result.bestRow].correlation)
            {
                haveBest = true;
                result.bestRow = r;
                result.frequency.resize(row.bins);
                result.observedPower.assign(observed.begin() + 1, observed.begin() + row.bins + 1);
                result.targetPower.assign(target.begin() + 1, target.begin() + row.bins + 1);
                for (std::size_t k = 0; k < row.bins; ++k)
                    result.frequency[k] = (k + 1) * row.rate / (100 * double(fft) * dx);
            }
        }
        if (progress)
            progress(int(100 * (r + 1) / result.rows.size()));
    }
    check(cancelled);
    if (!haveBest)
        throw std::domain_error("No candidate has nonconstant observed and target spectra");
    normalize(result.observedPower);
    normalize(result.targetPower);
    return result;
}
} // namespace aster::chronology
