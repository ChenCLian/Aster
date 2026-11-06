// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/spectrum/lomb_scargle.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace aster::numeric
{
namespace
{
constexpr long double tau = 6.283185307179586476925286766559L;
void stop(const std::function<bool()> &cancelled)
{
    if (cancelled && cancelled())
        throw LombCancelled{};
}
long double dot(const std::vector<long double> &a, const std::vector<long double> &b,
                const std::function<bool()> &cancelled)
{
    long double s = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancelled);
        s += a[i] * b[i];
    }
    return s;
}
} // namespace
LombResult lombScargle(const std::vector<double> &x, const std::vector<double> &y,
                       const LombOptions &o, const std::function<bool()> &cancel,
                       const std::function<void(int)> &progress)
{
    stop(cancel);
    if (x.size() != y.size() || x.size() < 5)
        throw std::invalid_argument("Lomb-Scargle requires at least five paired samples");
    if (x.size() > LombLimits::maxSamples)
        throw std::length_error("Lomb-Scargle sample budget exceeded");
    if (!std::isfinite(o.minimumFrequency) || !std::isfinite(o.maximumFrequency) ||
        o.minimumFrequency < 0 || o.maximumFrequency < 0 || o.samplesPerPeak < 1 ||
        o.samplesPerPeak > 20)
        throw std::invalid_argument("Invalid Lomb-Scargle frequency limits or grid density");
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || (i && !(x[i] > x[i - 1])))
            throw std::invalid_argument(
                "Lomb-Scargle requires finite data and strictly increasing X");
    }
    const auto bounds = std::minmax_element(y.begin(), y.end());
    if (*bounds.first == *bounds.second)
        throw std::domain_error("Normalized Lomb-Scargle is undefined for a constant series");
    // Subtract a safe midpoint before scaling: retain small variations on a
    // large baseline without overflowing for opposite near-maximum values.
    const long double offset = .5L * (*bounds.first) + .5L * (*bounds.second);
    const long double magnitude =
        std::max(std::abs(static_cast<long double>(*bounds.first) - offset),
                 std::abs(static_cast<long double>(*bounds.second) - offset));
    LombResult r;
    r.duration = static_cast<double>(static_cast<long double>(x.back()) - x.front());
    if (!std::isfinite(r.duration) || r.duration <= 0)
        throw std::overflow_error("Unrepresentable record duration");
    std::vector<double> gaps(x.size() - 1);
    for (std::size_t i = 0; i < gaps.size(); ++i)
        gaps[i] = x[i + 1] - x[i];
    auto middle = gaps.begin() + gaps.size() / 2;
    std::nth_element(gaps.begin(), middle, gaps.end());
    r.medianSpacing = *middle;
    if (gaps.size() % 2 == 0)
        r.medianSpacing = r.medianSpacing / 2 + *std::max_element(gaps.begin(), middle) / 2;
    stop(cancel);
    const double minimum = o.minimumFrequency > 0 ? o.minimumFrequency : 1 / r.duration;
    const double maximum = o.maximumFrequency > 0 ? o.maximumFrequency : .5 / r.medianSpacing;
    r.requestedMaximum = maximum;
    r.frequencyStep = (1 / r.duration) / o.samplesPerPeak;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(r.frequencyStep) ||
        r.frequencyStep <= 0 || minimum <= 0 || maximum <= minimum)
        throw std::invalid_argument(
            "Require a finite positive frequency band with maximum > minimum");
    if (static_cast<long double>(minimum) * r.duration < LombLimits::minCycles ||
        static_cast<long double>(maximum) * r.duration > LombLimits::maxCycles)
        throw std::invalid_argument("Frequency times duration must be between 1e-6 and 1e8 cycles");
    const long double steps = (static_cast<long double>(maximum) - minimum) / r.frequencyStep;
    const long double count =
        std::floor(steps + 32 * std::numeric_limits<double>::epsilon() * std::max(1.L, steps)) + 1;
    if (count < 2)
        throw std::invalid_argument("Frequency interval contains fewer than two grid points");
    if (count > LombLimits::maxFrequencies ||
        count > static_cast<long double>(LombLimits::maxWorkTerms) / x.size())
        throw std::length_error("Lomb-Scargle grid/work budget exceeded; narrow the frequency band "
                                "or reduce grid density");
    const auto n = static_cast<std::size_t>(count);
    r.frequency.resize(n);
    r.power.resize(n);
    std::vector<long double> t(x.size()), yc(x.size()), c(x.size()), s(x.size());
    long double mean = 0;
    for (std::size_t i = 0; i < y.size(); ++i)
    {
        if ((i & 4095) == 0)
            stop(cancel);
        t[i] = static_cast<long double>(x[i]) - x.front();
        yc[i] = (static_cast<long double>(y[i]) - offset) / magnitude;
        mean += yc[i] / y.size();
    }
    for (auto &v : yc)
        v -= mean;
    const long double total = dot(yc, yc, cancel);
    if (!std::isfinite(total) || total <= 0)
        throw std::domain_error("No representable variance for normalized Lomb-Scargle");
    const long double threshold =
        LombLimits::rankTolerance * std::sqrt(static_cast<long double>(x.size()));
    int previousPercent = -1;
    for (std::size_t k = 0; k < n; ++k)
    {
        stop(cancel);
        const double f = static_cast<double>(static_cast<long double>(minimum) +
                                             k * static_cast<long double>(r.frequencyStep));
        if (!std::isfinite(f) || (k && f <= r.frequency[k - 1]))
            throw std::overflow_error("Frequency grid loses floating-point resolution");
        r.frequency[k] = f;
        long double cm = 0, sm = 0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            if ((i & 4095) == 0)
                stop(cancel);
            const double phase =
                static_cast<double>(tau * std::remainder(static_cast<long double>(f) * t[i], 1.L));
            c[i] = std::cos(phase);
            s[i] = std::sin(phase);
            cm += c[i] / x.size();
            sm += s[i] / x.size();
        }
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            c[i] -= cm;
            s[i] -= sm;
        }
        const auto cc = dot(c, c, cancel), ss = dot(s, s, cancel);
        if (ss > cc)
            c.swap(s); // Pivot the two centered harmonic columns.
        const long double norm = std::sqrt(std::max(cc, ss));
        long double explained = 0;
        if (norm <= threshold)
            ++r.rankZeroBins;
        else
        {
            for (auto &v : c)
                v /= norm;
            const long double first = dot(c, yc, cancel);
            explained = first * first;
            // Reorthogonalization keeps low-frequency/near-alias fits stable.
            for (int pass = 0; pass < 2; ++pass)
            {
                const auto projection = dot(c, s, cancel);
                for (std::size_t i = 0; i < s.size(); ++i)
                    s[i] -= projection * c[i];
            }
            const long double residualNorm = std::sqrt(dot(s, s, cancel));
            if (residualNorm <= threshold)
                ++r.rankOneBins;
            else
            {
                const auto second = dot(s, yc, cancel) / residualNorm;
                explained += second * second;
            }
        }
        const long double power = explained / total;
        if (!std::isfinite(power) || power < 0 || power > 1 + 1e-8L)
            throw std::overflow_error("Unrepresentable normalized Lomb-Scargle power");
        r.power[k] = std::clamp(static_cast<double>(power), 0., 1.);
        const int percent = int(100 * (k + 1) / n);
        if (progress && percent != previousPercent)
        {
            previousPercent = percent;
            progress(percent);
        }
    }
    stop(cancel);
    return r;
}
} // namespace aster::numeric
